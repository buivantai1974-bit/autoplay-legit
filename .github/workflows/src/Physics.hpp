#pragma once
#include <Geode/Geode.hpp>

struct PhysicsConst {
    float gravity = 0.958199f;
    float jumpVelocity = 11.180032f;
    float terminalVelocity = 15.0f;
    float speedNormal = 311.58f;
    float gravityScale = 1.0f;
    float jumpScale = 1.0f;
    bool calibrated = false;
};

namespace Physics {
    inline PhysicsConst g_phys;

    inline float pxPerFrame(float speedMul) {
        return (g_phys.speedNormal * speedMul) / 60.0f;
    }

    inline float jumpHeight() {
        float v = g_phys.jumpVelocity * g_phys.jumpScale;
        float g = g_phys.gravity * g_phys.gravityScale;
        return (v * v) / (2.0f * g);
    }

    inline int framesToApex() {
        float v = g_phys.jumpVelocity * g_phys.jumpScale;
        float g = g_phys.gravity * g_phys.gravityScale;
        return (int)std::ceil((v / g) * 60.0f / 60.0f);
    }
}