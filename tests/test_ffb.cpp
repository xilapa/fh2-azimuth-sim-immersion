// Host-side unit tests for the force feedback model (g++ -std=c++17 tests/test_ffb.cpp src/dll/wheel_ffb.cpp).
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../src/dll/wheel_ffb.h"

using namespace fh2;

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

static FfbParams live(bool race) { FfbParams p; p.stale = false; p.race = race; return p; }

int main() {
    // 1. stale
    {
        FfbParams p; p.menuSpringScale = 0.5f; p.torque = 0.3f; p.race = true;   // stale defaults true
        FfbTerms t{1, 1, 1, 1, 1, 1};
        assert(FfbForce(p, 0.4f, 1.0f, 0, &t) == 0.0f);
        assert(t.torque == 0 && t.spring == 0 && t.damping == 0 && t.rumble == 0 && t.menu == 0 && t.total == 0);
    }
    // 2. menu
    {
        FfbParams p = live(false); p.menuSpringScale = 0.5f;
        assert(near(FfbForce(p, 0.4f, 0, 0, nullptr), -0.2f));
        assert(near(FfbForce(p, 2.0f, 0, 0, nullptr), -0.5f));
    }
    // 3. torque only
    {
        FfbParams p = live(true); p.torque = 0.08f;
        assert(near(FfbForce(p, 0, 0, 0, nullptr), 0.08f));
    }
    // 4. spring clamp
    {
        FfbParams p = live(true); p.raceSpringScale = 0.04f; p.raceSpringMax = 0.5f;
        assert(near(FfbForce(p, 1.0f, 0, 0, nullptr), -0.04f));
        p.raceSpringScale = 2.0f; p.raceSpringMax = 0.3f;
        assert(near(FfbForce(p, 1.0f, 0, 0, nullptr), -0.3f));
        assert(near(FfbForce(p, -1.0f, 0, 0, nullptr), 0.3f));
    }
    // 5. damping
    {
        FfbParams p = live(true); p.dampScale = 0.33f; p.dampMax = 0.31f;
        assert(near(FfbForce(p, 0, 0.5f, 0, nullptr), -0.165f));
        assert(near(FfbForce(p, 0, 10.0f, 0, nullptr), -0.31f));
    }
    // 6. rumble
    {
        FfbParams p = live(true); p.leftAmp = 0.1f; p.leftWl = 40.0f;
        assert(near(FfbForce(p, 0, 0, 10.0, nullptr), 0.1f));
        assert(near(FfbForce(p, 0, 0, 30.0, nullptr), -0.1f));
        p.leftWl = 0;
        assert(near(FfbForce(p, 0, 0, 10.0, nullptr), 0.0f));
    }
    // 7. total clamp
    {
        FfbParams p = live(true); p.torque = 0.9f;
        assert(near(FfbForce(p, 0, 0, 0, nullptr), 0.5f));
    }
    // 8. device magnitude
    assert(FfbDeviceMagnitude(0.25f, 1.0f, false) == -5000);
    assert(FfbDeviceMagnitude(0.25f, 1.0f, true) == 5000);
    assert(FfbDeviceMagnitude(0.5f, 2.0f, false) == -10000);
    assert(FfbDeviceMagnitude(-0.1f, 1.0f, false) == 2000);

    std::puts("all ffb tests passed");
    return 0;
}
