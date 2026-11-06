// SPDX-License-Identifier: MIT
// Force feedback calibration, shared by the plugin (reads the game's values, records drives) and the app (calibrates).
// No Windows calls.
#pragma once
#include <cmath>
#include <cstdint>

namespace fh2 {

// A recorded drive: the game's steering force |torque| 0..1 in steps of 0.005; the last bin also holds everything above.
constexpr int kTorqueBins = 200;

inline int TorqueBin(float torque) {
    const float a = std::fabs(torque);
    if (!(a >= 0.0f)) return 0;   // NaN
    const int i = (int)(a * kTorqueBins);
    return i >= kTorqueBins ? kTorqueBins - 1 : i;
}

// The upper edge of the bin where the share q (0..1) of the samples is reached; 0 when there are none.
inline float TorquePercentile(const uint32_t* bins, double q) {
    uint64_t total = 0;
    for (int i = 0; i < kTorqueBins; ++i) total += bins[i];
    if (total == 0) return 0.0f;
    const double want = q * (double)total;
    uint64_t sum = 0;
    for (int i = 0; i < kTorqueBins; ++i) {
        sum += bins[i];
        if (bins[i] && (double)sum >= want) return (float)(i + 1) / kTorqueBins;
    }
    return 1.0f;
}

// The game's steering force is clamp(tyre force * dynScale * gain, +-min(dynMax, 1)) (UpdateFFB, 0x1410741a0), and the
// tyre force is capped at +-1, so its strongest value is known without driving.
inline float GameTorqueMax(float dynScale, float gain, float dynMax) {
    float m = dynScale * gain;
    const float cap = dynMax < 1.0f ? dynMax : 1.0f;
    if (m > cap) m = cap;
    return m > 0.0f ? m : 0.0f;   // also NaN
}

// The strongest steering force with the game's default force feedback scale (50%): dynScale 0.41, read from a drive.
constexpr float kDefaultGameTorqueMax = 0.205f;

// The share of the wheel's maximum a steering force reaches, as the force loop computes it without spring and damping:
// torque * torqueGain * gain (Strength), clipped at 1, then lifted over the wheel's dead band by minForce.
inline float WheelShare(float torque, float torqueGain, float gain, float minForce) {
    float x = std::fabs(torque) * torqueGain * gain;
    if (!(x > 0.0f)) return 0.0f;
    if (x > 1.0f) x = 1.0f;
    return minForce + x * (1.0f - minForce);
}

// The Steering torque gain that brings the force torqueMax to target (0..1) of the wheel's maximum.
// Rounded to 0.1 and kept within 0.1..5 (the INI's range); 0 when torqueMax or gain is not positive.
inline float CalibratedTorqueGain(float torqueMax, float gain, float minForce, float target) {
    if (!(torqueMax > 0.0f) || !(gain > 0.0f)) return 0.0f;
    float x = (target - minForce) / (1.0f - minForce);   // the force before the dead band lift
    if (!(x > 0.01f)) x = 0.01f;
    float g = std::round(x / (gain * torqueMax) * 10.0f) / 10.0f;
    return g < 0.1f ? 0.1f : g > 5.0f ? 5.0f : g;
}

} // namespace fh2
