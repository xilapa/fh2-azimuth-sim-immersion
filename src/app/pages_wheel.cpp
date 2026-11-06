// SPDX-License-Identifier: MIT
// Tab 2: steering wheel, with an optional live DirectInput test.
#define DIRECTINPUT_VERSION 0x0800
#include "pages.h"
#include <dinput.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include "draw.h"
#include "ini.h"
#include "settings_tables.h"
#include "../common/wheel_enum.h"
#include "../common/ffb_calib.h"

namespace app {

namespace {
const int kIdTest = 9100, kIdPush = 9101, kIdDetect = 9102;
const int kIdForce = 9103;
const int kIdCalibrate = 9104;
const int kIdRecord = 9105;
const DWORD kPushMs = 500;

enum Role { kWheel, kPedals, kShifter, kHandbrake, kRoles };
const char* const kRoleKey[kRoles] = {"DeviceMatch", "PedalsDevice", "ShifterDevice", "HandbrakeDevice"};

Role roleOf(const char* key) {
    if (!strcmp(key, "ThrottleAxis") || !strcmp(key, "BrakeAxis") || !strcmp(key, "ClutchAxis")) return kPedals;
    if (!strcmp(key, "ReverseButton")) return kShifter;
    if (!strcmp(key, "HandbrakeButton") || !strcmp(key, "HandbrakeAxis")) return kHandbrake;
    return kWheel;
}

// The rows press-to-assign can fill: the button numbers and the axis choices.
bool assignable(const Setting& s) {
    return (s.type == SType::Int && strstr(s.key, "Button")) || (s.type == SType::Enum && strstr(s.values, "SLIDER0"));
}

const struct { const char* name; DWORD offset; } kAxes[8] = {
    {"X", DIJOFS_X}, {"Y", DIJOFS_Y}, {"Z", DIJOFS_Z}, {"RX", DIJOFS_RX}, {"RY", DIJOFS_RY}, {"RZ", DIJOFS_RZ},
    {"SLIDER0", DIJOFS_SLIDER(0)}, {"SLIDER1", DIJOFS_SLIDER(1)}};

LONG axisRaw(const DIJOYSTATE2& js, DWORD offset) { return *reinterpret_cast<const LONG*>(reinterpret_cast<const BYTE*>(&js) + offset); }
}

class WheelPage : public Page {
public:
    using Page::Page;
    ~WheelPage() override { StopPush(); StopTest(); }
    const wchar_t* Title() const override { return L"Wheel"; }

    void OnShow(bool visible) override { if (!visible) { StopPush(); StopTest(); } }

