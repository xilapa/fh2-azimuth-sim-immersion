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
// Piecewise linear between (x0 -> y0) and (x1 -> y1), clamped to the end values outside.
float piecewise(float x, float x0, float y0, float x1, float y1) {
    if (x0 > x1) { float tx = x0, ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty; }
    if (x0 == x1) return y1;
    if (x <= x0) return y0;
    if (x >= x1) return y1;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}
} // namespace

float FfbForce(const FfbParams& p, const FfbTuning& tune, float angle, float velocity, double tMs, FfbTerms* terms) {
    FfbTerms t{0, 0, 0, 0, 0, 0};
    if (!p.stale) {
        if (p.race) {
            t.torque  = p.torque * tune.torqueGain;
            float scale;
            if (p.haveGame && tune.slipSpringFloor > 0.0f) {     // the floor needs the parts of the game's spring
                const GameFfbInput& g = p.game;
                float speedF = piecewise(g.speed, g.speed0, g.speedScale0, g.speed1, g.speedScale1);
                float slipF  = piecewise(std::fabs(g.slip), g.slip0, g.slipScale0, g.slip1, g.slipScale1);
                scale = speedF * std::fmax(slipF, tune.slipSpringFloor) * g.springParam * g.gain * tune.springGain;
            } else {
                scale = p.raceSpringScale * tune.springGain;
            }
            t.spring  = limit(-scale * angle, p.raceSpringMax);
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
