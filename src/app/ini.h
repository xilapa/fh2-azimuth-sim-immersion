// Settings file access: <exe dir>\FH2SimImmersion.ini is the single source of truth.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ini_parse.h"

namespace app {

std::wstring Widen(const std::string& s);          // UTF-8 -> UTF-16
std::string  Narrow(const std::wstring& s);        // UTF-16 -> UTF-8

std::wstring ExeDir();
std::wstring IniPath();        // FH2SimImmersion.ini
std::wstring CarsIniPath();    // FH2SimImmersion_cars.ini
std::wstring LogPath();        // FH2SimImmersion.log
std::wstring DllPath();        // FH2SimImmersion.dll

const std::vector<IniEntry>& Defaults();                           // parsed kDefaultIni
const IniEntry* FindDefault(const char* section, const char* key);

// Writes the default file when missing; moves an older-version file to .old.bak first
// (same rule as the plugin, src/dll/config.cpp).
void EnsureIni();

std::string ReadSetting(const char* section, const char* key);     // INI value, else the default
bool        WriteSetting(const char* section, const char* key, const std::string& value);

} // namespace app
