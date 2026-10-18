// DirectInput steering wheel reader (Logitech G29 and friends).
#pragma once
#include <windows.h>
#include <cstdint>
#include "config.h"

namespace fh2 {

struct WheelInput {            // already mapped to game conventions
    float steering = 0;        // -1..1 BEFORE rotation scaling (full lock of the physical wheel = +-1)
    float throttle = 0, brake = 0, clutch = 0;   // 0..1, 0 = released
    bool  a = false, b = false, x = false, y = false, view = false, menu = false;
    bool  up = false, down = false, left = false, right = false, lb = false, rb = false;
    int   gear = 0;            // -1 reverse, 0 neutral, 1..7 (only when H-shifter enabled)
    uint64_t timestamp = 0;    // QPC ticks of last poll
    bool  valid = false;       // device acquired and polled within the last 500 ms
    int32_t  raw[8] = {};      // raw DirectInput axes: lX, lY, lZ, lRx, lRy, lRz, slider0, slider1
    uint32_t rawButtons[4] = {};   // bit i of word i/32 set when DirectInput button i is down (0..127)
};

// Enumerates, logs, starts the poll thread; false if no device (unless TestMode).
bool WheelDevice_Start(const WheelConfig& cfg);
// Copy of the latest snapshot; returns out.valid.
bool WheelDevice_Read(WheelInput& out);

} // namespace fh2
