// SPDX-License-Identifier: MIT
// Realistic ABS / traction control / stability control / clutch / automatic shifting: overrides the game's LocalPlayerChoices getters per car
// and writes the same choices to the player's car every frame.
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
// Does nothing unless RealisticABS, RealisticTC, RealisticSTM, RealisticClutch or RealisticAutomatic is on.
void AssistHook_Install(uintptr_t base, const Config& cfg);

// Records the car ordinal of the local player's car (called from the FFB call-site hook and the car update hook, game thread).
void AssistHook_SetPlayerCar(uint32_t ordinal);

// Re-maps the assist bytes the game already copied to the player's car (called from the car update hook, every frame).
void AssistHook_ApplyToCar(uint8_t* car);

} // namespace fh2
