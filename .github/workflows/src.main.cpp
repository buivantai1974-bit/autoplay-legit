#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <matjson.hpp>
#include <fstream>
#include <deque>
#include "LevelSolver.hpp"
#include "Physics.hpp"

using namespace geode::prelude;

static const std::unordered_set<int> HAZARD_IDS = {
    8, 9, 39, 103, 1704, 1705, 1706, 1707, 1751, 1752, 1753,
    1787, 1788, 1789, 1790, 1831, 1832, 1833, 1834, 1835
};

struct AutoPlayerSave {
    bool enabled = false;
    bool useSolver = true;
    bool fallbackReactive = true;
    float safeThreshold = 1.0f;
    int releaseFrames = 2;
    int jitterMin = 1, jitterMax = 3;
};

template<> struct matjson::Serialize<AutoPlayerSave> {
    static Result<AutoPlayerSave> fromJson(matjson::Value const& v) {
        AutoPlayerSave s;
        GEODE_UNWRAP_INTO(s.enabled, v["enabled"].asBool());
        GEODE_UNWRAP_INTO(s.useSolver, v["useSolver"].asBool());
        GEODE_UNWRAP_INTO(s.fallbackReactive, v["fallbackReactive"].asBool());
        GEODE_UNWRAP_INTO(s.safeThreshold, v["safeThreshold"].asDouble());
        GEODE_UNWRAP_INTO(s.releaseFrames, v["releaseFrames"].asInt());
        GEODE_UNWRAP_INTO(s.jitterMin, v["jitterMin"].asInt());
        GEODE_UNWRAP_INTO(s.jitterMax, v["jitterMax"].asInt());
        return Ok(s);
    }
    static matjson::Value toJson(AutoPlayerSave const& s) {
        auto v = matjson::Value();
        v["enabled"] = s.enabled;
        v["useSolver"] = s.useSolver;
        v["fallbackReactive"] = s.fallbackReactive;
        v["safeThreshold"] = s.safeThreshold;
        v["releaseFrames"] = s.releaseFrames;
        v["jitterMin"] = s.jitterMin;
        v["jitterMax"] = s.jitterMax;
        return v;
    }
};

static AutoPlayerSave g_save;
static void loadSave() {
    auto p = Mod::get()->getSaveDir() / "settings.json";
    if (!std::filesystem::exists(p)) return;
    std::ifstream f(p); if (!f) return;
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    auto r = matjson::parse(raw);
    if (r.isOk()) {
        auto x = matjson::Serialize<AutoPlayerSave>::fromJson(r.unwrap());
        if (x.isOk()) g_save = x.unwrap();
    }
}
static void saveSave() {
    std::ofstream f(Mod::get()->getSaveDir() / "settings.json");
    f << matjson::Serialize<AutoPlayerSave>::toJson(g_save).dump();
}

class AutoPlayUI : public geode::Popup<> {
    CCMenuItemToggler* m_toggle = nullptr;
    CCMenuItemToggler* m_solverToggle = nullptr;
protected:
    bool setup() override {
        auto ws = CCDirector::get()->getWinSize();
        this->setTitle("Auto Player");

        m_toggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(AutoPlayUI::onToggle), 0.7f);
        m_toggle->toggle(g_save.enabled);
        m_solverToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(AutoPlayUI::onSolver), 0.7f);
        m_solverToggle->toggle(g_save.useSolver);

        auto menu = CCMenu::create();
        menu->addChild(m_toggle);
        menu->addChild(m_solverToggle);
        menu->alignItemsVerticallyWithPadding(12.f);
        menu->setPosition({ ws.width / 2, ws.height / 2 });
        this->addChild(menu);

        auto l1 = CCLabelBMFont::create("Enabled", "bigFont.fnt"); l1->setScale(0.5f);
        l1->setPosition({ ws.width / 2 + 50, ws.height / 2 + 12 });
        this->addChild(l1);
        auto l2 = CCLabelBMFont::create("Solver", "bigFont.fnt"); l2->setScale(0.5f);
        l2->setPosition({ ws.width / 2 + 50, ws.height / 2 - 12 });
        this->addChild(l2);
        return true;
    }
    void onToggle(CCObject*) { g_save.enabled = m_toggle->isToggled(); saveSave(); }
    void onSolver(CCObject*) { g_save.useSolver = m_solverToggle->isToggled(); saveSave(); }
public:
    static AutoPlayUI* create() {
        auto r = new AutoPlayUI();
        if (r->init(260.f, 180.f)) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
};

class $modify(AutoPlayLayer, PlayLayer) {
    struct Fields {
        SolverResult m_solver;
        int m_frame = 0;
        bool m_calibrated = false;
        int m_calibrateAttempts = 0;
        GameObject* m_lastHazard = nullptr;
        int m_releaseTimer = 0;
        bool m_pressed = false;
        int m_jitterOffset = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        loadSave();
        m_fields->m_frame = 0;
        m_fields->m_calibrated = Physics::g_phys.calibrated;
        m_fields->m_calibrateAttempts = 0;
        m_fields->m_pressed = false;
        m_fields->m_releaseTimer = 0;
        m_fields->m_solver = {};
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (!g_save.enabled || !m_player1 || m_player1->m_isDead) return;

