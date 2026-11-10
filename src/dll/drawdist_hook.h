// SPDX-License-Identifier: MIT
// Pop-in killer: moves the game's detail switches of scenery, trees, fences and grass farther out (never closer).
// Values are multipliers, 0 or 1 = leave the game's.
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
// Installs only the parts whose setting is non-zero.
void DrawDist_Install(uintptr_t base, const Config& cfg);

} // namespace fh2
