// SPDX-License-Identifier: MIT
// DirectInput steering wheel reader and force feedback output.
#define DIRECTINPUT_VERSION 0x0800
#include "wheel_device.h"
#include <dinput.h>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>
#include "log.h"
#include "steer_lock.h"
#include "wheel_com.h"
#include "wheel_ffb.h"
#include "../common/wheel_enum.h"

namespace fh2 {

namespace {

WheelConfig        g_cfg;
SRWLOCK            g_lock = SRWLOCK_INIT;
WheelInput         g_snap;
IDirectInputDevice8W* g_dev = nullptr;
IDirectInput8W*       g_di = nullptr;
bool               g_started = false;
bool               g_diag = false;
bool               g_ffbCapable = false;
FfbStatus          g_ffbStatus;

GUID               g_wheelGuid{};
DWORD              g_forceAxis = DIJOFS_X;

// A separate USB device for the pedals, the shifter or the handbrake, read in the poll thread.
struct ExtraDev {
    IDirectInputDevice8W* dev = nullptr;
    GUID guid{};
    DIJOYSTATE2 st{};
    bool ok = false;
    double nextAcquireMs = 0;
};
constexpr int kOnWheel = -1, kNotFound = -2;   // else an index in g_extras
std::vector<ExtraDev> g_extras;
int g_pedalsFrom = kOnWheel, g_shifterFrom = kOnWheel, g_handbrakeFrom = kOnWheel;

std::string utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "?";
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(n - 1);
    return s;
}

std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n - 1);
    return w;
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

DWORD axisOffset(WheelAxis a) {
    switch (a) {
        case WheelAxis::X: return DIJOFS_X;
        case WheelAxis::Y: return DIJOFS_Y;
        case WheelAxis::Z: return DIJOFS_Z;
        case WheelAxis::RX: return DIJOFS_RX;
        case WheelAxis::RY: return DIJOFS_RY;
        case WheelAxis::RZ: return DIJOFS_RZ;
        case WheelAxis::Slider0: return DIJOFS_SLIDER(0);
        case WheelAxis::Slider1: return DIJOFS_SLIDER(1);
        default: return DIJOFS_X;
    }
}

float pedal(const WheelConfig& cfg, const DIJOYSTATE2* s, WheelAxis a, PedalRest& rest) {
    if (!s) return 0.0f;     // its device is missing or not readable: released
    bool present;
    LONG raw = axisValue(*s, a, present);
    if (!present || !rest.moved(raw)) return 0.0f;
    double p = raw / 65535.0;
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    if (cfg.pedalsInverted) p = 1.0 - p;
    double dz = cfg.pedalDeadzone;
    p = (p - dz) / (1.0 - dz);
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    return (float)p;
}

bool btn(const DIJOYSTATE2& s, int idx) {
    return idx >= 0 && idx < 128 && (s.rgbButtons[idx] & 0x80);
}

