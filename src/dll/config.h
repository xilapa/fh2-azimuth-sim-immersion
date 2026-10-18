#pragma once
#include <windows.h>
#include <string>
#include "pose.h"

namespace fh2 {

enum class Source { Auto, Udp, FreeTrack };

enum class WheelAxis { X, Y, Z, RX, RY, RZ, Slider0, Slider1, None };
enum class WheelUserMode { Auto, None };

struct WheelConfig {
    bool        enabled = true;
    std::string deviceMatch = "G29";
    int         wheelRange = 900;
    WheelAxis   steeringAxis = WheelAxis::X, throttleAxis = WheelAxis::Y,
                brakeAxis = WheelAxis::RZ, clutchAxis = WheelAxis::Slider0;
    bool        steeringInvert = false;
    bool        pedalsInverted = true;
    float       pedalDeadzone = 0.02f;
    int         buttonA = 0, buttonB = 2, buttonX = 1, buttonY = 3;
    int         buttonView = 8, buttonMenu = 9, buttonLB = 5, buttonRB = 4;
    bool        hShifter = false;
    int         gearButtons[7] = {12, 13, 14, 15, 16, 17, -1};
    int         reverseButton = 18;
    WheelUserMode userMode = WheelUserMode::Auto;
    bool        forceRescan = true;
    int         vendorId = 0x046D, productId = 0xC261;
    int         configFlag14 = 1;
    bool        testMode = false;
    bool        ffbEnabled = true;
    float       ffbGain = 1.0f;
    bool        ffbInvert = false;
};

struct Config {
    // [Input]
    Source source = Source::Auto;
    bool   diagnostics = true;
    // [Network]
    int          udpPort = 4242;
    std::string  bindAddress = "127.0.0.1";
    // [General]
    bool enableOnStartup = true;
    bool cockpitOnly     = true;     // only DriverCam layers; false = also hood/bumper
    int  staleTimeoutMs  = 500;
    double smoothing     = 0.0;      // 0..1
    // [Hotkeys] Windows virtual key codes
    int toggleKey = VK_END, recenterKey = 0;
    int chordToggleKey = 'Y', chordRecenterKey = 0;     // used with Ctrl+Shift; 0 = none
    unsigned recenterButtons = 0x00C0;                  // XInput mask, default L3+R3
    // Pose mapping
    MapCfg map;
    // [Wheel]
    WheelConfig wheel;
};

// Loads the INI, writing a commented default file first if it does not exist.
Config loadConfig(const std::wstring& iniPath);

} // namespace fh2
