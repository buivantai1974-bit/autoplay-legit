#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <unordered_map>
#include <cmath>
#include "Physics.hpp"

using namespace geode::prelude;

struct HazardRect {
    float x, y, w, h;
    int id;
    bool isOrb;
    bool isPad;
    bool isTrigger;
};

struct InputFrame {
    bool jump;
    bool valid;
};

struct SolverResult {
    std::vector<InputFrame> inputs;
    bool solved = false;
    int failFrame = -1;
};

class LevelSolver {
public:
    static SolverResult solve(PlayLayer* layer, GameObject* player);

private:
    static std::vector<HazardRect> extractHazards(GameObject* player, float maxX);
    static bool rectCollide(float px, float py, float pw, float ph, HazardRect const& h);
    static bool simulatePath(
        float startX, float startY, float speedMul,
        std::vector<HazardRect> const& hazards,
        std::vector<InputFrame>& out, int& failFrame
    );
};