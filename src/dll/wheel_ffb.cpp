#include "wheel_ffb.h"
#include <cmath>

namespace fh2 {

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
float limit(float v, float mx) { if (mx < 0) mx = 0; return clampf(v, -mx, mx); }
float sq(float a, float wl, double t) {
    if (!(wl > 0) || a == 0) return 0;
    return std::fmod(t, (double)wl) < (double)wl / 2 ? a : -a;
}
} // namespace

float FfbForce(const FfbParams& p, float angle, float velocity, double tMs, FfbTerms* terms) {
    FfbTerms t{0, 0, 0, 0, 0, 0};
    if (!p.stale) {
        if (p.race) {
            t.torque  = p.torque;
            t.spring  = limit(-p.raceSpringScale * angle, p.raceSpringMax);
            t.damping = limit(-p.dampScale * velocity, p.dampMax);
            t.rumble  = sq(p.leftAmp, p.leftWl, tMs) + sq(p.rightAmp, p.rightWl, tMs);
            t.total   = t.torque + t.spring + t.damping + t.rumble;
        } else {
            t.menu  = -p.menuSpringScale * angle;
            t.total = t.menu;
        }
        t.total = clampf(t.total, -0.5f, 0.5f);
    }
    if (terms) *terms = t;
    return t.total;
}

long FfbDeviceMagnitude(float force, float gain, bool invert) {
    float m = clampf(force / 0.5f * gain, -1.0f, 1.0f) * 10000.0f;
    if (!invert) m = -m;
    return (long)std::lround(m);
}

} // namespace fh2
