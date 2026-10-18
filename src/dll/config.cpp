#include "config.h"
#include "log.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace fh2 {

static const char kDefaultIni[] =
"; FH2 Head Tracking for Azimuth - settings. Edit, then restart the game.\n"
"\n"
"[Input]\n"
"ConfigVersion=4\n"
"; auto      = FreeTrack/TrackIR shared memory when present (Beam \"Head Tracking Device\n"
";             Emulation\", OpenTrack \"freetrack 2.0 Enhanced\"), otherwise UDP\n"
"; freetrack = shared memory only\n"
"; udp       = OpenTrack \"UDP over network\" only\n"
"Source=auto\n"
"; 1 = write the received pose and the camera offsets to the log every 2 seconds (troubleshooting)\n"
"Diagnostics=0\n"
"\n"
"[Network]\n"
"; OpenTrack: Output = \"UDP over network\", address 127.0.0.1, port below.\n"
"UdpPort=4242\n"
"; 127.0.0.1 = this PC only. 0.0.0.0 = also accept a phone/second PC on the LAN.\n"
"BindAddress=127.0.0.1\n"
"\n"
"[General]\n"
"EnableOnStartup=1\n"
"; 1 = cockpit camera only. 0 = also hood and bumper cameras.\n"
"CockpitOnly=1\n"
"; With no packets for this long the view eases back to centre.\n"
"StaleTimeoutMs=500\n"
"; 0.0 none .. 1.0 heavy. Prefer OpenTrack's own filter (Accela) and leave 0.\n"
"Smoothing=0.0\n"
"\n"
"[Hotkeys]\n"
"; Windows virtual key codes in hex. End=0x23 Home=0x24 PgUp=0x21 PgDn=0x22 F1..F12=0x70..0x7B\n"
"ToggleKey=0x23\n"
"; Recenter is on the controller by default (see RecenterButtons). 0 = no key.\n"
"RecenterKey=0\n"
"; Same actions with Ctrl+Shift+<key>, for keyboards without a nav cluster.\n"
"ChordToggleKey=0x59\n"
"ChordRecenterKey=0\n"
"; Xbox controller buttons that recenter when pressed together, joined with +.\n"
"; A B X Y LB RB L3 R3 BACK START UP DOWN LEFT RIGHT. Empty = off.\n"
"RecenterButtons=L3+R3\n"
"\n"
"; Gain multiplies your head movement. Invert flips an axis. Limit caps the\n"
"; camera offset (degrees for rotation, metres for position); Limit=0 turns\n"
"; that axis off. Sensitivity curves belong in OpenTrack's Mapping window.\n"
"[Rotation]\n"
"YawGain=1.0\n"
"YawInvert=0\n"
"YawLimit=90\n"
"PitchGain=1.0\n"
"PitchInvert=0\n"
"PitchLimit=40\n"
"RollGain=1.0\n"
"RollInvert=0\n"
"RollLimit=20\n"
"\n"
"[Position]\n"
"Enabled=1\n"
"XGain=1.0\n"
"XInvert=0\n"
"XLimit=0.10\n"
"YGain=1.0\n"
"YInvert=0\n"
"YLimit=0.06\n"
"ZGain=1.0\n"
"ZInvert=0\n"
"ZLimit=0.08\n"
"\n"
"[Advanced]\n"
"; Which component of the game's rotation Param receives yaw, pitch, roll (0,1,2).\n"
"RotOrder=0,1,2\n"
"; Which car-space component receives head X, Y, Z (0,1,2).\n"
"PosOrder=0,1,2\n"
"\n"
"[Wheel]\n"
"; Steering wheel support (DirectInput). 0 = off.\n"
"Enabled=1\n"
"; Part of the device name to look for; empty = first steering wheel found.\n"
"DeviceMatch=G29\n"
"; Rotation range set in Logitech G HUB, in degrees.\n"
"WheelRange=900\n"
"SteeringAxis=X\n"
"SteeringInvert=0\n"
"ThrottleAxis=Y\n"
"BrakeAxis=RZ\n"
"ClutchAxis=SLIDER0\n"
"; 1 = the pedal axes read maximum when released (G29 default).\n"
"PedalsInverted=1\n"
"PedalDeadzone=0.02\n"
"; DirectInput button numbers (0-based) for the game's buttons; -1 = none.\n"
"ButtonA=0\n"
"ButtonB=2\n"
"ButtonX=1\n"
"ButtonY=3\n"
"ButtonView=8\n"
"ButtonMenu=9\n"
"ButtonLB=5\n"
"ButtonRB=4\n"
"; 1 = Logitech Driving Force Shifter connected.\n"
"HShifter=0\n"
"GearButtons=12,13,14,15,16,17,-1\n"
"ReverseButton=18\n"
"; Who the wheel belongs to: auto = same player as the first Xbox controller (if any), none = no player.\n"
"UserMode=auto\n"
"; Re-scan controllers once if the game started before the plugin was ready.\n"
"ForceRescan=1\n"
"; Identity reported to the game (Logitech G920).\n"
"VendorId=0x046D\n"
"ProductId=0xC261\n"
"ConfigFlag14=1\n"
"TestMode=0\n";