// pedals, shifter and handbrake: the state of their device; nullptr = not readable, counts as released.
// rest: throttle, brake, clutch, handbrake axis.
void mapState(const WheelConfig& cfg, const DIJOYSTATE2& s, const DIJOYSTATE2* pedals, const DIJOYSTATE2* shifter,
              const DIJOYSTATE2* handbrake, PedalRest* rest, WheelInput& o) {
    bool present;
    LONG raw = axisValue(s, cfg.steeringAxis, present);
    double st = present ? (raw - 32767.5) / 32767.5 : 0.0;
    st = st > 1 ? 1 : st < -1 ? -1 : st;
    if (cfg.steeringInvert) st = -st;
    o.steering = (float)st;
    o.throttle = pedal(cfg, pedals, cfg.throttleAxis, rest[0]);
    o.brake    = pedal(cfg, pedals, cfg.brakeAxis, rest[1]);
    o.clutch   = pedal(cfg, pedals, cfg.clutchAxis, rest[2]);
    o.a = btn(s, cfg.buttonA); o.b = btn(s, cfg.buttonB);
    o.x = btn(s, cfg.buttonX); o.y = btn(s, cfg.buttonY);
    o.view = btn(s, cfg.buttonView); o.menu = btn(s, cfg.buttonMenu);
    o.lb = btn(s, cfg.buttonLB); o.rb = btn(s, cfg.buttonRB);
    {   // real handbrake -> the configured game button
        bool hbPresent = false;
        double h = 0.0;
        if (handbrake) {
            LONG hraw = axisValue(*handbrake, cfg.handbrakeAxis, hbPresent);
            if (hbPresent && rest[3].moved(hraw)) {
                h = hraw / 65535.0;
                h = h < 0 ? 0 : h > 1 ? 1 : h;
                if (cfg.handbrakeAxisInvert) h = 1.0 - h;
            }
        }
        o.handbrakeAxis = (float)h;
        o.handbrake = (handbrake && btn(*handbrake, cfg.handbrakeButton)) || (hbPresent && h >= cfg.handbrakeThreshold);
        if (o.handbrake) {
            switch (cfg.handbrakeGameButton) {
                case 'A': o.a = true; break;
                case 'X': o.x = true; break;
                case 'Y': o.y = true; break;
                case 'L': o.lb = true; break;
                case 'R': o.rb = true; break;
                default:  o.b = true; break;
            }
        }
    }
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
    if (cfg.hShifter && shifter) {
        for (int i = 0; i < 7; ++i)
            if (btn(*shifter, cfg.gearButtons[i])) { o.gear = i + 1; break; }
        if (o.gear == 0 && btn(*shifter, cfg.reverseButton)) o.gear = -1;
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

double qpcMs() {
    static double freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }();
    return (double)qpc() * 1000.0 / freq;
}

void setFullRange(IDirectInputDevice8W* dev) {
    DIPROPRANGE rng{};
    rng.diph.dwSize = sizeof(rng);
    rng.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    rng.diph.dwObj = 0;
    rng.diph.dwHow = DIPH_DEVICE;
    rng.lMin = 0; rng.lMax = 65535;
    dev->SetProperty(DIPROP_RANGE, &rng.diph);   // failures ignored
}

void setupExtras(HWND hwnd) {
    for (auto& e : g_extras) {
        e.dev->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        setFullRange(e.dev);
        e.dev->Acquire();
    }
}

void pollExtras() {
    for (auto& e : g_extras) {
        HRESULT hr = e.dev->Poll();
        if (SUCCEEDED(hr)) hr = e.dev->GetDeviceState(sizeof(e.st), &e.st);
        bool was = e.ok;
        e.ok = SUCCEEDED(hr);
        if (was && !e.ok) LOG("wheel: lost a separate device (0x%08lx); it counts as released", (unsigned long)hr);
        if (!e.ok && qpcMs() >= e.nextAcquireMs) {   // not on every loop: this device must not slow the wheel
            e.nextAcquireMs = qpcMs() + 500.0;
            e.dev->Acquire();
        }
    }
}

const DIJOYSTATE2* sourceState(int from, const DIJOYSTATE2& wheel) {
    if (from == kOnWheel) return &wheel;
    if (from == kNotFound || !g_extras[from].ok) return nullptr;
    return &g_extras[from].st;
}

// Opens the device of one setting, shared if two settings name the same one.
int openExtra(const std::vector<FoundDevice>& devs, int wheelIdx, const std::string& match, const char* what) {
    if (match.empty()) return kOnWheel;
    const int i = FindDeviceByName(WheelCandidates(devs), wide(match));
    if (i < 0) {
        LOG("wheel: %s device \"%s\" not found; plug it in before starting the game. It counts as released.", what, match.c_str());
        return kNotFound;
    }
    if (i == wheelIdx) return kOnWheel;
    LOG("wheel: %s on \"%s\"", what, utf8(devs[i].name.c_str()).c_str());
    for (size_t k = 0; k < g_extras.size(); ++k)
        if (IsEqualGUID(g_extras[k].guid, devs[i].guid)) return (int)k;
    IDirectInputDevice8W* d = nullptr;
    HRESULT hr = g_di->CreateDevice(devs[i].guid, &d, nullptr);
    if (SUCCEEDED(hr) && d) hr = d->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr) || !d) {
        LOG("wheel: could not open the %s device 0x%08lx; it counts as released", what, (unsigned long)hr);
        if (d) d->Release();
        return kNotFound;
    }
    g_extras.push_back({});
    g_extras.back().dev = d;
    g_extras.back().guid = devs[i].guid;
    return (int)g_extras.size() - 1;
}

