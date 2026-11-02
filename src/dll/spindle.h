// SPDX-License-Identifier: MIT
// Turns the steering wheel bone further than the driver animation can. The animation only spans about
// 90 degrees each way, so the bone is rebuilt from its centred pose instead. Pure maths, no game access.
#pragma once
#include <cmath>
#include <cstring>

namespace fh2 {

struct Spindle {
    bool  hasRef = false;
    float refMag = 3.0f * 3.14159265f / 180.0f;   // the reference is only replaced by a pose nearer to the centre
    float ref[3][3] = {};   // bone axes in car space with the wheel centred
    int   axis = -1;        // bone row the wheel turns around, -1 = not known yet
    float sign = 1.0f;      // +1 when row axis+1 turns towards row axis+2 for a positive angle
};

inline float SpindleDot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// m: the bone's 4x4 matrix (rows are its axes), c: the car's 4x4 matrix, angle: the wanted wheel angle in radians.
// Returns true when `out` holds the bone turned to `angle`; false means use `m` unchanged.
inline bool SpindleRotate(Spindle& s, const float* m, const float* c, float angle, float* out) {
    const float kDeg = 3.14159265f / 180.0f;
    float l[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) l[i][j] = SpindleDot(m + i * 4, c + j * 4);
    const float mag = std::fabs(angle);
    if (mag < s.refMag) {
        std::memcpy(s.ref, l, sizeof(l));
        s.refMag = mag;
        s.hasRef = true;
    }
    if (!s.hasRef) return false;
    if (s.axis < 0) {
        if (mag < 20.0f * kDeg || mag > 85.0f * kDeg) return false;
        int best = 0, stayed = 0;
        for (int i = 0; i < 3; ++i) {
            const float d = SpindleDot(l[i], s.ref[i]);
            if (d > 0.995f) { best = i; ++stayed; }
        }
        if (stayed != 1) return false;     // no single row stayed put: not a rotation around a bone axis
        s.axis = best;
        const float sinA = SpindleDot(l[(best + 1) % 3], s.ref[(best + 2) % 3]);
        s.sign = sinA * angle > 0.0f ? 1.0f : -1.0f;
    }
    // Built from the reference, never from the animated bone: animation noise cannot shake the wheel.
    const int j = (s.axis + 1) % 3, k = (s.axis + 2) % 3;
    const float ca = std::cos(angle), sa = std::sin(angle) * s.sign;
    float r[3][3];
    for (int t = 0; t < 3; ++t) {
        r[s.axis][t] = s.ref[s.axis][t];
        r[j][t] = ca * s.ref[j][t] + sa * s.ref[k][t];
        r[k][t] = ca * s.ref[k][t] - sa * s.ref[j][t];
    }
    std::memcpy(out, m, 16 * sizeof(float));
    for (int i = 0; i < 3; ++i)
        for (int t = 0; t < 3; ++t) out[i * 4 + t] = r[i][0] * c[t] + r[i][1] * c[4 + t] + r[i][2] * c[8 + t];
    return true;
}

} // namespace fh2
