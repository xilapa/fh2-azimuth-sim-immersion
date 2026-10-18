#define DIRECTINPUT_VERSION 0x0800
#include "wheel_device.h"
#include <dinput.h>
#include <cwctype>
#include <string>
#include <vector>
#include "log.h"

namespace fh2 {

namespace {

WheelConfig        g_cfg;
SRWLOCK            g_lock = SRWLOCK_INIT;
WheelInput         g_snap;
IDirectInputDevice8W* g_dev = nullptr;
IDirectInput8W*       g_di = nullptr;
bool               g_started = false;

std::string utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "?";
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(n - 1);
    return s;
}

std::wstring lower(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

struct DevInfo {
    GUID         instance;
    std::wstring name;
    DWORD        type;
    DWORD        data1;
};

BOOL CALLBACK enumCb(const DIDEVICEINSTANCEW* inst, void* user) {
    auto* v = static_cast<std::vector<DevInfo>*>(user);
    v->push_back({inst->guidInstance, inst->tszProductName, inst->dwDevType, inst->guidProduct.Data1});
    return DIENUM_CONTINUE;
}

HWND findOwnWindow() {
    struct Ctx { DWORD pid; HWND hwnd; } ctx{GetCurrentProcessId(), nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == c->pid && IsWindowVisible(h)) { c->hwnd = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

LONG axisValue(const DIJOYSTATE2& s, WheelAxis a, bool& present) {
    present = true;
    switch (a) {
        case WheelAxis::X: return s.lX;
        case WheelAxis::Y: return s.lY;
        case WheelAxis::Z: return s.lZ;
        case WheelAxis::RX: return s.lRx;
        case WheelAxis::RY: return s.lRy;
        case WheelAxis::RZ: return s.lRz;
        case WheelAxis::Slider0: return s.rglSlider[0];
        case WheelAxis::Slider1: return s.rglSlider[1];
        default: present = false; return 0;
    }
}

float pedal(const DIJOYSTATE2& s, WheelAxis a) {
    bool present;
    LONG raw = axisValue(s, a, present);
    if (!present) return 0.0f;
    double p = raw / 65535.0;
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    if (g_cfg.pedalsInverted) p = 1.0 - p;
    double dz = g_cfg.pedalDeadzone;
    p = (p - dz) / (1.0 - dz);
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    return (float)p;
}

bool btn(const DIJOYSTATE2& s, int idx) {
    return idx >= 0 && idx < 128 && (s.rgbButtons[idx] & 0x80);
}

void mapState(const DIJOYSTATE2& s, WheelInput& o) {
    bool present;
    LONG raw = axisValue(s, g_cfg.steeringAxis, present);
    double st = present ? (raw - 32767.5) / 32767.5 : 0.0;
    st = st > 1 ? 1 : st < -1 ? -1 : st;
    if (g_cfg.steeringInvert) st = -st;
    o.steering = (float)st;
    o.throttle = pedal(s, g_cfg.throttleAxis);
    o.brake    = pedal(s, g_cfg.brakeAxis);
    o.clutch   = pedal(s, g_cfg.clutchAxis);
    o.a = btn(s, g_cfg.buttonA); o.b = btn(s, g_cfg.buttonB);
    o.x = btn(s, g_cfg.buttonX); o.y = btn(s, g_cfg.buttonY);
    o.view = btn(s, g_cfg.buttonView); o.menu = btn(s, g_cfg.buttonMenu);
    o.lb = btn(s, g_cfg.buttonLB); o.rb = btn(s, g_cfg.buttonRB);
    o.up = o.down = o.left = o.right = false;
    DWORD pov = s.rgdwPOV[0];
    if ((pov & 0xFFFF) != 0xFFFF) {
        pov %= 36000;
        o.up    = pov >= 31500 || pov <= 4500;
        o.right = pov >= 4500 && pov <= 13500;
        o.down  = pov >= 13500 && pov <= 22500;
        o.left  = pov >= 22500 && pov <= 31500;
    }
    o.raw[0] = s.lX; o.raw[1] = s.lY; o.raw[2] = s.lZ;
    o.raw[3] = s.lRx; o.raw[4] = s.lRy; o.raw[5] = s.lRz;
    o.raw[6] = s.rglSlider[0]; o.raw[7] = s.rglSlider[1];
    for (int i = 0; i < 4; ++i) o.rawButtons[i] = 0;
    for (int i = 0; i < 128; ++i)
        if (s.rgbButtons[i] & 0x80) o.rawButtons[i / 32] |= (1u << (i % 32));
    o.gear = 0;
    if (g_cfg.hShifter) {
        for (int i = 0; i < 7; ++i)
            if (btn(s, g_cfg.gearButtons[i])) { o.gear = i + 1; break; }
        if (o.gear == 0 && btn(s, g_cfg.reverseButton)) o.gear = -1;
    }
}

uint64_t qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)t.QuadPart; }

void publish(const WheelInput& in) {
    AcquireSRWLockExclusive(&g_lock);
    g_snap = in;
    ReleaseSRWLockExclusive(&g_lock);
}

DWORD WINAPI testThread(void*) {
    for (;;) {
        WheelInput in;
        in.steering = 0.25f; in.throttle = 0.5f; in.a = true;
        in.timestamp = qpc();
        in.valid = true;
        publish(in);
        Sleep(2);
    }
}

DWORD WINAPI pollThread(void*) {
    // Wait for a visible top-level window of this process.
    HWND hwnd = nullptr;
    while (!(hwnd = findOwnWindow())) Sleep(200);
    HRESULT hr = g_dev->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (FAILED(hr)) LOG("wheel: SetCooperativeLevel failed 0x%08lx", (unsigned long)hr);
    DIPROPRANGE rng{};
    rng.diph.dwSize = sizeof(rng);
    rng.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    rng.diph.dwObj = 0;
    rng.diph.dwHow = DIPH_DEVICE;
    rng.lMin = 0; rng.lMax = 65535;
    g_dev->SetProperty(DIPROP_RANGE, &rng.diph);   // failures ignored
    g_dev->Acquire();
    LOG("wheel: polling started");

    bool acquired = false;
    for (;;) {
        hr = g_dev->Poll();
        DIJOYSTATE2 st{};
        if (SUCCEEDED(hr)) hr = g_dev->GetDeviceState(sizeof(st), &st);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            acquired = false;
            g_dev->Acquire();
            AcquireSRWLockExclusive(&g_lock);
            g_snap.valid = false;
            ReleaseSRWLockExclusive(&g_lock);
            Sleep(2);
            continue;
        }
        if (SUCCEEDED(hr)) {
            if (!acquired) { acquired = true; LOG("wheel: device acquired"); }
            WheelInput in;
            mapState(st, in);
            in.timestamp = qpc();
            in.valid = true;
            publish(in);
        }
        Sleep(2);
    }
}

} // namespace

