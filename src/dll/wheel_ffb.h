// Emulation of the Xbox wheel's on-device force feedback programs (pure math, no Windows calls).
#pragma once

namespace fh2 {

struct FfbParams {
    float torque = 0, menuSpringScale = 0, raceSpringScale = 0, raceSpringMax = 0,
          dampScale = 0, dampMax = 0, leftAmp = 0, leftWl = 0, rightAmp = 0, rightWl = 0;
    bool race = false;   // heartbeat seen within 2.5 s
    bool stale = true;   // no variable writes for > 1 s
};

struct FfbTerms { float torque, spring, damping, rumble, menu, total; };

// Returns the clamped total force in game units (-0.5..0.5). terms may be null.
float FfbForce(const FfbParams& p, float angle, float velocity, double tMs, FfbTerms* terms);
// Device magnitude -10000..10000 from a force, gain and invert flag.
long FfbDeviceMagnitude(float force, float gain, bool invert);

} // namespace fh2
