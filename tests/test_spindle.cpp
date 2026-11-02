// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "../src/dll/spindle.h"
using namespace fh2;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static const float kD = 3.14159265f / 180.0f;

// A car turned 30 degrees and moved away from the origin.
static void carMatrix(float* c) {
    const float y = 30.0f * kD;
    const float v[16] = {std::cos(y), 0, -std::sin(y), 0, 0, 1, 0, 0, std::sin(y), 0, std::cos(y), 0, 100, 5, -40, 1};
    for (int i = 0; i < 16; ++i) c[i] = v[i];
}

static void toWorld(float* m, const float l[3][3], const float* c) {
    for (int i = 0; i < 3; ++i) {
        for (int t = 0; t < 3; ++t) m[i * 4 + t] = l[i][0] * c[t] + l[i][1] * c[4 + t] + l[i][2] * c[8 + t];
        m[i * 4 + 3] = 0;
    }
    m[12] = 101; m[13] = 6; m[14] = -39; m[15] = 1;
}

// Bone at wheel angle a: the axis row stays, the other two turn in direction s. The rest pose is tilted like a steering column.
static void bone(float* m, const float* c, int axis, float s, float a) {
    const float t = 20.0f * kD;
    const float r[3][3] = {{1, 0, 0}, {0, std::cos(t), std::sin(t)}, {0, -std::sin(t), std::cos(t)}};
    const int j = (axis + 1) % 3, k = (axis + 2) % 3;
    float l[3][3];
    for (int n = 0; n < 3; ++n) {
        l[axis][n] = r[axis][n];
        l[j][n] = std::cos(a) * r[j][n] + s * std::sin(a) * r[k][n];
        l[k][n] = std::cos(a) * r[k][n] - s * std::sin(a) * r[j][n];
    }
    toWorld(m, l, c);
}

static bool same(const float* a, const float* b) {
    for (int i = 0; i < 16; ++i) if (std::fabs(a[i] - b[i]) > 1e-3f) return false;
    return true;
}

int main() {
    float c[16], m[16], want[16], out[16];
    carMatrix(c);
    {
        Spindle s;
        bone(m, c, 2, 1, 45 * kD);
        CHECK(!SpindleRotate(s, m, c, 45 * kD, out));        // no centred pose seen yet
        CHECK(s.axis == -1);
    }
    for (int axis = 0; axis < 3; ++axis) {
        for (float dir : {1.0f, -1.0f}) {
            Spindle s;
            bone(m, c, axis, dir, 0);
            CHECK(!SpindleRotate(s, m, c, 0, out));
            CHECK(s.hasRef);
            bone(m, c, axis, dir, 45 * kD);
            bone(want, c, axis, dir, 45 * kD);
            CHECK(SpindleRotate(s, m, c, 45 * kD, out));     // the axis is learned on this call
            CHECK(same(out, want));
            CHECK(s.axis == axis);
            CHECK(s.sign == dir);
            bone(m, c, axis, dir, 90 * kD);                  // the animation stops at 90
            bone(want, c, axis, dir, 270 * kD);
            CHECK(SpindleRotate(s, m, c, 270 * kD, out));
            CHECK(same(out, want));
            bone(m, c, axis, dir, -90 * kD);
            bone(want, c, axis, dir, -450 * kD);
            CHECK(SpindleRotate(s, m, c, -450 * kD, out));
            CHECK(same(out, want));
            CHECK(out[12] == 101 && out[13] == 6 && out[14] == -39 && out[15] == 1);
        }
    }
    {   // a noisy animation does not move the result, and the reference only gets nearer to the centre
        Spindle s;
        bone(m, c, 2, 1, 2 * kD);
        SpindleRotate(s, m, c, 2 * kD, out);
        bone(m, c, 2, 1, 0);
        SpindleRotate(s, m, c, 0, out);
        bone(m, c, 2, 1, 2 * kD);
        SpindleRotate(s, m, c, 2 * kD, out);              // a pose further from the centre is not taken
        CHECK(s.refMag == 0.0f);
        bone(m, c, 2, 1, 45 * kD);
        SpindleRotate(s, m, c, 45 * kD, out);
        bone(want, c, 2, 1, 200 * kD);
        bone(m, c, 2, 1, 90 * kD);
        CHECK(SpindleRotate(s, m, c, 200 * kD, out));
        CHECK(same(out, want));
        bone(m, c, 2, 1, 88 * kD);
        CHECK(SpindleRotate(s, m, c, 200 * kD, out));
        CHECK(same(out, want));
    }
    {   // the direction is found from a negative angle too
        Spindle s;
        bone(m, c, 1, -1, 0);
        SpindleRotate(s, m, c, 0, out);
        bone(m, c, 1, -1, -60 * kD);
        SpindleRotate(s, m, c, -60 * kD, out);
        CHECK(s.axis == 1);
        CHECK(s.sign == -1);
    }
    {   // past 85 degrees the axis is not looked for
        Spindle s;
        bone(m, c, 0, 1, 0);
        SpindleRotate(s, m, c, 0, out);
        bone(m, c, 0, 1, 90 * kD);
        CHECK(!SpindleRotate(s, m, c, 200 * kD, out));
        CHECK(s.axis == -1);
    }
    {   // a turn around no single row leaves the axis unknown and the bone untouched
        Spindle s;
        const float id[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        toWorld(m, id, c);
        SpindleRotate(s, m, c, 0, out);
        const float a = 45 * kD, n = 1.0f / std::sqrt(3.0f), ca = std::cos(a), sa = std::sin(a), t = 1 - ca;
        const float x = t * n * n + ca, y = t * n * n - sa * n, z = t * n * n + sa * n;
        const float l[3][3] = {{x, y, z}, {z, x, y}, {y, z, x}};
        toWorld(m, l, c);
        CHECK(!SpindleRotate(s, m, c, 45 * kD, out));
        CHECK(s.axis == -1);
    }
    if (fails) { printf("%d spindle check(s) FAILED\n", fails); return 1; }
    printf("all spindle tests passed\n");
    return 0;
}
