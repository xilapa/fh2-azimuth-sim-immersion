// SPDX-License-Identifier: MIT
// Host-side unit tests for the device rules (g++ -std=c++17 tests/test_wheel.cpp).
#include <cassert>
#include <cstdio>
#include "../src/common/wheel_pick.h"

using namespace fh2;

int main() {
    // separate pedals of wheel type come first in the list: the wheel with force feedback still wins
    std::vector<WheelCandidate> c = {
        {L"Xbox pad", false, true, true},
        {L"Heusinkveld Pedals", true, false, false},
        {L"Logitech G29 Driving Force", true, false, true}};
    assert(PickWheel(c, L"") == 2);
    assert(PickWheel(c, L"g29") == 2);
    assert(PickWheel(c, L"HEUSINKVELD") == 1);        // the name always wins
    assert(PickWheel(c, L"Moza") == 2);               // an old DeviceMatch that matches nothing
    // a direct drive base that does not report as a wheel beats pedals without force feedback, a gamepad never does
    c = {{L"Pad", false, true, true}, {L"Pedals", true, false, false}, {L"DD Base", false, false, true}};
    assert(PickWheel(c, L"") == 2);
    c = {{L"Pad", false, true, true}, {L"Pedals", true, false, false}};
    assert(PickWheel(c, L"") == 1);
    c = {{L"Pad", false, true, true}};
    assert(PickWheel(c, L"") == -1);
    assert(PickWheel({}, L"") == -1);
    assert(FindDeviceByName(c, L"") == -1);
    assert(FindDeviceByName(c, L"nope") == -1);
    assert(FindDeviceByName(c, L"pad") == 0);

    assert(IsKnownGentleWheel(0x046D, 0xC24F));       // G29
    assert(IsKnownGentleWheel(0x046D, 0xC29B));       // G27
    assert(!IsKnownGentleWheel(0x046D, 0xC272));      // G Pro: direct drive
    assert(!IsKnownGentleWheel(0x346E, 0xC24F));      // same product number, other maker

    assert(PickForceAxis(0, {}) == 0);
    assert(PickForceAxis(0, {0, 4}) == 0);
    assert(PickForceAxis(8, {0, 4}) == 0);

    PedalRest r;
    assert(!r.moved(32768));
    assert(!r.moved(32768 + 1300));                   // noise
    assert(r.moved(32768 + 1301));
    assert(r.moved(32768));                           // stays moved
    PedalRest down;
    assert(!down.moved(65535));
    assert(down.moved(0));

    std::printf("test_wheel: all checks passed\n");
    return 0;
}
