// Field-of-view adjustment for the cockpit, driver, hood and bumper cameras.
#pragma once
#include <cstdint>
#include "config.h"

namespace fh2 {

// Called from installWhenReady once the verified game image base is known.
void FovHook_Install(uintptr_t base, const Config& cfg);

} // namespace fh2
