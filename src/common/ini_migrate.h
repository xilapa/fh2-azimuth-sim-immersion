// SPDX-License-Identifier: MIT
// Shared by the plugin (src/dll/config.cpp) and the settings window (src/app/ini.cpp): creates the
// settings file, and on a ConfigVersion bump keeps the old file as .old.bak and carries its values over.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include "default_ini.h"
#include "ini_parse.h"

namespace fh2 {

// Returns true if an older-version file was moved to <ini>.old.bak (*kept = values copied to the new file).
// *created (if given) is set when a fresh default file was written.
inline bool EnsureCurrentIni(const std::wstring& ini, int* kept = nullptr, bool* created = nullptr) {
    if (kept) *kept = 0;
    if (created) *created = false;
    bool moved = false;
    std::wstring bak = ini + L".old.bak";
    if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetPrivateProfileIntW(L"Input", L"ConfigVersion", 0, ini.c_str()) < kConfigVersion)
        moved = MoveFileExW(ini.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (FILE* f = _wfopen(ini.c_str(), L"w")) {
            fputs(kDefaultIni, f);
            fclose(f);
            if (created) *created = true;
        }
    }
    if (!moved) return false;
    const int oldVersion = GetPrivateProfileIntW(L"Input", L"ConfigVersion", 0, bak.c_str());
    // Force values that were the defaults before version 7 follow the new ones, sized to the game's torque; changed values stay.
    const struct { const char* key; double value; } kOldForceDefaults[] = {
        {"FFBTorqueGain", 2.0}, {"FFBSlipGain", 0.5}, {"FFBAlignGain", 0.6}, {"FFBRumbleGain", 1.5}};
    auto wide = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };   // ASCII names
    std::vector<wchar_t> buf(4096);
    for (const app::IniEntry& e : app::ParseIniText(kDefaultIni)) {
        if (e.section == "Input" && e.key == "ConfigVersion") continue;
        std::wstring sec = wide(e.section), key = wide(e.key);
        GetPrivateProfileStringW(sec.c_str(), key.c_str(), L"\x01", buf.data(), (DWORD)buf.size(), bak.c_str());
        if (wcscmp(buf.data(), L"\x01") == 0) continue;
        bool oldDefault = false;
        if (oldVersion < 7 && e.section == "Wheel")
            for (const auto& d : kOldForceDefaults)
                if (e.key == d.key && wcstod(buf.data(), nullptr) == d.value) oldDefault = true;
        if (oldDefault) continue;
        if (WritePrivateProfileStringW(sec.c_str(), key.c_str(), buf.data(), ini.c_str()) && kept) ++*kept;
    }
    return true;
}

} // namespace fh2