        if (!m_fields->m_calibrated) {
            calibratePhysics();
            return;
        }

        if (g_save.useSolver && m_fields->m_frame == 0) {
            m_fields->m_solver = LevelSolver::solve(this, m_player1);
            if (m_fields->m_solver.solved) {
                log::info("solver: solved {} frames", m_fields->m_solver.inputs.size());
            } else {
                log::warn("solver: fail at frame {}, reactive fallback", m_fields->m_solver.failFrame);
            }
        }

        bool hold = false;
        bool useReactive = !m_fields->m_solver.solved || !g_save.useSolver;

        if (!useReactive && m_fields->m_frame < (int)m_fields->m_solver.inputs.size()) {
            auto& inp = m_fields->m_solver.inputs[m_fields->m_frame];
            if (!inp.valid) useReactive = true;
            else hold = inp.jump;
        }
        if (useReactive && g_save.fallbackReactive) {
            hold = reactiveDecision();
        }

        applyHold(hold);
        m_fields->m_frame++;
    }

    bool reactiveDecision() {
        auto p = m_player1;
        if (!p) return false;
        float px = p->getPositionX(), py = p->getPositionY();
        auto pr = p->getObjectRect();
        auto mode = p->m_gameMode;

        GameObject* nearest = nullptr;
        float nd = FLT_MAX;
        auto it = std::lower_bound(m_objects->begin(), m_objects->end(), px,
            [](GameObject* o, float x){ return o && o->getPositionX() < x; });
        for (auto s = it; s != m_objects->end(); ++s) {
            auto o = *s; if (!o) continue;
            float ox = o->getPositionX();
            if (ox > px + 400.f) break;
            if (ox < px - 20.f) continue;
            if (!HAZARD_IDS.contains(o->m_objectID)) continue;
            float d = ox - px;
            if (d > 0 && d < nd) { nd = d; nearest = o; }
        }
        if (!nearest) return false;

        float safe = g_save.safeThreshold * Physics::pxPerFrame(1.0f) * Physics::framesToApex();
        auto nr = nearest->getObjectRect();

        switch (mode) {
            case GameMode::Cube:
            case GameMode::Robot:
            case GameMode::Spider:
            case GameMode::Swing:
                return nd < safe;
            case GameMode::Ship:
            case GameMode::UFO: {
                auto ws = CCDirector::get()->getWinSize();
                if (py > ws.height - 80.f) return false;
                if (py < 80.f) return true;
                return nd < safe && nr.getMidY() > pr.getMidY();
            }
            case GameMode::Wave: {
                auto ws = CCDirector::get()->getWinSize();
                float mid = ws.height / 2.f;
                if (py < mid - 30.f) return true;
                if (py > mid + 30.f) return false;
                return false;
            }
            case GameMode::Ball: {
                bool flip = p->m_isUpsideDown;
                if (flip) return nd < safe && nr.getMidY() < pr.getMidY();
                return nd < safe && nr.getMidY() > pr.getMidY();
            }
            default:
                return nd < safe;
        }
    }

    void applyHold(bool hold) {
        auto p = m_player1;
        if (m_fields->m_releaseTimer > 0) {
            if (--m_fields->m_releaseTimer == 0 && m_fields->m_pressed) {
                p->releaseButton(PlayerButton::Jump);
                m_fields->m_pressed = false;
            }
            return;
        }
        if (hold && !m_fields->m_pressed) {
            p->pushButton(PlayerButton::Jump);
            m_fields->m_pressed = true;
            int j = g_save.jitterMin + (std::rand() % (g_save.jitterMax - g_save.jitterMin + 1));
            m_fields->m_releaseTimer = g_save.releaseFrames + j;
        } else if (!hold && m_fields->m_pressed) {
            p->releaseButton(PlayerButton::Jump);
            m_fields->m_pressed = false;
        }
    }

    void calibratePhysics() {
        auto p = m_player1;
        if (!p) return;
        auto f = m_fields;
        if (f->m_frame == 0) {
            p->pushButton(PlayerButton::Jump);
            f->m_pressed = true;
        }
        if (f->m_frame == 3 && f->m_pressed) {
            float vy1 = p->m_yVelocity;
            f->m_jitterOffset = (int)(vy1 * 1000.f);
        }
        if (f->m_frame == 5 && f->m_pressed) {
            float vy1 = f->m_jitterOffset / 1000.f;
            float vy2 = p->m_yVelocity;
            if (std::abs(vy2 - vy1) > 1e-3f) {
                Physics::g_phys.gravity = std::abs(vy2 - vy1);
                Physics::g_phys.jumpVelocity = std::abs(vy1) + Physics::g_phys.gravity * 0.05f;
            }
            p->releaseButton(PlayerButton::Jump);
            f->m_pressed = false;
            Physics::g_phys.calibrated = true;
            f->m_calibrated = true;
            log::info("calibrated gravity={} jumpV={}", Physics::g_phys.gravity, Physics::g_phys.jumpVelocity);
            f->m_solver = {};
            f->m_frame = -1;
        }
        f->m_frame++;
    }
};