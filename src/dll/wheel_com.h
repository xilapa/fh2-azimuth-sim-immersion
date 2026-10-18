// The virtual Xbox wheel: a fake Windows.Xbox.Input.IController/IWheel and its helper objects.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include "config.h"

namespace fh2 {

void  VirtualWheel_Configure(const Config& cfg);   // call before anything else (stores settings)
void* VirtualWheel_Controller();          // IController* of the single static fake controller (borrowed)
void  VirtualWheel_SetGameBase(uintptr_t base);
float VirtualWheel_GameRotation();        // last SetRotation value (degrees), default cfg.wheelRange
// Takes over one reference to `user` (may be null) as the controller's User (UserMode auto).
void  VirtualWheel_SetUser(void* user);
// Formats the registered variables (current [min..max]) for the diagnostics line and
// resets their min/max.
void  VirtualWheel_FormatVars(char* buf, size_t cap);

} // namespace fh2