DWORD WINAPI pollThread(void*) {
    // Wait for a visible top-level window of this process.
    HWND hwnd = nullptr;
    while (!(hwnd = findOwnWindow())) Sleep(200);
    bool ffbOn = g_cfg.ffbEnabled && g_ffbCapable && !g_cfg.testMode;
    // FFBForeground: the wheel stays shared for reading and a second handle takes the force, only while the game has the focus.
    const bool fgMode = ffbOn && g_cfg.ffbForeground;
    IDirectInputDevice8W* ffbDev = g_dev;
    if (fgMode) {
        ffbDev = nullptr;
        HRESULT hc = g_di->CreateDevice(g_wheelGuid, &ffbDev, nullptr);
        if (SUCCEEDED(hc) && ffbDev) hc = ffbDev->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hc) || !ffbDev) {
            LOG("wheel ffb: could not open the force handle 0x%08lx; force feedback off", (unsigned long)hc);
            if (ffbDev) { ffbDev->Release(); ffbDev = nullptr; }
            ffbOn = false;
        }
    }
    HRESULT hr = S_OK;
    if (ffbOn && !fgMode) {
        hr = g_dev->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
        if (FAILED(hr)) {
            LOG("wheel ffb: exclusive cooperative level failed 0x%08lx; force feedback off", (unsigned long)hr);
            ffbOn = false;
        }
    }
    if (!ffbOn || fgMode) {
        hr = g_dev->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        if (FAILED(hr)) LOG("wheel: SetCooperativeLevel failed 0x%08lx", (unsigned long)hr);
    }
    setFullRange(g_dev);
    if (ffbOn && !fgMode) AutocenterOff(g_dev);
    g_dev->Acquire();
    setupExtras(hwnd);
    LOG("wheel: polling started");

    bool acquired = false;
    IDirectInputEffect* effect = nullptr;
    bool effectTried = false, needRestart = false, sendErrLogged = false, forceSend = false;
    DICONSTANTFORCE cf{};
    DIEFFECT eff{};
    LONG dir[1] = {1};          // eff points at these for the lifetime of the effect
    DWORD axes[1] = {DIJOFS_X};
    LONG lastSent = 0;
    double lastSendMs = 0;
    struct Sample { double ms; float angle; float phys; };
    std::deque<Sample> samples;
    float velocity = 0;
    float physVel = 0;     // degrees per second of the real wheel (not clamped at the lock)
    PedalRest rest[4];
    bool ffbHeld = false;      // FFBForeground: the force handle is acquired
    HWND ffbWnd = nullptr;
    double nextFgTryMs = 0;
    for (;;) {
        pollExtras();
        if (fgMode && ffbOn) {
            DWORD fgPid = 0;
            HWND fg = GetForegroundWindow();
            if (fg) GetWindowThreadProcessId(fg, &fgPid);
            const bool ours = fgPid == GetCurrentProcessId();
            if (ours && !ffbHeld && qpcMs() >= nextFgTryMs) {
                nextFgTryMs = qpcMs() + 250.0;
                if (fg != ffbWnd) {
                    ffbDev->Unacquire();
                    if (SUCCEEDED(ffbDev->SetCooperativeLevel(fg, DISCL_EXCLUSIVE | DISCL_FOREGROUND))) ffbWnd = fg;
                }
                if (ffbWnd && SUCCEEDED(ffbDev->Acquire())) {
                    ffbHeld = true;
                    AutocenterOff(ffbDev);
                    if (effect) needRestart = true;
                    LOG("wheel ffb: force taken (game has the focus)");
                }
            } else if (!ours && ffbHeld) {
                if (effect) { cf.lMagnitude = 0; effect->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS); lastSent = 0; }
                ffbDev->Unacquire();
                ffbHeld = false;
                LOG("wheel ffb: force released (game lost the focus)");
            }
        }
        hr = g_dev->Poll();
        DIJOYSTATE2 st{};
        if (SUCCEEDED(hr)) hr = g_dev->GetDeviceState(sizeof(st), &st);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            acquired = false;
            if (effect && !fgMode) needRestart = true;
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
            mapState(g_cfg, st, sourceState(g_pedalsFrom, st), sourceState(g_shifterFrom, st), sourceState(g_handbrakeFrom, st), rest, in);
            in.timestamp = qpc();
            in.valid = true;
            publish(in);

            if (g_diag) {
                static bool havePrev = false;
                static uint32_t prevBtn[4] = {};
                static DWORD prevPov = 0xFFFFFFFF;
                bool changed = !havePrev;
                for (int w = 0; w < 4; ++w) if (in.rawButtons[w] != prevBtn[w]) changed = true;
                if (changed && havePrev) {
                    std::string dn, up;
                    for (int b = 0; b < 128; ++b) {
                        bool now = (in.rawButtons[b / 32] >> (b % 32)) & 1, was = (prevBtn[b / 32] >> (b % 32)) & 1;
                        if (now && !was) dn += (dn.empty() ? "" : ",") + std::to_string(b);
                        if (!now && was) up += (up.empty() ? "" : ",") + std::to_string(b);
                    }
                    LOG("wheel buttons: down %s up %s", dn.empty() ? "-" : dn.c_str(), up.empty() ? "-" : up.c_str());
                }
                for (int w = 0; w < 4; ++w) prevBtn[w] = in.rawButtons[w];
                if (!havePrev || st.rgdwPOV[0] != prevPov) {
                    if (havePrev) LOG("wheel pov: %lu", (unsigned long)st.rgdwPOV[0]);
                    prevPov = st.rgdwPOV[0];
                }
                havePrev = true;
            }

            if (ffbOn && !effectTried && (!fgMode || ffbHeld)) {
                effectTried = true;
                axes[0] = g_forceAxis;
                cf.lMagnitude = 0;
                eff.dwSize = sizeof(DIEFFECT);
                eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
                eff.dwDuration = INFINITE;
                eff.dwSamplePeriod = 0;
                eff.dwGain = 10000;
                eff.dwTriggerButton = DIEB_NOTRIGGER;
                eff.dwTriggerRepeatInterval = 0;
                eff.cAxes = 1;
                eff.rgdwAxes = axes;
                eff.rglDirection = dir;
                eff.lpEnvelope = nullptr;
                eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
                eff.lpvTypeSpecificParams = &cf;
                eff.dwStartDelay = 0;
                HRESULT he = ffbDev->CreateEffect(GUID_ConstantForce, &eff, &effect, nullptr);
                if (SUCCEEDED(he) && effect) he = effect->Start(1, 0);
                if (SUCCEEDED(he) && effect) {
                    LOG("wheel ffb: constant force effect started");
                    lastSent = 0; lastSendMs = qpcMs();
                } else {
                    LOG("wheel ffb: constant force effect failed 0x%08lx; force feedback off", (unsigned long)he);
                    if (effect) { effect->Release(); effect = nullptr; }
                    ffbOn = false;
                }
            }
            if (effect && needRestart && (!fgMode || ffbHeld)) {
                effect->Start(1, 0);
                forceSend = true;      // reset lastSent: the next value is always sent
                needRestart = false;
            }
            if (ffbOn && effect) {
                double nowMs = qpcMs();
                float angle = VirtualWheel_GameAngle(in.steering);
                samples.push_back({nowMs, angle, in.steering * (float)g_cfg.wheelRange * 0.5f});
                // keep the newest sample that is >= 20 ms old; drop everything older
                int idx = -1;
                for (int i = (int)samples.size() - 1; i >= 0; --i)
                    if (nowMs - samples[i].ms >= 20.0) { idx = i; break; }
                if (idx >= 0) {
                    samples.erase(samples.begin(), samples.begin() + idx);
                    const float dtS = (float)((nowMs - samples.front().ms) / 1000.0);
                    velocity = (angle - samples.front().angle) / dtS;
                    physVel = (samples.back().phys - samples.front().phys) / dtS;
                }
                FfbParams p;
                VirtualWheel_FfbSnapshot(p);
                FfbTerms terms{};
                float F = FfbForce(p, g_cfg.ffbTuning, angle, velocity, nowMs, &terms);
                LONG mag = FfbDeviceMagnitude(F, g_cfg.ffbGain, g_cfg.ffbInvert);
                // Soft lock: end stop at the physical lock (works also when the game's FFB is idle, e.g. in menus).
                const float physLock = VirtualWheel_PhysicalLockDeg();
                const float physDeg = in.steering * (float)g_cfg.wheelRange * 0.5f;
                const float lockK = SoftLockK(physDeg, physLock, (float)g_cfg.wheelRange, g_cfg.ffbLockStrength, g_cfg.ffbLockRampDeg);
                if (lockK > 0.0f) {
                    mag = ApplySoftLock(mag, physDeg, physLock, (float)g_cfg.wheelRange, g_cfg.ffbLockStrength, g_cfg.ffbLockRampDeg,
                                        g_cfg.ffbInvert);
                    // damping inside the end zone, from the real wheel's speed (the game angle stops at the lock)
                    long damp = std::lround(physVel * 0.002f * lockK * 10000.0f * g_cfg.ffbLockStrength);
                    mag += g_cfg.ffbInvert ? -damp : damp;
                    mag = mag > 10000 ? 10000 : mag < -10000 ? -10000 : mag;
                }
                mag = FfbMinForce(mag, g_cfg.ffbMinForce);
                bool send = forceSend || labs(mag - lastSent) >= 20 || (mag != lastSent && nowMs - lastSendMs >= 10.0) ||
                            (mag == 0 && lastSent != 0);
                if (send && (!fgMode || ffbHeld)) {
                    cf.lMagnitude = mag;
                    HRESULT sh = effect->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
                    if (SUCCEEDED(sh)) {
                        lastSent = mag; lastSendMs = nowMs; forceSend = false;
                    } else if (sh == DIERR_INPUTLOST || sh == DIERR_NOTACQUIRED) {
                        needRestart = true;
                        if (fgMode) {      // the force handle is lost; the shared input handle still reads
                            ffbHeld = false;
                        } else {
                            acquired = false;
                            g_dev->Acquire();
                            AcquireSRWLockExclusive(&g_lock);
                            g_snap.valid = false;
                            ReleaseSRWLockExclusive(&g_lock);
                        }
                    } else if (!sendErrLogged) {
                        sendErrLogged = true;
                        LOG("wheel ffb: SetParameters failed 0x%08lx", (unsigned long)sh);
                    }
                }
                AcquireSRWLockExclusive(&g_lock);
                g_ffbStatus.terms = terms; g_ffbStatus.angle = angle; g_ffbStatus.velocity = velocity;
                g_ffbStatus.mag = mag; g_ffbStatus.lockDeg = std::fabs(physDeg); g_ffbStatus.lockLim = physLock * 0.5f;
                g_ffbStatus.lockK = lockK; g_ffbStatus.race = p.race; g_ffbStatus.stale = p.stale;
                g_ffbStatus.active = true;
                g_ffbStatus.haveGame = p.haveGame; g_ffbStatus.game = p.game;
                g_ffbStatus.leftAmp = p.leftAmp; g_ffbStatus.rightAmp = p.rightAmp;
                ReleaseSRWLockExclusive(&g_lock);
            }
        }
        Sleep(2);
    }
}

} // namespace