    void Tick() override {
        Page::Tick();
        if (pushDev_ && GetTickCount() - pushStart_ >= kPushMs) {
            StopPush();
            SetStatus(L"Did the wheel turn right? If it turned left, tick \"Invert force direction\".");
        }
        if (!dev_[kWheel]) return;
        for (int r = 0; r < kRoles; ++r) if (dev_[r]) readRole(r);
        if (!ok_[kWheel]) return;
        live_.active = true;
        live_.steer = axis(js_[kWheel], "SteeringAxis", false);
        if (steerInvert()) live_.steer = 1.0 - live_.steer;
        double dz = Num("Wheel", "PedalDeadzone", 0.02);
        const DIJOYSTATE2& pedals = source(kPedals);
        const bool havePedals = readable(kPedals);
        live_.throttle = havePedals && moved(0, pedals, "ThrottleAxis") ? pedal(axis(pedals, "ThrottleAxis", true), dz) : 0.0;
        live_.brake = havePedals && moved(1, pedals, "BrakeAxis") ? pedal(axis(pedals, "BrakeAxis", true), dz) : 0.0;
        live_.clutch = havePedals && moved(2, pedals, "ClutchAxis") ? pedal(axis(pedals, "ClutchAxis", true), dz) : 0.0;
        live_.pressed.clear();
        for (int r = 0; r < kRoles; ++r)   // all open devices: the shifter's numbers are needed for GearButtons
            if (dev_[r] && ok_[r])
                for (int i = 0; i < 128; ++i) if (js_[r].rgbButtons[i] & 0x80) live_.pressed.push_back(i);
        {
            double hb = 0;
            if (readable(kHandbrake)) {
                const DIJOYSTATE2& js = source(kHandbrake);
                Ctl* hbA = Find("Wheel", "HandbrakeAxis");
                if (hbA && GetValue(*hbA) != "NONE" && !GetValue(*hbA).empty() && moved(3, js, "HandbrakeAxis")) {
                    hb = axis(js, "HandbrakeAxis", true);
                    Ctl* inv = Find("Wheel", "HandbrakeAxisInvert");
                    if (inv && GetValue(*inv) == "1") hb = 1.0 - hb;
                }
                int hbBtn = (int)Num("Wheel", "HandbrakeButton", -1);
                if (hbBtn >= 0 && hbBtn < 128 && (js.rgbButtons[hbBtn] & 0x80)) hb = 1.0;
            }
            live_.handbrake = hb;
        }
        live_.lit.clear();
        {
            static const struct { const char* key; const char* part; } kBtns[] = {
                {"ButtonA", "btnA"}, {"ButtonB", "btnB"}, {"ButtonX", "btnX"}, {"ButtonY", "btnY"},
                {"ButtonView", "view"}, {"ButtonMenu", "menu"}, {"ButtonLB", "lb"}, {"ButtonRB", "rb"}};
            for (auto& b : kBtns) {
                int i = (int)Num("Wheel", b.key, -1);
                if (i >= 0 && i < 128 && (js_[kWheel].rgbButtons[i] & 0x80)) live_.lit.insert(b.part);
            }
            if (live_.handbrake >= Num("Wheel", "HandbrakeThreshold", 0.3)) live_.lit.insert("handbrake");
            live_.gear = 0;
            if (readable(kShifter)) {
                const DIJOYSTATE2& sh = source(kShifter);
                std::string g;
                if (Ctl* c = Find("Wheel", "GearButtons")) g = GetValue(*c);
                const char* p = g.c_str();
                for (int i = 0; i < 7 && *p; ++i) {          // "12,13,14,15,16,17,-1": gears 1..7
                    char* e = nullptr;
                    long v = strtol(p, &e, 0);
                    if (e == p) break;
                    if (v >= 0 && v < 128 && (sh.rgbButtons[v] & 0x80)) { live_.gear = i + 1; break; }
                    p = e;
                    while (*p == ' ' || *p == ',') ++p;
                }
                int r = (int)Num("Wheel", "ReverseButton", -1);
                if (live_.gear == 0 && r >= 0 && r < 128 && (sh.rgbButtons[r] & 0x80)) live_.gear = -1;
            }
        }
        Detect();
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }

