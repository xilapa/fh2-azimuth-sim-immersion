// SPDX-License-Identifier: MIT
// In-game steering wheel that follows the real wheel, and hiding the driver (arms) in the cockpit view.
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
// Does nothing unless CockpitHook_NeedsCarTracking(cfg).
void CockpitHook_Install(uintptr_t base, const Config& cfg);

// True when any setting needs the local player's car to be tracked (the car update hook then runs).
bool CockpitHook_NeedsCarTracking(const Config& cfg);

// The local player's car object (called from the FFB call-site hook, game thread, every frame).
void CockpitHook_SetPlayerCar(uint8_t* car);

} // namespace fh2
