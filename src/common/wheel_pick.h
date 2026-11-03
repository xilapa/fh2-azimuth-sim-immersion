// SPDX-License-Identifier: MIT
// Device rules shared by the plugin and the settings window. No Windows headers, so tests/test_wheel.cpp runs on any host.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

namespace fh2 {

struct WheelCandidate { std::wstring name; bool driving; bool gamepad; bool ffb; };

inline std::wstring WheelLower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

// First device whose name contains `match` (any case); -1 if none or `match` is empty.
inline int FindDeviceByName(const std::vector<WheelCandidate>& c, const std::wstring& match) {
    if (match.empty()) return -1;
    const std::wstring m = WheelLower(match);
    for (size_t i = 0; i < c.size(); ++i)
        if (WheelLower(c[i].name).find(m) != std::wstring::npos) return (int)i;
    return -1;
}

// The name match; else a wheel with force feedback; else anything but a gamepad with force feedback
// (some wheels do not report as wheels); else a wheel without it; else -1.
inline int PickWheel(const std::vector<WheelCandidate>& c, const std::wstring& match) {
    int i = FindDeviceByName(c, match);
    if (i >= 0) return i;
    for (size_t k = 0; k < c.size(); ++k) if (c[k].driving && c[k].ffb) return (int)k;
    for (size_t k = 0; k < c.size(); ++k) if (!c[k].gamepad && c[k].ffb) return (int)k;
    for (size_t k = 0; k < c.size(); ++k) if (c[k].driving) return (int)k;
    return -1;
}

// Logitech gear wheels (about 2 Nm), where the soft lock at full force is safe.
inline bool IsKnownGentleWheel(unsigned vid, unsigned pid) {
    if (vid != 0x046D) return false;
    static const unsigned kPids[] = {0xC298, 0xC299, 0xC29A, 0xC29B,   // DFP, G25, DFGT, G27
                                     0xC24F, 0xC260, 0xC261, 0xC262,   // G29, G920
                                     0xC266, 0xC267, 0xC26D, 0xC26E};  // G923
    return std::find(std::begin(kPids), std::end(kPids), pid) != std::end(kPids);
}

// The axis the force effect drives: the steering axis if it has a motor, else the first axis that has one.
inline unsigned long PickForceAxis(unsigned long steering, const std::vector<unsigned long>& actuators) {
    if (actuators.empty() || std::find(actuators.begin(), actuators.end(), steering) != actuators.end()) return steering;
    return actuators[0];
}

// Some pedals report a made-up value (0 or half) until they first move: count them as released until then.
struct PedalRest {
    bool seen = false, hasMoved = false;
    long first = 0;
    bool moved(long raw) {
        if (!seen) { seen = true; first = raw; }
        if (std::labs(raw - first) > 1300) hasMoved = true;   // 2% of the 0..65535 range: more than noise
        return hasMoved;
    }
};

} // namespace fh2