    void PaintPicture(HDC dc, const RECT& rc) override {
        WheelPicState st;
        st.part = part_;
        st.live = live_;
        st.rangeDeg = Num("Wheel", "WheelRange", 900);
        auto val = [&](const char* key) { Ctl* c = Find("Wheel", key); return c ? GetValue(*c) : std::string(); };
        auto button = [&](const char* key) { std::string v = val(key); return Widen(v.empty() || v == "-1" ? std::string("none") : "button " + v); };
        st.label["steer"] = Widen("axis " + val("SteeringAxis") + (val("SteeringInvert") == "1" ? ", inverted" : ""));
        st.label["throttle"] = Widen(val("ThrottleAxis"));
        st.label["brake"] = Widen(val("BrakeAxis"));
        st.label["clutch"] = Widen(val("ClutchAxis"));
        st.label["btnA"] = button("ButtonA"); st.label["btnB"] = button("ButtonB");
        st.label["btnX"] = button("ButtonX"); st.label["btnY"] = button("ButtonY");
        st.label["view"] = button("ButtonView"); st.label["menu"] = button("ButtonMenu");
        st.label["lb"] = button("ButtonLB"); st.label["rb"] = button("ButtonRB");
        st.label["shifter"] = val("HShifter") == "1" ? L"on" : L"off";
        st.label["ffb"] = val("FFB") == "1" ? L"strength " + std::to_wstring(std::lround(Num("Wheel", "FFBGain", 1.0) * 100)) + L"%" : L"off";
        {   // handbrake: button and/or axis
            std::wstring t;
            std::string bv = val("HandbrakeButton"), av = val("HandbrakeAxis");
            if (!bv.empty() && bv != "-1") t = L"button " + Widen(bv);
            if (!av.empty() && av != "NONE") t += (t.empty() ? L"axis " : L" / axis ") + Widen(av);
            st.label["handbrake"] = t.empty() ? L"not set" : t;
        }
        hits_.clear();   // DrawWheelPicture fills it for the click handling
        DrawWheelPicture(dc, rc, st, (int)G.dpi, G.font, &hits_);
    }

protected:
    void Build() override {
        box_ = MakeStatic(host_, L"Test wheel");
        headers_.push_back(box_);   // bold, like the group titles below
        test_ = CreateWindowExW(0, L"BUTTON", L"Test input", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdTest, G.inst, nullptr);
        AddTip(test_, L"Opens the wheel selected by \"Wheel: name contains\", and any separate pedals, shifter and handbrake, with DirectInput "
                      L"(shared, in the background) and shows their axes and the numbers of the pressed buttons in the picture. "
                      L"Use it to find the numbers for the button settings. Stops when unchecked or when you leave this tab.");
        push_ = CreateWindowExW(0, L"BUTTON", L"Test force: push right", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdPush, G.inst, nullptr);
        AddTip(push_, L"Pushes the wheel to the right for half a second with the force set next to it. Hold the wheel lightly. "
                      L"If it turns left, tick \"Invert force direction\". It cannot run while the game holds the wheel.");
        forceLabel_ = MakeStatic(host_, L"Force (%):");
        force_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"15", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                                 0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdForce, G.inst, nullptr);
        AddTip(force_, L"Force of the push test, 1 - 100% of the wheel's maximum. Start low on a strong wheel.");
        detect_ = CreateWindowExW(0, L"BUTTON", L"Press to assign", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdDetect, G.inst, nullptr);
        AddTip(detect_, L"Turns the input test on. Click a button or axis row, then press that button or move that axis far: the row is filled in. "
                        L"Press Apply to keep it.");
        calib_ = CreateWindowExW(0, L"BUTTON", L"Calibrate force", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdCalibrate, G.inst, nullptr);
        AddTip(calib_, L"Sets Steering torque so the strongest steering force the game can send reaches about 85% of the wheel, "
                       L"with the Strength and Smallest force set here. The mod reads that force from the game when you are in a car; "
                       L"before that it uses the game's default. Press Apply to keep it.");
        rec_ = CreateWindowExW(0, L"BUTTON", L"Record drive", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdRecord, G.inst, nullptr);
        AddTip(rec_, L"Calibrates from your own driving: press it, start the game and drive a few minutes, come back and press "
                     L"\"Stop recording and calibrate\". Steering torque is then set so the strongest forces of that drive reach about 85% of the wheel.");
        recStarted_ = RecordingStarted();   // a recording left running stays on
        if (!recStarted_.empty()) SetWindowTextW(rec_, L"Stop recording and calibrate");
        BuildRows(kWheelSettings, sizeof(kWheelSettings) / sizeof(Setting));
        MakePicture();
    }
    void Layout(int w, int h) override {
        int lw = w * 54 / 100, gap = S(8), top = S(119), half = (lw - S(32)) / 2;
        SetWindowPos(box_, nullptr, S(8), S(4), lw - S(16), S(20), SWP_NOZORDER);
        SetWindowPos(test_, nullptr, S(14), S(28), half, S(24), SWP_NOZORDER);
        SetWindowPos(detect_, nullptr, S(22) + half, S(28), half, S(24), SWP_NOZORDER);
        SetWindowPos(push_, nullptr, S(14), S(57), half, S(26), SWP_NOZORDER);
        SetWindowPos(forceLabel_, nullptr, S(22) + half, S(61), S(70), S(22), SWP_NOZORDER);
        SetWindowPos(force_, nullptr, S(96) + half, S(58), S(48), S(24), SWP_NOZORDER);
        SetWindowPos(calib_, nullptr, S(14), S(86), half, S(26), SWP_NOZORDER);
        SetWindowPos(rec_, nullptr, S(22) + half, S(86), half, S(26), SWP_NOZORDER);
        SetWindowPos(scroller_, nullptr, 0, top, lw, h - top, SWP_NOZORDER);
        SetWindowPos(picture_, nullptr, lw + gap, 0, w - lw - gap, h, SWP_NOZORDER);
        LayoutRows();
    }
    void OnChanged(Ctl&) override { if (picture_) InvalidateRect(picture_, nullptr, FALSE); }
    void OnCommand(int id, int code, HWND ctl) override {
        if (id == kIdTest && code == BN_CLICKED) {
            if (SendMessageW(test_, BM_GETCHECK, 0, 0) == BST_CHECKED) StartTest(); else StopTest();
        } else if (id == kIdPush && code == BN_CLICKED) {
            StartPush();
        } else if (id == kIdCalibrate && code == BN_CLICKED) {
            Calibrate();
        } else if (id == kIdRecord && code == BN_CLICKED) {
            ToggleRecord();
        } else if (id == kIdDetect && code == BN_CLICKED) {
            if (SendMessageW(detect_, BM_GETCHECK, 0, 0) == BST_CHECKED && !dev_[kWheel]) {
                SendMessageW(test_, BM_SETCHECK, BST_CHECKED, 0);
                StartTest();
            }
            if (SendMessageW(detect_, BM_GETCHECK, 0, 0) == BST_CHECKED && assign_)
                SetStatus(L"Press to assign: press or move the control for \"" + Widen(assign_->s->label) + L"\".");
        } else if (code == CBN_DROPDOWN) {
            if (Ctl* c = FindByWnd(ctl)) if (c->s->type == SType::Device) FillDevices(*c);
        }
    }
    void OnPictureClick(int x, int y) override {
        static const struct { const char* part; const char* key; } kKeys[] = {
            {"steer", "SteeringAxis"}, {"throttle", "ThrottleAxis"}, {"brake", "BrakeAxis"}, {"clutch", "ClutchAxis"},
            {"pedals", "PedalsInverted"}, {"btnA", "ButtonA"}, {"btnB", "ButtonB"}, {"btnX", "ButtonX"}, {"btnY", "ButtonY"},
            {"view", "ButtonView"}, {"menu", "ButtonMenu"}, {"lb", "ButtonLB"}, {"rb", "ButtonRB"},
            {"shifter", "GearButtons"}, {"handbrake", "HandbrakeAxis"}, {"ffb", "FFB"}};
        POINT pt{x, y};
        const char* part = nullptr;
        for (auto& hh : hits_) if (PtInRect(&hh.r, pt)) part = hh.part.c_str();    // the last match is the smallest area
        if (!part) return;
        for (auto& k : kKeys)
            if (!strcmp(k.part, part))
                if (Ctl* c = Find("Wheel", k.key)) { EnsureVisible(c->main); SetFocus(c->main); }
    }

private:
    HWND box_ = nullptr, forceLabel_ = nullptr, force_ = nullptr;
    HWND test_ = nullptr, push_ = nullptr, detect_ = nullptr, calib_ = nullptr, rec_ = nullptr;
    std::wstring recStarted_;   // the Started stamp of the recording this app started; empty = not recording
    std::vector<PicHit> hits_;
    IDirectInput8W* di_ = nullptr;
    IDirectInputDevice8W* dev_[kRoles] = {};   // null for a role on the wheel, or whose device was not found
    bool missing_[kRoles] = {};
    bool ok_[kRoles] = {};
    DIJOYSTATE2 js_[kRoles] = {};
    WheelLive live_;
    // press to assign
    HWND assignFocus_ = nullptr;
    Ctl* assign_ = nullptr;
    BYTE prevButtons_[kRoles][128] = {};
    bool axisBaseSet_ = false;
    bool reported_[kRoles] = {};
    fh2::PedalRest rest_[4];   // throttle, brake, clutch, handbrake axis
    LONG axisBase_[8] = {};
    // force test
    IDirectInput8W* pushDi_ = nullptr;
    IDirectInputDevice8W* pushDev_ = nullptr;
    IDirectInputEffect* pushEffect_ = nullptr;
    DWORD pushStart_ = 0;

