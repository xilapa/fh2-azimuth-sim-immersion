// SPDX-License-Identifier: MIT
// Force feedback model (see wheel_ffb.h).
#include "wheel_ffb.h"
#include <cmath>
#include <cstdlib>

namespace fh2 {

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
float limit(float v, float mx) { if (mx < 0) mx = 0; return clampf(v, -mx, mx); }
float sq(float a, float wl, double t) {
    if (!(wl > 0) || a == 0) return 0;
    return std::fmod(t, (double)wl) < (double)wl / 2 ? a : -a;
}
} // namespace

float FfbForce(const FfbParams& p, const FfbTuning& tune, float angle, float velocity, double tMs, FfbTerms* terms) {
    FfbTerms t{0, 0, 0, 0, 0, 0};
    if (!p.stale) {
        if (p.race) {
            t.torque  = p.torque * tune.torqueGain;
            t.spring  = limit(-p.raceSpringScale * tune.springGain * angle, p.raceSpringMax);
            t.damping = limit(-p.dampScale * velocity, p.dampMax);
            t.rumble  = sq(p.leftAmp, p.leftWl, tMs) + sq(p.rightAmp, p.rightWl, tMs);
            t.rumble *= tune.rumbleGain;
            t.total   = t.torque + t.spring + t.damping + t.rumble;
        } else {
            t.menu  = -p.menuSpringScale * angle;
            t.total = t.menu;
        }
    }
    if (terms) *terms = t;
    return t.total;
}

float FfbForce(const FfbParams& p, float angle, float velocity, double tMs, FfbTerms* terms) {
    return FfbForce(p, FfbTuning(), angle, velocity, tMs, terms);
}

long FfbDeviceMagnitude(float force, float gain, bool invert) {
    float m = clampf(force * gain, -1.0f, 1.0f) * 10000.0f;
    if (!invert) m = -m;
    return (long)std::lround(m);
}

long FfbMinForce(long mag, float minForce) {
    if (!(minForce > 0.0f) || mag == 0) return mag;
    const float lo = clampf(minForce, 0.0f, 1.0f) * 10000.0f, a = (float)std::labs(mag);
    const float kRamp = 100.0f;   // the first 1% ramps up to the floor, so the wheel does not jerk across the centre
    float out = lo + a * (10000.0f - lo) / 10000.0f;
    if (a < kRamp) out = a / kRamp * (lo + kRamp * (10000.0f - lo) / 10000.0f);
    return std::lround(mag > 0 ? out : -out);
}

} // namespace fh2