constexpr int kConfigVersion = 4;

// "L3+R3" -> XINPUT_GAMEPAD_* mask
static unsigned parseButtons(const std::string& spec) {
    static const struct { const char* name; unsigned mask; } kNames[] = {
        {"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008},
        {"START", 0x0010}, {"BACK", 0x0020}, {"L3", 0x0040}, {"R3", 0x0080},
        {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000},
    };
    unsigned mask = 0;
    size_t i = 0;
    while (i < spec.size()) {
        size_t j = spec.find('+', i);
        if (j == std::string::npos) j = spec.size();
        std::string tok;
        for (size_t k = i; k < j; ++k) if (spec[k] != ' ') tok += (char)toupper((unsigned char)spec[k]);
        bool known = tok.empty();
        for (auto& n : kNames) if (tok == n.name) { mask |= n.mask; known = true; }
        if (!known) LOG("config: unknown controller button '%s' in RecenterButtons", tok.c_str());
        i = j + 1;
    }
    return mask;
}

static std::string readStr(const wchar_t* ini, const char* sec, const char* key, const char* def) {
    char buf[256];
    // ANSI path variant is fine for the key/section names; convert path once.
    char path[MAX_PATH * 3];
    WideCharToMultiByte(CP_ACP, 0, ini, -1, path, sizeof(path), nullptr, nullptr);
    GetPrivateProfileStringA(sec, key, def, buf, sizeof(buf), path);
    return buf;
}
static double readF(const wchar_t* ini, const char* sec, const char* key, double def) {
    char d[64]; snprintf(d, sizeof(d), "%g", def);
    return atof(readStr(ini, sec, key, d).c_str());
}
static int readI(const wchar_t* ini, const char* sec, const char* key, int def) {
    char d[32]; snprintf(d, sizeof(d), "%d", def);
    return (int)strtol(readStr(ini, sec, key, d).c_str(), nullptr, 0);
}
static void readOrder(const wchar_t* ini, const char* key, int out[3]) {
    std::string s = readStr(ini, "Advanced", key, "0,1,2");
    int a = 0, b = 1, c = 2;
    if (sscanf(s.c_str(), "%d,%d,%d", &a, &b, &c) == 3 && a >= 0 && a < 3 && b >= 0 && b < 3 &&
        c >= 0 && c < 3 && a != b && b != c && a != c) {
        out[0] = a; out[1] = b; out[2] = c;
    } else {
        LOG("config: %s='%s' is not a permutation of 0,1,2; using 0,1,2", key, s.c_str());
    }
}
static AxisCfg readAxis(const wchar_t* ini, const char* sec, const char* name, float defLimit) {
    AxisCfg a;
    char k[64];
    snprintf(k, sizeof(k), "%sGain", name);   a.gain   = (float)readF(ini, sec, k, 1.0);
    snprintf(k, sizeof(k), "%sInvert", name); a.invert = readI(ini, sec, k, 0) != 0;
    snprintf(k, sizeof(k), "%sLimit", name);  a.limit  = (float)readF(ini, sec, k, defLimit);
    if (a.limit < 0) a.limit = 0;
    return a;
}

static WheelAxis readWheelAxis(const wchar_t* ini, const char* key, WheelAxis def, const char* defName) {
    std::string s = readStr(ini, "Wheel", key, defName), t;
    for (char ch : s) if (ch != ' ') t += (char)toupper((unsigned char)ch);
    static const struct { const char* n; WheelAxis a; } kAx[] = {
        {"X", WheelAxis::X}, {"Y", WheelAxis::Y}, {"Z", WheelAxis::Z}, {"RX", WheelAxis::RX},
        {"RY", WheelAxis::RY}, {"RZ", WheelAxis::RZ}, {"SLIDER0", WheelAxis::Slider0},
        {"SLIDER1", WheelAxis::Slider1}, {"NONE", WheelAxis::None}};
    for (auto& a : kAx) if (t == a.n) return a.a;
    LOG("config: unknown axis '%s' for %s; using %s", s.c_str(), key, defName);
    return def;
}
static const char* wheelAxisName(WheelAxis a) {
    static const char* n[] = {"X", "Y", "Z", "RX", "RY", "RZ", "SLIDER0", "SLIDER1", "NONE"};
    return n[(int)a];
}
static void readWheel(const wchar_t* ini, WheelConfig& w) {
    w.enabled        = readI(ini, "Wheel", "Enabled", 1) != 0;
    w.deviceMatch    = readStr(ini, "Wheel", "DeviceMatch", "G29");
    w.wheelRange     = readI(ini, "Wheel", "WheelRange", 900);
    if (w.wheelRange < 40 || w.wheelRange > 65535) {
        LOG("config: WheelRange=%d out of range 40..65535; using 900", w.wheelRange);
        w.wheelRange = 900;
    }
    w.steeringAxis   = readWheelAxis(ini, "SteeringAxis", WheelAxis::X, "X");
    w.steeringInvert = readI(ini, "Wheel", "SteeringInvert", 0) != 0;
    w.throttleAxis   = readWheelAxis(ini, "ThrottleAxis", WheelAxis::Y, "Y");
    w.brakeAxis      = readWheelAxis(ini, "BrakeAxis", WheelAxis::RZ, "RZ");
    w.clutchAxis     = readWheelAxis(ini, "ClutchAxis", WheelAxis::Slider0, "SLIDER0");
    w.pedalsInverted = readI(ini, "Wheel", "PedalsInverted", 1) != 0;
    w.pedalDeadzone  = (float)readF(ini, "Wheel", "PedalDeadzone", 0.02);
    if (!(w.pedalDeadzone >= 0.0f)) w.pedalDeadzone = 0.0f;
    if (w.pedalDeadzone > 0.9f) w.pedalDeadzone = 0.9f;
    w.buttonA    = readI(ini, "Wheel", "ButtonA", 0);
    w.buttonB    = readI(ini, "Wheel", "ButtonB", 2);
    w.buttonX    = readI(ini, "Wheel", "ButtonX", 1);
    w.buttonY    = readI(ini, "Wheel", "ButtonY", 3);
    w.buttonView = readI(ini, "Wheel", "ButtonView", 8);
    w.buttonMenu = readI(ini, "Wheel", "ButtonMenu", 9);
    w.buttonLB   = readI(ini, "Wheel", "ButtonLB", 5);
    w.buttonRB   = readI(ini, "Wheel", "ButtonRB", 4);
    w.hShifter   = readI(ini, "Wheel", "HShifter", 0) != 0;
    {
        static const int defGears[7] = {12, 13, 14, 15, 16, 17, -1};
        for (int i = 0; i < 7; ++i) w.gearButtons[i] = defGears[i];
        std::string g = readStr(ini, "Wheel", "GearButtons", "12,13,14,15,16,17,-1");
        const char* p = g.c_str();
        for (int i = 0; i < 7; ++i) {
            char* e = nullptr;
            long v = strtol(p, &e, 0);
            if (e == p) { LOG("config: GearButtons='%s' has fewer than 7 entries; rest use defaults", g.c_str()); break; }
            w.gearButtons[i] = (int)v;
            p = e;
            while (*p == ' ') ++p;
            if (*p == ',') ++p; else break;
        }
    }
    w.reverseButton = readI(ini, "Wheel", "ReverseButton", 18);
    std::string um = readStr(ini, "Wheel", "UserMode", "auto"), umt;
    for (char ch : um) if (ch != ' ') umt += (char)tolower((unsigned char)ch);
    if (umt == "none") w.userMode = WheelUserMode::None;
    else {
        if (umt != "auto") LOG("config: unknown UserMode '%s'; using auto", um.c_str());
        w.userMode = WheelUserMode::Auto;
    }
    w.forceRescan  = readI(ini, "Wheel", "ForceRescan", 1) != 0;
    w.vendorId     = readI(ini, "Wheel", "VendorId", 0x046D) & 0xFFFF;
    w.productId    = readI(ini, "Wheel", "ProductId", 0xC261) & 0xFFFF;
    w.configFlag14 = readI(ini, "Wheel", "ConfigFlag14", 1);
    w.testMode     = readI(ini, "Wheel", "TestMode", 0) != 0;
    LOG("config: wheel enabled %d match '%s' range %d axes steer %s%s thr %s brk %s clu %s pedalsInv %d dz %.2f "
        "hshifter %d user %s rescan %d id %04x:%04x flag14 %d test %d",
        w.enabled, w.deviceMatch.c_str(), w.wheelRange, wheelAxisName(w.steeringAxis),
        w.steeringInvert ? "(inv)" : "", wheelAxisName(w.throttleAxis), wheelAxisName(w.brakeAxis),
        wheelAxisName(w.clutchAxis), w.pedalsInverted, w.pedalDeadzone, w.hShifter,
        w.userMode == WheelUserMode::Auto ? "auto" : "none", w.forceRescan, w.vendorId, w.productId,
        w.configFlag14, w.testMode);
}

Config loadConfig(const std::wstring& iniPath) {
    if (GetFileAttributesW(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetPrivateProfileIntW(L"Input", L"ConfigVersion", 0, iniPath.c_str()) < kConfigVersion) {
        // Settings from an older version: keep a backup, start from the new defaults.
        std::wstring bak = iniPath + L".old.bak";
        MoveFileExW(iniPath.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING);
        LOG("config: settings from an older version saved as FH2HeadTracking.ini.old.bak, writing new defaults");
    }
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (FILE* f = _wfopen(iniPath.c_str(), L"w")) {
            fputs(kDefaultIni, f);
            fclose(f);
            LOG("config: wrote default settings file");
        }
    }
    const wchar_t* ini = iniPath.c_str();
    Config c;
    std::string src = readStr(ini, "Input", "Source", "auto");
    c.source = src == "udp" ? Source::Udp : src == "freetrack" ? Source::FreeTrack : Source::Auto;
    c.diagnostics     = readI(ini, "Input", "Diagnostics", 0) != 0;
    c.udpPort         = readI(ini, "Network", "UdpPort", 4242);
    c.bindAddress     = readStr(ini, "Network", "BindAddress", "127.0.0.1");
    c.enableOnStartup = readI(ini, "General", "EnableOnStartup", 1) != 0;
    c.cockpitOnly     = readI(ini, "General", "CockpitOnly", 1) != 0;
    c.staleTimeoutMs  = readI(ini, "General", "StaleTimeoutMs", 500);
    c.smoothing       = readF(ini, "General", "Smoothing", 0.0);
    c.toggleKey        = readI(ini, "Hotkeys", "ToggleKey", VK_END);
    c.recenterKey      = readI(ini, "Hotkeys", "RecenterKey", 0);
    c.chordToggleKey   = readI(ini, "Hotkeys", "ChordToggleKey", 'Y');
    c.chordRecenterKey = readI(ini, "Hotkeys", "ChordRecenterKey", 0);
    c.recenterButtons  = parseButtons(readStr(ini, "Hotkeys", "RecenterButtons", "L3+R3"));
    c.map.yaw   = readAxis(ini, "Rotation", "Yaw", 90.0f);
    c.map.pitch = readAxis(ini, "Rotation", "Pitch", 40.0f);
    c.map.roll  = readAxis(ini, "Rotation", "Roll", 20.0f);
    c.map.positionEnabled = readI(ini, "Position", "Enabled", 1) != 0;
    c.map.x = readAxis(ini, "Position", "X", 0.10f);
    c.map.y = readAxis(ini, "Position", "Y", 0.06f);
    c.map.z = readAxis(ini, "Position", "Z", 0.08f);
    readOrder(ini, "RotOrder", c.map.rotOrder);
    readOrder(ini, "PosOrder", c.map.posOrder);
    LOG("config: source %s diagnostics %d recenter buttons 0x%04x", src.c_str(), c.diagnostics, c.recenterButtons);
    LOG("config: port %d bind %s enabled %d cockpitOnly %d smoothing %.2f",
        c.udpPort, c.bindAddress.c_str(), c.enableOnStartup, c.cockpitOnly, c.smoothing);
    LOG("config: yaw g%.2f i%d l%.0f  pitch g%.2f i%d l%.0f  roll g%.2f i%d l%.0f",
        c.map.yaw.gain, c.map.yaw.invert, c.map.yaw.limit, c.map.pitch.gain, c.map.pitch.invert,
        c.map.pitch.limit, c.map.roll.gain, c.map.roll.invert, c.map.roll.limit);
    LOG("config: pos %d  x g%.2f i%d l%.2f  y g%.2f i%d l%.2f  z g%.2f i%d l%.2f",
        c.map.positionEnabled, c.map.x.gain, c.map.x.invert, c.map.x.limit, c.map.y.gain,
        c.map.y.invert, c.map.y.limit, c.map.z.gain, c.map.z.invert, c.map.z.limit);
    readWheel(ini, c.wheel);
    return c;
}

} // namespace fh2