    bool steerInvert() { Ctl* c = Find("Wheel", "SteeringInvert"); return c && GetValue(*c) == "1"; }

    std::wstring deviceText(Role r) { Ctl* c = Find("Wheel", kRoleKey[r]); return c ? Widen(GetValue(*c)) : L""; }

    void readRole(int r) {
        HRESULT hr = dev_[r]->Poll();
        if (FAILED(hr)) { dev_[r]->Acquire(); dev_[r]->Poll(); }
        hr = dev_[r]->GetDeviceState(sizeof(js_[r]), &js_[r]);
        ok_[r] = SUCCEEDED(hr);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) dev_[r]->Acquire();
    }
    const DIJOYSTATE2& source(Role r) const { return dev_[r] ? js_[r] : js_[kWheel]; }
    bool readable(Role r) const { return !missing_[r] && (!dev_[r] || ok_[r]); }

    // Like the plugin: a pedal counts as released until it first moves (the G29 reads half pressed until then).
    bool moved(int i, const DIJOYSTATE2& js, const char* key) {
        Ctl* c = Find("Wheel", key);
        const std::string a = c ? GetValue(*c) : "";
        for (auto& x : kAxes) if (a == x.name) return rest_[i].moved(axisRaw(js, x.offset));
        return false;
    }

    // normalised 0..1 reading of the axis chosen in the combo box of `key`
    double axis(const DIJOYSTATE2& js, const char* key, bool pedalAxis) {
        Ctl* c = Find("Wheel", key);
        std::string a = c ? GetValue(*c) : "";
        for (auto& x : kAxes) if (a == x.name) return std::min(std::max(axisRaw(js, x.offset) / 65535.0, 0.0), 1.0);
        return pedalAxis ? 0.0 : 0.5;
    }
    double pedal(double raw, double deadzone) {
        Ctl* c = Find("Wheel", "PedalsInverted");
        double v = (c && GetValue(*c) == "1") ? 1.0 - raw : raw;
        if (v < deadzone) return 0.0;
        return std::min((v - deadzone) / std::max(1.0 - deadzone, 0.05), 1.0);
    }

    // The list of a device row: refilled when it opens, so a device plugged in later shows up.
    void FillDevices(Ctl& c) {
        std::wstring text = Widen(GetValue(c));
        bool was = loading_;
        loading_ = true;
        SendMessageW(c.main, CB_RESETCONTENT, 0, 0);
        SendMessageW(c.main, CB_ADDSTRING, 0, (LPARAM)L"");
        IDirectInput8W* di = nullptr;
        if (SUCCEEDED(DirectInput8Create(G.inst, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, nullptr)) && di) {
            for (auto& d : fh2::ListGameDevices(di)) SendMessageW(c.main, CB_ADDSTRING, 0, (LPARAM)d.name.c_str());
            di->Release();
        }
        SetWindowTextW(c.main, text.c_str());
        loading_ = was;
    }

    // Press to assign: a newly pressed button or an axis moved over 30% fills the row last clicked.
    void Detect() {
        HWND f = GetFocus();
        Ctl* c = FindByWnd(f);
        if (f != assignFocus_ && c && assignable(*c->s)) {
            assignFocus_ = f;
            assign_ = c;
            axisBaseSet_ = false;
            if (SendMessageW(detect_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                if (c->s->type == SType::Enum) SendMessageW(c->main, CB_SHOWDROPDOWN, FALSE, 0);   // a value set into an open list is lost
                SetStatus(L"Press to assign: press or move the control for \"" + Widen(c->s->label) + L"\".");
            }
        }
        if (SendMessageW(detect_, BM_GETCHECK, 0, 0) == BST_CHECKED && assign_) {
            Role r = roleOf(assign_->s->key);
            if (readable(r)) {
                const DIJOYSTATE2& js = source(r);
                std::string value;
                if (assign_->s->type == SType::Int) {
                    for (int i = 0; i < 128 && value.empty(); ++i)
                        if ((js.rgbButtons[i] & 0x80) && !(prevButtons_[r][i] & 0x80)) value = std::to_string(i);
                } else {
                    LONG v[8];
                    for (int i = 0; i < 8; ++i) v[i] = axisRaw(js, kAxes[i].offset);
                    // Until its first report a device's axes sit at a made-up value, and that report moves them all at once.
                    if (axisBaseSet_ && !reported_[r] && !std::equal(v, v + 8, axisBase_)) { reported_[r] = true; axisBaseSet_ = false; }
                    if (!axisBaseSet_) { std::copy(v, v + 8, axisBase_); axisBaseSet_ = true; }
                    for (int i = 0; i < 8 && value.empty(); ++i)
                        if (labs(v[i] - axisBase_[i]) > 65535 * 30 / 100) value = kAxes[i].name;
                }
                if (!value.empty()) {
                    SetValue(*assign_, value);
                    SetStatus(L"Set " + Widen(assign_->s->label) + L" to " + Widen(value) + L". Press Apply to keep it.");
                    assign_ = nullptr;   // the next press needs another row
                }
            }
        }
        for (int r = 0; r < kRoles; ++r) memcpy(prevButtons_[r], source((Role)r).rgbButtons, 128);
    }

    void StartTest() {
        CloseTest();
        live_ = WheelLive();
        live_.device = L"searching...";
        HRESULT hr = DirectInput8Create(G.inst, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di_, nullptr);
        if (FAILED(hr) || !di_) { Fail(L"DirectInput is not available"); return; }
        const std::vector<fh2::FoundDevice> devs = fh2::ListGameDevices(di_);
        const std::vector<fh2::WheelCandidate> cands = fh2::WheelCandidates(devs);
        // same rules as the plugin (wheel_pick.h)
        const int pick = fh2::PickWheel(cands, deviceText(kWheel));
        if (pick < 0) { Fail(devs.empty() ? L"no game controller found" : L"no wheel found (the plugin would not use one either)"); return; }
        if (!open(kWheel, devs[pick].guid)) { Fail(L"could not open the device"); return; }
        live_.active = true;
        live_.device = devs[pick].name;
        std::wstring status = L"Testing \"" + devs[pick].name + L"\". Move the wheel and press buttons.";
        for (int r = kPedals; r < kRoles; ++r) {
            std::wstring text = deviceText((Role)r);
            if (text.empty()) continue;
            int i = fh2::FindDeviceByName(cands, text);
            if (i == pick) continue;
            if (i < 0 || !open((Role)r, devs[i].guid)) {
                missing_[r] = true;
                status += L" \"" + text + L"\" not found: it counts as released.";
            }
        }
        SetStatus(status);
    }
    bool open(Role r, const GUID& guid) {
        if (FAILED(di_->CreateDevice(guid, &dev_[r], nullptr)) || !dev_[r]) { dev_[r] = nullptr; return false; }
        dev_[r]->SetDataFormat(&c_dfDIJoystick2);
        dev_[r]->SetCooperativeLevel(G.main, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
        dev_[r]->Acquire();
        return true;
    }
    void Fail(const std::wstring& why) {
        StopTest();
        live_ = WheelLive();
        live_.active = true;
        live_.device = why;
        SetStatus(L"Test input: " + why + L".");
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
        live_.active = false;
    }
    void CloseTest() {
        for (int r = 0; r < kRoles; ++r) {
            if (dev_[r]) { dev_[r]->Unacquire(); dev_[r]->Release(); dev_[r] = nullptr; }
            missing_[r] = false;
            ok_[r] = false;
            reported_[r] = false;
        }
        for (auto& x : rest_) x = fh2::PedalRest();
        if (di_) { di_->Release(); di_ = nullptr; }
        live_ = WheelLive();
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }
    void StopTest() {
        CloseTest();
        assign_ = nullptr;
        assignFocus_ = nullptr;
        if (test_) SendMessageW(test_, BM_SETCHECK, BST_UNCHECKED, 0);
        if (detect_) SendMessageW(detect_, BM_SETCHECK, BST_UNCHECKED, 0);
    }

    // Sets Steering torque so the game's strongest steering force reaches about 85% of the wheel.
    void Calibrate() {
        const std::wstring path = ExeDir() + L"\\FH2SimImmersion_ffb.ini";
        wchar_t buf[64];
        GetPrivateProfileStringW(L"Game", L"TorqueMax", L"0", buf, 64, path.c_str());
        float tmax = (float)_wtof(buf);
        const bool fromGame = tmax > 0.0f && tmax <= 1.0f;
        if (!fromGame) tmax = fh2::kDefaultGameTorqueMax;
        wchar_t what[160];
        swprintf(what, 160, L"the game's strongest steering force (%.3f, %ls)", tmax,
                 fromGame ? L"read from the game" : L"the game's default; drive once with the mod to read yours");
        CalibrateTo(tmax, what);
    }

    // Sets Steering torque so the force torque reaches about 85% of the wheel; what describes it in the status line.
    void CalibrateTo(float torque, const wchar_t* what) {
        Ctl* c = Find("Wheel", "FFBTorqueGain");
        const float gain = (float)Num("Wheel", "FFBGain", 1.0), minF = (float)Num("Wheel", "FFBMinForce", 0.05);
        if (!c || !(gain > 0.0f)) {
            SetStatus(L"Calibrate: Strength is 0%. Raise it first.");
            return;
        }
        const float now = (float)Num("Wheel", "FFBTorqueGain", 1.0);
        const float set = fh2::CalibratedTorqueGain(torque, gain, minF, 0.85f);
        char v[16];
        snprintf(v, sizeof(v), "%.1f", set);
        SetValue(*c, v);
        wchar_t msg[500];
        swprintf(msg, 500, L"Calibrate: %ls would reach %.0f%% of the wheel with Steering torque at %.0f%%. "
                           L"Set to %.0f%% so it reaches about 85%%%ls. Press Apply to keep it.",
                 what, fh2::WheelShare(torque, now, gain, minF) * 100.0f, now * 100.0f, set * 100.0f,
                 set >= 5.0f ? L" (the most it can be)" : L"");
        SetStatus(msg);
    }

    std::wstring RecordPath() { return ExeDir() + L"\\FH2SimImmersion_record.ini"; }

    // The Started stamp of a recording in progress (the plugin follows the file), empty when none.
    std::wstring RecordingStarted() {
        const std::wstring p = RecordPath();
        if (GetPrivateProfileIntW(L"Record", L"Active", 0, p.c_str()) == 0) return L"";
        wchar_t b[64];
        GetPrivateProfileStringW(L"Record", L"Started", L"", b, 64, p.c_str());
        return b;
    }

    // "Record drive": starts a recording, or stops it and calibrates from what the plugin recorded.
    void ToggleRecord() {
        const std::wstring p = RecordPath();
        if (recStarted_.empty()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t when[32];
            swprintf(when, 32, L"%04d-%02d-%02d %02d:%02d:%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay,
                     (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
            if (!WritePrivateProfileStringW(L"Record", L"Started", when, p.c_str()) ||
                !WritePrivateProfileStringW(L"Record", L"Active", L"1", p.c_str())) {
                SetStatus(L"Record drive: could not write " + p + L".");
                return;
            }
            recStarted_ = when;
            SetWindowTextW(rec_, L"Stop recording and calibrate");
            SetStatus(L"Recording: start the game (or keep it running) and drive a few minutes, then come back and press "
                      L"\"Stop recording and calibrate\".");
            return;
        }
        WritePrivateProfileStringW(L"Record", L"Active", L"0", p.c_str());
        SetWindowTextW(rec_, L"Record drive");
        const std::wstring started = recStarted_;
        recStarted_.clear();
        const std::wstring ffb = ExeDir() + L"\\FH2SimImmersion_ffb.ini";
        wchar_t got[64], t[32];
        GetPrivateProfileStringW(L"Recording", L"Started", L"", got, 64, ffb.c_str());
        const int secs = GetPrivateProfileIntW(L"Recording", L"Seconds", 0, ffb.c_str());
        GetPrivateProfileStringW(L"Recording", L"Torque99", L"0", t, 32, ffb.c_str());
        const float t99 = (float)_wtof(t);
        if (started != got || secs < 20 || !(t99 > 0.0f)) {
            SetStatus(L"Recording stopped: nothing usable was recorded. Record again and drive at least 20 s with force feedback on "
                      L"(the game must run with this mod while recording).");
            return;
        }
        wchar_t what[160];
        swprintf(what, 160, L"the strongest forces of your recorded drive (%.3f over %d %ls)", t99,
                 secs < 120 ? secs : secs / 60, secs < 120 ? L"s" : L"min");
        CalibrateTo(t99, what);
    }

    void StartPush() {
        StopPush();
        HRESULT hr = DirectInput8Create(G.inst, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&pushDi_, nullptr);
        if (FAILED(hr) || !pushDi_) { PushFail(L"DirectInput is not available."); return; }
        const std::vector<fh2::FoundDevice> devs = fh2::ListGameDevices(pushDi_);
        const int pick = fh2::PickWheel(fh2::WheelCandidates(devs), deviceText(kWheel));
        if (pick < 0) { PushFail(L"no wheel found."); return; }
        if (!devs[pick].ffb) { PushFail(L"\"" + devs[pick].name + L"\" has no force feedback."); return; }
        if (FAILED(pushDi_->CreateDevice(devs[pick].guid, &pushDev_, nullptr)) || !pushDev_) { pushDev_ = nullptr; PushFail(L"could not open the wheel."); return; }
        pushDev_->SetDataFormat(&c_dfDIJoystick2);
        pushDev_->SetCooperativeLevel(G.main, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
        if (FAILED(pushDev_->Acquire())) { PushFail(L"the wheel is busy. Close the game and try again."); return; }
        fh2::AutocenterOff(pushDev_);
        Ctl* steer = Find("Wheel", "SteeringAxis");
        const std::string steerAxis = steer ? GetValue(*steer) : "";
        DWORD steerOffset = DIJOFS_X;
        for (auto& x : kAxes) if (steerAxis == x.name) steerOffset = x.offset;
        DWORD axes[1] = {fh2::ForceAxisOf(pushDev_, steerOffset)};
        LONG dir[1] = {1};
        wchar_t buf[16] = L"";
        GetWindowTextW(force_, buf, 16);
        long pct = wcstol(buf, nullptr, 10);
        pct = pct < 1 ? 1 : pct > 100 ? 100 : pct;
        const LONG magnitude = (LONG)(pct * 100);
        Ctl* inv = Find("Wheel", "FFBInvert");
        DICONSTANTFORCE cf{};
        cf.lMagnitude = (inv && GetValue(*inv) == "1") ? magnitude : -magnitude;   // same sign as the plugin (FfbDeviceMagnitude)
        DIEFFECT eff{};
        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = kPushMs * 1000;   // microseconds: it stops by itself even if this window hangs
        eff.dwGain = 10000;
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.cAxes = 1;
        eff.rgdwAxes = axes;
        eff.rglDirection = dir;
        eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        eff.lpvTypeSpecificParams = &cf;
        if (FAILED(pushDev_->CreateEffect(GUID_ConstantForce, &eff, &pushEffect_, nullptr)) || !pushEffect_ || FAILED(pushEffect_->Start(1, 0))) {
            PushFail(L"the wheel refused the force.");
            return;
        }
        pushStart_ = GetTickCount();
        SetStatus(L"Pushing \"" + devs[pick].name + L"\" to the right at " + std::to_wstring(pct) + L"%...");
    }
    void PushFail(const std::wstring& why) {
        StopPush();
        SetStatus(L"Test force: " + why);
    }
    void StopPush() {
        if (pushEffect_) { pushEffect_->Stop(); pushEffect_->Release(); pushEffect_ = nullptr; }
        if (pushDev_) { pushDev_->Unacquire(); pushDev_->Release(); pushDev_ = nullptr; }
        if (pushDi_) { pushDi_->Release(); pushDi_ = nullptr; }
    }
};

Page* NewWheelPage(int index) { return new WheelPage(index); }

} // namespace app
