// SPDX-License-Identifier: MIT
// Host-side unit tests for the force feedback model (g++ -std=c++17 tests/test_ffb.cpp src/dll/wheel_ffb.cpp).
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../src/dll/wheel_ffb.h"
#include "../src/common/ffb_calib.h"

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
        assert(near(FfbForce(p, 2.0f, 0, 0, nullptr), -1.0f));
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
    // 7. no cap on the total: the game's value goes to the wheel
    {
        FfbParams p = live(true); p.torque = 0.9f;
        assert(near(FfbForce(p, 0, 0, 0, nullptr), 0.9f));
    }
    // 9. torque gain
    {
        FfbParams p = live(true); p.torque = 0.1f;
        FfbTuning t;
        assert(near(FfbForce(p, t, 0, 0, 0, nullptr), 0.1f));
        t.torqueGain = 2.0f;
        assert(near(FfbForce(p, t, 0, 0, 0, nullptr), 0.2f));
    }
    // 10. game-driven spring with slip floor
    auto game = [] {
        FfbParams p = live(true);
        p.haveGame = true; p.raceSpringMax = 0.5f; p.raceSpringScale = 0.9f;
        GameFfbInput& g = p.game;
        g.speed = 20; g.slip = 0.6f; g.gain = 1; g.springParam = 0.5f;
        g.slip0 = 0.32f; g.slip1 = 0.55f; g.slipScale0 = 1; g.slipScale1 = 0;
        g.speed0 = 2; g.speed1 = 15; g.speedScale0 = 0.15f; g.speedScale1 = 1;
        return p;
    };
    {
        FfbParams p = game();
        FfbTuning t; t.torqueGain = 1; t.slipSpringFloor = 0.4f;
        FfbTerms tm{};
        FfbForce(p, t, 0.5f, 0, 0, &tm);
        assert(near(tm.spring, -0.1f));
        t.slipSpringFloor = 0;                       // no floor: the game's own spring variable, as its program uses
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -0.45f));
        t.slipSpringFloor = 0.4f;
        p.game.slip = 0.1f; p.game.speed = 1.0f;      // slow, no slip: speedF clamps to 0.15
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.15f * 1.0f * 0.5f) * 0.5f));
        t.springGain = 2.0f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.15f * 0.5f) * 2.0f * 0.5f));
        // swapped / equal points
        p.game.speed = 8.5f; p.game.speed0 = 15; p.game.speed1 = 2; p.game.speedScale0 = 1; p.game.speedScale1 = 0.15f;
        t.springGain = 1.0f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.575f * 0.5f) * 0.5f));
        p.game.speed0 = p.game.speed1 = 5; p.game.speedScale0 = 0.3f; p.game.speedScale1 = 0.8f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -(0.8f * 0.5f) * 0.5f));
    }
    // 14. rumble gain
    {
        FfbParams p = live(true); p.leftAmp = 0.1f; p.leftWl = 40.0f;
        FfbTuning t; t.torqueGain = 1; t.rumbleGain = 1;
        FfbTerms tm{};
        FfbForce(p, t, 0, 0, 10.0, &tm);
        assert(near(tm.rumble, 0.1f));
        t.rumbleGain = 2;
        FfbForce(p, t, 0, 0, 10.0, &tm);
        assert(near(tm.rumble, 0.2f));
        t.rumbleGain = 0;
        FfbForce(p, t, 0, 0, 10.0, &tm);
        assert(tm.rumble == 0.0f);
    }
    // 12. no game input: fall back to the captured spring scale
    {
        FfbParams p = game();
        p.haveGame = false;
        FfbTuning t; t.torqueGain = 1; t.slipSpringFloor = 0.4f; t.springGain = 1.0f;
        FfbTerms tm{};
        FfbForce(p, t, 0.5f, 0, 0, &tm);
        assert(near(tm.spring, -0.45f));
        t.springGain = 0.5f;
        assert(near(FfbForce(p, t, 0.5f, 0, 0, nullptr), -0.225f));
    }

    // 15. default tuning: the game's torque unchanged, plus the extra forces
    {
        FfbParams p = game();
        p.torque = 0.1f;
        FfbTerms tm{};
        FfbForce(p, FfbTuning(), 0.5f, 0, 0, &tm);
        assert(near(tm.torque, 0.1f) && near(tm.spring, -0.1f));
    }
    // 8. device magnitude
    assert(FfbDeviceMagnitude(0.25f, 1.0f, false) == -2500);
    assert(FfbDeviceMagnitude(0.25f, 1.0f, true) == 2500);
    assert(FfbDeviceMagnitude(0.6f, 2.0f, false) == -10000);
    assert(FfbDeviceMagnitude(-0.1f, 1.0f, false) == 1000);

    // minimum force
    {
        assert(FfbMinForce(0, 0.05f) == 0);
        assert(FfbMinForce(5000, 0.0f) == 5000);
        assert(FfbMinForce(10000, 0.05f) == 10000);
        assert(FfbMinForce(-10000, 0.05f) == -10000);
        assert(FfbMinForce(5000, 0.05f) == 5250);
        assert(FfbMinForce(-5000, 0.05f) == -5250);
        assert(FfbMinForce(100, 0.05f) == 595);
        assert(FfbMinForce(50, 0.05f) == 298);
        assert(FfbMinForce(-50, 0.05f) == -298);
    }

    // calibration (src/common/ffb_calib.h)
    {
        assert(near(GameTorqueMax(0.41f, 0.5f, 0.6f), 0.205f));
        assert(near(GameTorqueMax(0.41f, 1.0f, 0.3f), 0.3f));     // capped at dynMax
        assert(near(GameTorqueMax(2.0f, 1.0f, 5.0f), 1.0f));      // never above 1
        assert(GameTorqueMax(0.0f, 0.5f, 0.6f) == 0.0f);
        assert(GameTorqueMax(std::nanf(""), 0.5f, 0.6f) == 0.0f);
        assert(near(CalibratedTorqueGain(0.205f, 1.5f, 0.05f, 0.85f), 2.7f));
        assert(near(WheelShare(0.205f, 2.7f, 1.5f, 0.05f), 0.838f, 1e-3f));
        assert(near(CalibratedTorqueGain(0.2f, 1.5f, 0.05f, 0.85f), 2.8f));
        assert(CalibratedTorqueGain(0.0f, 1.5f, 0.05f, 0.85f) == 0.0f);
        assert(CalibratedTorqueGain(0.2f, 0.0f, 0.05f, 0.85f) == 0.0f);
        assert(near(CalibratedTorqueGain(0.001f, 1.0f, 0.05f, 0.85f), 5.0f));
        assert(near(CalibratedTorqueGain(1.0f, 4.0f, 0.05f, 0.85f), 0.2f));
        assert(WheelShare(0.0f, 1.0f, 1.0f, 0.05f) == 0.0f);
        assert(near(WheelShare(1.0f, 3.0f, 1.0f, 0.05f), 1.0f));

        // a recorded drive
        assert(TorqueBin(0.0f) == 0);
        assert(TorqueBin(0.5f) == 100);
        assert(TorqueBin(-0.5f) == 100);
        assert(TorqueBin(2.0f) == kTorqueBins - 1);
        assert(TorqueBin(std::nanf("")) == 0);
        uint32_t bins[kTorqueBins] = {};
        assert(TorquePercentile(bins, 0.99) == 0.0f);
        bins[10] = 100;
        bins[150] = 1;
        assert(near(TorquePercentile(bins, 0.5), 0.055f));
        assert(near(TorquePercentile(bins, 0.99), 0.055f));
        assert(near(TorquePercentile(bins, 1.0), 0.755f));
    }

    std::puts("all ffb tests passed");
    return 0;
}
