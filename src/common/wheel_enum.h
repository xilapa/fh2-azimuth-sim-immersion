// SPDX-License-Identifier: MIT
// DirectInput device listing shared by the plugin and the settings window. Define DIRECTINPUT_VERSION first.
#pragma once
#include <dinput.h>
#include <string>
#include <vector>
#include "wheel_pick.h"

namespace fh2 {

struct FoundDevice { GUID guid; std::wstring name; DWORD type; DWORD productData1; bool ffb; };   // productData1: low word vendor, high word product

inline std::vector<FoundDevice> ListGameDevices(IDirectInput8W* di) {
    std::vector<FoundDevice> v;
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, [](const DIDEVICEINSTANCEW* d, void* c) -> BOOL {
        static_cast<std::vector<FoundDevice>*>(c)->push_back({d->guidInstance, d->tszProductName, d->dwDevType, d->guidProduct.Data1, false});
        return DIENUM_CONTINUE;
    }, &v, DIEDFL_ATTACHEDONLY);
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, [](const DIDEVICEINSTANCEW* d, void* c) -> BOOL {
        for (auto& x : *static_cast<std::vector<FoundDevice>*>(c)) if (IsEqualGUID(x.guid, d->guidInstance)) x.ffb = true;
        return DIENUM_CONTINUE;
    }, &v, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    return v;
}

inline std::vector<WheelCandidate> WheelCandidates(const std::vector<FoundDevice>& devs) {
    std::vector<WheelCandidate> c;
    for (auto& d : devs)
        c.push_back({d.name, GET_DIDEVICE_TYPE(d.type) == DI8DEVTYPE_DRIVING, GET_DIDEVICE_TYPE(d.type) == DI8DEVTYPE_GAMEPAD, d.ffb});
    return c;
}

// Asks the device which axes have a motor; the device must have its data format set.
inline DWORD ForceAxisOf(IDirectInputDevice8W* dev, DWORD steeringOffset) {
    // by offset, not EnumObjects: that reports the device's native offsets, which effects do not take
    static const DWORD kAxes[] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ, DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)};
    std::vector<unsigned long> actuators;
    for (DWORD ofs : kAxes) {
        DIDEVICEOBJECTINSTANCEW o{};
        o.dwSize = sizeof(o);
        if (SUCCEEDED(dev->GetObjectInfo(&o, ofs, DIPH_BYOFFSET)) && (o.dwFlags & DIDOI_FFACTUATOR)) actuators.push_back(ofs);
    }
    return (DWORD)PickForceAxis(steeringOffset, actuators);
}

inline void AutocenterOff(IDirectInputDevice8W* dev) {
    DIPROPDWORD ac{};
    ac.diph.dwSize = sizeof(ac);
    ac.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    ac.diph.dwObj = 0;
    ac.diph.dwHow = DIPH_DEVICE;
    ac.dwData = DIPROPAUTOCENTER_OFF;
    dev->SetProperty(DIPROP_AUTOCENTER, &ac.diph);   // failures ignored
}

} // namespace fh2