bool WheelDevice_Start(const WheelConfig& cfg) {
    if (g_started) return true;
    g_cfg = cfg;
    if (cfg.testMode) {
        LOG("wheel: TestMode, synthetic input");
        if (HANDLE t = CreateThread(nullptr, 0, testThread, nullptr, 0, nullptr)) CloseHandle(t);
        g_started = true;
        return true;
    }
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                    reinterpret_cast<void**>(&g_di), nullptr);
    if (FAILED(hr) || !g_di) { LOG("wheel: DirectInput8Create failed 0x%08lx", (unsigned long)hr); return false; }
    std::vector<DevInfo> devs;
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, &devs, DIEDFL_ATTACHEDONLY);
    for (auto& d : devs)
        LOG("wheel: device \"%s\" vid %04x pid %04x type 0x%x", utf8(d.name.c_str()).c_str(),
            (unsigned)(d.data1 & 0xFFFF), (unsigned)(d.data1 >> 16), (unsigned)d.type);

    int pick = -1;
    if (!cfg.deviceMatch.empty()) {
        int n = MultiByteToWideChar(CP_ACP, 0, cfg.deviceMatch.c_str(), -1, nullptr, 0);
        std::wstring m(n > 0 ? n : 1, L'\0');
        if (n > 0) MultiByteToWideChar(CP_ACP, 0, cfg.deviceMatch.c_str(), -1, &m[0], n);
        m.resize(wcslen(m.c_str()));
        m = lower(m);
        for (size_t i = 0; i < devs.size() && pick < 0; ++i)
            if (lower(devs[i].name).find(m) != std::wstring::npos) pick = (int)i;
    }
    if (pick < 0)
        for (size_t i = 0; i < devs.size() && pick < 0; ++i)
            if (GET_DIDEVICE_TYPE(devs[i].type) == DI8DEVTYPE_DRIVING) pick = (int)i;
    if (pick < 0) {
        LOG("wheel: no wheel found (DeviceMatch=%s)", cfg.deviceMatch.c_str());
        return false;
    }
    LOG("wheel: using \"%s\"", utf8(devs[pick].name.c_str()).c_str());
    hr = g_di->CreateDevice(devs[pick].instance, &g_dev, nullptr);
    if (FAILED(hr) || !g_dev) { LOG("wheel: CreateDevice failed 0x%08lx", (unsigned long)hr); return false; }
    hr = g_dev->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) { LOG("wheel: SetDataFormat failed 0x%08lx", (unsigned long)hr); g_dev->Release(); g_dev = nullptr; return false; }
    if (HANDLE t = CreateThread(nullptr, 0, pollThread, nullptr, 0, nullptr)) CloseHandle(t);
    g_started = true;
    return true;
}

bool WheelDevice_Read(WheelInput& out) {
    AcquireSRWLockShared(&g_lock);
    out = g_snap;
    ReleaseSRWLockShared(&g_lock);
    if (out.valid) {
        static uint64_t freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (uint64_t)f.QuadPart; }();
        if (qpc() - out.timestamp > freq / 2) out.valid = false;
    }
    return out.valid;
}

} // namespace fh2
