// SPDX-License-Identifier: MIT
// dinput8.dll next to azimuth.exe. Azimuth opens DirectInput before ExtraOptions loads any mod, so this proxy keeps
// Azimuth from opening the wheel while Sim Immersion drives it. Everything else goes to the system dinput8.dll.
#include <windows.h>
#include <string>

namespace {

HINSTANCE g_self = nullptr;

// Azimuth 1.0 opens the wheel exclusively at start, which would lock the mod out of its force feedback.
bool azimuthAsks(void* caller) {
    HMODULE from = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(caller), &from);
    return from == GetModuleHandleW(nullptr);
}

// Refused only when ExtraOptions will load the mod and the mod wants the wheel; otherwise Azimuth would be left with no wheel at all.
bool keepWheelFromAzimuth() {
    wchar_t self[MAX_PATH];
    const DWORD n = GetModuleFileNameW(g_self, self, MAX_PATH);
    std::wstring dir(self, n);
    dir.resize(dir.find_last_of(L"\\/") + 1);
    const std::wstring mod = dir + L"mods\\Sim Immersion\\";
    auto exists = [](const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    return exists(dir + L"winmm.dll") && exists(mod + L"plugins\\FH2SimImmersion.dll") &&
           GetPrivateProfileIntW(L"Wheel", L"Enabled", 1, (mod + L"FH2SimImmersion.ini").c_str()) != 0;
}

} // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    typedef HRESULT (WINAPI *Fn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    if (azimuthAsks(__builtin_return_address(0)) && keepWheelFromAzimuth()) return E_FAIL;
    static Fn real = nullptr;
    if (!real) {
        wchar_t dir[MAX_PATH];
        const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
        const std::wstring path = std::wstring(dir, n) + L"\\dinput8.dll";
        if (HMODULE m = LoadLibraryW(path.c_str())) real = reinterpret_cast<Fn>(GetProcAddress(m, "DirectInput8Create"));
    }
    return real ? real(inst, version, iid, out, outer) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
