// SPDX-License-Identifier: MIT
// GDI pictures of the settings window (no image files): head axes and steering wheel parts.
#pragma once
#include <windows.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace app {

struct HeadPicState {
    std::string part;                                     // yaw pitch roll x y z, rot / pos = all of them
    double yawLim = 90, pitchLim = 40, rollLim = 20;      // degrees
    double xLim = 0.10, yLim = 0.06, zLim = 0.08;         // metres
    bool live = false;                                    // live preview: draw the pose below
    double yawLeft = 0, pitchDown = 0, rollLeft = 0;      // degrees after gains and limits; positive = left / down / left
    double x = 0, y = 0, z = 0;                           // metres after gains and limits; positive = right / up / back
    std::wstring liveText;                                // one line of values, or why there are none
};
void DrawHeadPicture(HDC dc, const RECT& rc, const HeadPicState& st, int dpi, HFONT font);

struct WheelLive {
    bool active = false;
    std::wstring device;               // device name or a message
    double steer = 0.5, throttle = 0, brake = 0, clutch = 0, handbrake = 0;   // 0..1
    std::vector<int> pressed;
    std::set<std::string> lit;         // picture parts whose mapped button or pedal is pressed now (btnA ... rb, handbrake)
    int gear = 0;                      // shifter position now: -1 reverse, 0 neutral, 1..7
};
struct WheelPicState {
    std::string part;                  // steer throttle brake clutch pedals btnA btnB btnX btnY view menu lb rb shifter handbrake ffb
    std::map<std::string, std::wstring> label;   // text shown for each part: the value of its setting
    double rangeDeg = 900;
    WheelLive live;
};
struct PicHit { RECT r; std::string part; };          // a clickable area of the picture
void DrawWheelPicture(HDC dc, const RECT& rc, const WheelPicState& st, int dpi, HFONT font, std::vector<PicHit>* hits);

} // namespace app
