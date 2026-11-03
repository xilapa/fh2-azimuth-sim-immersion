// SPDX-License-Identifier: MIT
// Emulation of the Xbox wheel's on-device force feedback programs (pure math, no Windows calls).
#pragma once
#include <cstdint>

namespace fh2 {

// Inputs the game hands to UpdateFFB each frame, plus the driver's parameters (captured by a hook).
struct GameFfbInput {
    float rawTorque = 0, speed = 0, slip = 0, gain = 0, dynScale = 0, dynMax = 0, springParam = 0,
          slip0 = 0, slip1 = 0, slipScale0 = 0, slipScale1 = 0, speed0 = 0, speed1 = 0,
          speedScale0 = 0, speedScale1 = 0;
    uint64_t qpc = 0;
};

struct FfbParams {
    float torque = 0, menuSpringScale = 0, raceSpringScale = 0, raceSpringMax = 0,
          dampScale = 0, dampMax = 0, leftAmp = 0, leftWl = 0, rightAmp = 0, rightWl = 0;
    bool race = false;   // heartbeat seen within 2.5 s
    bool stale = true;   // no variable writes for > 1 s
    GameFfbInput game;   // latest UpdateFFB input
    bool haveGame = false;   // captured within the last 0.5 s
};

struct FfbTuning {
    float torqueGain = 1.0f, springGain = 1.0f, slipSpringFloor = 0.4f;   // slipSpringFloor: share of the spring kept when the rear slides
    float rumbleGain = 1.0f;
};

struct FfbTerms { float torque, spring, damping, rumble, menu, total; };

// Returns the total force in game units (1.0 = the wheel's maximum). terms may be null.
float FfbForce(const FfbParams& p, const FfbTuning& tune, float angle, float velocity, double tMs, FfbTerms* terms);
// Default tuning.
float FfbForce(const FfbParams& p, float angle, float velocity, double tMs, FfbTerms* terms);
// Device magnitude -10000..10000 from a force, gain and invert flag.
long FfbDeviceMagnitude(float force, float gain, bool invert);
// Lifts a device magnitude (-10000..10000) over the wheel's dead band: |out| = floor + |mag| * (1 - floor),
// with the first 1% ramping up from 0 so the force does not jump across the centre. minForce 0 = unchanged.
long FfbMinForce(long mag, float minForce);

} // namespace fh2
