// SPDX-License-Identifier: MIT
// dinput8.dll for the folder of azimuth.exe. Azimuth imports this name, so Windows loads this file when Azimuth starts.
// It loads FH2SimImmersion.dll from the same folder and hands DirectInput8Create on to the system's dinput8.dll, so
// the mod runs without the settings window.
#include <windows.h>
#include <string>

namespace {

HINSTANCE g_self = nullptr;

std::wstring besideSelf(const wchar_t* file) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/") + 1) + file;
}

// The settings window imports this file too when it sits in the same folder; there this is only the real dinput8.dll.
bool inSettingsWindow() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    const std::wstring s(path, n);
    return _wcsicmp(s.substr(s.find_last_of(L"\\/") + 1).c_str(), L"FH2SimImmersion.exe") == 0;
}

DWORD WINAPI loadPlugin(void*) {
    LoadLibraryW(besideSelf(L"FH2SimImmersion.dll").c_str());      // missing file: Azimuth just runs without the mod
    return 0;
}

// Azimuth 1.0 opens the wheel exclusively at start, which would lock the mod out of its force feedback.
bool azimuthAsks(void* caller) {
    HMODULE from = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(caller), &from);
    return from == GetModuleHandleW(nullptr);
}

// Only refused when the mod is there and wants the wheel; otherwise Azimuth would be left with no wheel at all.
bool keepWheelFromAzimuth() {
    const std::wstring ini = besideSelf(L"FH2SimImmersion.ini");
    return GetFileAttributesW(besideSelf(L"FH2SimImmersion.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
           GetPrivateProfileIntW(L"Wheel", L"Enabled", 1, ini.c_str()) != 0 &&
           GetPrivateProfileIntW(L"Wheel", L"OverrideAzimuthWheel", 1, ini.c_str()) != 0;
}

} // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    typedef HRESULT (WINAPI *Fn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    if (azimuthAsks(__builtin_return_address(0)) && !inSettingsWindow() && keepWheelFromAzimuth()) return E_FAIL;
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
        // LoadLibrary is not safe inside DllMain; this thread runs once the loader lock is free.
        if (!inSettingsWindow())
            if (HANDLE t = CreateThread(nullptr, 0, loadPlugin, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
