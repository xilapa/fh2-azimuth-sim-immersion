// SPDX-License-Identifier: MIT
// FH2 Sim Immersion: an ExtraOptions plugin, loaded into azimuth.exe from mods\Sim Immersion\plugins.
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <string>
#include "config.h"
#include "hook.h"
#include "log.h"
#include "tracker.h"

#ifndef FH2SI_VERSION   // set by the build from CMakeLists.txt project(VERSION)
#define FH2SI_VERSION "dev"
#endif

namespace {

std::wstring moduleDir(HMODULE m) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring s(path, n);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

// The plugin sits in <mod folder>\plugins; its settings and log are in the mod folder.
std::wstring modDir(HMODULE m) {
    std::wstring d = moduleDir(m);
    size_t slash = d.find_last_of(L"\\/");
    if (slash != std::wstring::npos && _wcsicmp(d.c_str() + slash + 1, L"plugins") == 0) return d.substr(0, slash);
    return d;
}

DWORD WINAPI initThread(void* param) {
    HMODULE self = static_cast<HMODULE>(param);
    std::wstring dir = modDir(self);
    std::wstring ini = dir + L"\\FH2SimImmersion.ini";

    const std::wstring log = dir + L"\\FH2SimImmersion.log";
    if (GetPrivateProfileIntW(L"Logging", L"Enabled", 1, ini.c_str()) != 0) fh2::Log::open(log.c_str());
    else DeleteFileW(log.c_str());     // the Logging tab shows this file: an old one must not pass for this run's log

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    LOG("FH2 Sim Immersion " FH2SI_VERSION " loaded into pid %lu (%ls)", GetCurrentProcessId(), exe);

    fh2::Config cfg = fh2::loadConfig(ini);
    if (cfg.overrideAzimuthHeadTracking) fh2::Tracker::start(cfg);
    fh2::installWhenReady(cfg);
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    return TRUE;
}

// ExtraOptions calls this once, on its loader thread, after the game is mapped and its own patches are in.
// Without ExtraOptions nothing calls it and the mod stays off.
extern "C" __declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const void* /*context*/) {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return TRUE;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,   // our hooks point here: never unload
                       reinterpret_cast<LPCWSTR>(&FH2PluginInitialize), &self);
    if (HANDLE t = CreateThread(nullptr, 0, initThread, self, 0, nullptr)) CloseHandle(t);
    return TRUE;
}