bool WheelDevice_Start(const WheelConfig& cfg, bool diagnostics) {
    if (g_started) return true;
    g_cfg = cfg;
    g_diag = diagnostics;
    if (cfg.testMode) {
        LOG("wheel: TestMode, synthetic input");
        if (HANDLE t = CreateThread(nullptr, 0, testThread, nullptr, 0, nullptr)) CloseHandle(t);
        g_started = true;
        return true;
    }
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                    reinterpret_cast<void**>(&g_di), nullptr);
    if (FAILED(hr) || !g_di) { LOG("wheel: DirectInput8Create failed 0x%08lx", (unsigned long)hr); return false; }
    const std::vector<FoundDevice> devs = ListGameDevices(g_di);
    for (auto& d : devs)
        LOG("wheel: device \"%s\" vid %04x pid %04x type 0x%x ffb %d", utf8(d.name.c_str()).c_str(),
            (unsigned)(d.productData1 & 0xFFFF), (unsigned)(d.productData1 >> 16), (unsigned)d.type, d.ffb);
    const std::vector<WheelCandidate> cands = WheelCandidates(devs);
    const std::wstring match = wide(cfg.deviceMatch);
    const int pick = PickWheel(cands, match);
    if (pick < 0) {
        LOG("wheel: no wheel found (DeviceMatch=%s)", cfg.deviceMatch.c_str());
        return false;
    }
    if (!match.empty() && FindDeviceByName(cands, match) < 0)
        LOG("wheel: nothing matches DeviceMatch=%s; choosing by type", cfg.deviceMatch.c_str());
    LOG("wheel: using \"%s\"", utf8(devs[pick].name.c_str()).c_str());
    g_wheelGuid = devs[pick].guid;
    hr = g_di->CreateDevice(devs[pick].guid, &g_dev, nullptr);
    if (FAILED(hr) || !g_dev) { LOG("wheel: CreateDevice failed 0x%08lx", (unsigned long)hr); return false; }
    hr = g_dev->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) { LOG("wheel: SetDataFormat failed 0x%08lx", (unsigned long)hr); g_dev->Release(); g_dev = nullptr; return false; }
    if (cfg.ffbEnabled) {
        DIDEVCAPS caps{};
        caps.dwSize = sizeof(caps);
        if (SUCCEEDED(g_dev->GetCapabilities(&caps)) && (caps.dwFlags & DIDC_FORCEFEEDBACK)) {
            g_ffbCapable = true;
            LOG("wheel ffb: device supports force feedback");
            g_forceAxis = ForceAxisOf(g_dev, axisOffset(cfg.steeringAxis));
            LOG("wheel ffb: force axis offset %lu (steering %lu)", (unsigned long)g_forceAxis, (unsigned long)axisOffset(cfg.steeringAxis));
            if (cfg.ffbLimitUnknown && g_cfg.ffbLockStrength > 0.5f &&
                !IsKnownGentleWheel(devs[pick].productData1 & 0xFFFF, devs[pick].productData1 >> 16)) {
                g_cfg.ffbLockStrength = 0.5f;
                LOG("wheel ffb: wheel not known to be gentle, soft lock limited to half force (FFBLimitUnknown=0 lifts this)");
            }
        } else {
            LOG("wheel ffb: device has no force feedback");
        }
    }
    g_pedalsFrom = openExtra(devs, pick, cfg.pedalsDevice, "pedals");
    g_shifterFrom = openExtra(devs, pick, cfg.shifterDevice, "shifter");
    g_handbrakeFrom = openExtra(devs, pick, cfg.handbrakeDevice, "handbrake");
    if (HANDLE t = CreateThread(nullptr, 0, pollThread, nullptr, 0, nullptr)) CloseHandle(t);
    g_started = true;
    return true;
}

bool WheelDevice_ButtonsHeld(const int* idx, int n) {
    if (!idx || n <= 0) return false;
    WheelInput in;
    if (!WheelDevice_Read(in)) return false;
    for (int i = 0; i < n; ++i)
        if (idx[i] < 0 || idx[i] > 127 || !((in.rawButtons[idx[i] / 32] >> (idx[i] % 32)) & 1)) return false;
    return true;
}

bool WheelDevice_FfbStatus(FfbStatus& out) {
    AcquireSRWLockShared(&g_lock);
    out = g_ffbStatus;
    ReleaseSRWLockShared(&g_lock);
    return out.active;
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

// Test hook for tests/harness.cpp: one input mapping with a fixed configuration.
extern "C" __declspec(dllexport) void FH2SI_TestMapInput(const DIJOYSTATE2* wheel, const DIJOYSTATE2* pedals, const DIJOYSTATE2* shifter,
                                                         const DIJOYSTATE2* handbrake, fh2::PedalRest* rest, fh2::WheelInput* out) {
    fh2::WheelConfig cfg;
    cfg.hShifter = true;
    cfg.handbrakeAxis = fh2::WheelAxis::X;
    fh2::mapState(cfg, *wheel, pedals, shifter, handbrake, rest, *out);
}
