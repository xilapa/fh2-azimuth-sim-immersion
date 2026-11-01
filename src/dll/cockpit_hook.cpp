// SPDX-License-Identifier: MIT
// Replaces the car update entry in the car vtable: after the game computed the in-game steering wheel angle
// (car + 0x4f30, radians) the angle of the real wheel is written over it, and the driver model is shown or
// hidden depending on the active camera. The same hook finds the local player's car every frame and
// re-applies the assists to it.
#include "cockpit_hook.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include "assist_hook.h"
#include "fov_hook.h"
#include "log.h"
#include "patch_util.h"
#include "wheel_com.h"
#include "wheel_device.h"

namespace fh2 {
namespace {

constexpr uintptr_t kCarUpdateRva  = 0x2a9400;     // void CarUpdate(Car*, float, float, int)
constexpr uintptr_t kCarUpdateSlot = 0x2c5cc00;     // vtable slot holding base + kCarUpdateRva
constexpr uintptr_t kSetDriverRva  = 0xf75500;     // void Car_SetDriverVisible(Car*, bool)
const uint8_t       kSetDriverBytes[10] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr size_t    kWheelAngleOff = 0x4f30;        // float, radians
constexpr size_t    kWheelAnimOff  = 0x4f38;        // float, animation offset the game adds to the angle when rendering
constexpr size_t    kFrozenOff     = 0x4f11;       // byte, != 0: the game freezes the angle (photo mode ...)
constexpr size_t    kDriverShownOff = 0x4f10;       // byte, 1 = driver visible
constexpr size_t    kDriverModelOff = 0x4d38;       // qword pointer, != 0: the car has a driver model
constexpr size_t    kDriverOff     = 0x4d00;        // qword pointer, null for cars without a driver
constexpr size_t    kDriverGetterSlot = 0xa8;           // driver vtable: returns an object whose first qword is set for the local player
constexpr size_t    kCarIdOff      = 0x118;             // uint32
constexpr size_t    kCarReadSize   = 0x4f40;
constexpr uint64_t  kCockpitFreshMs = 250;
constexpr uint64_t  kPlayerFreshMs  = 1000;

using CarUpdateFn = void (*)(uint8_t*, float, float, int);
using SetDriverFn = void (*)(uint8_t*, bool);

uintptr_t         g_base = 0;
bool              g_wheelOn = false, g_hideOn = false, g_hideReady = false, g_trackOn = false;
float             g_dir = 1.0f;
std::atomic<uint8_t*>  g_playerCar{nullptr};
std::atomic<uint64_t>  g_playerTick{0};
std::atomic<uint64_t>  g_ffbTick{0};
uint8_t*          g_hidCar = nullptr;          // game thread only
bool              g_weHid = false;
bool              g_diag = false;
std::atomic<bool> g_loggedWheel{false};

const float kPi = 3.14159265358979f;

// The in-game wheel angle for the steering value s (-1..1) at the given lock.
bool applyWheel(uint8_t* car, float s, float lock) {
    if (!readable(car, kCarReadSize)) return false;
    if (car[kFrozenOff] != 0) return false;
    const float a = g_dir * s * lock * 0.5f * kPi / 180.0f;
    const float noAnim = 0.0f;                  // the offset would turn the wheel beyond the real one
    std::memcpy(car + kWheelAngleOff, &a, sizeof(a));
    std::memcpy(car + kWheelAnimOff, &noAnim, sizeof(noAnim));
    return true;
}

void applyDriver(uint8_t* car) {
    if (!g_hideReady || !readable(car, kCarReadSize)) return;
    if (g_weHid && g_hidCar != car) g_weHid = false;      // the player's car changed: never touch the old one
    uint64_t model;
    std::memcpy(&model, car + kDriverModelOff, sizeof(model));
    if (model == 0) return;                                // no driver model: the game ignores the call too
    const bool cockpit = GetTickCount64() - FovHook_LastCockpitTick() <= kCockpitFreshMs && FovHook_LastCockpitTick() != 0;
    const bool want = !cockpit;                            // driver visible wanted
    auto setVisible = reinterpret_cast<SetDriverFn>(g_base + kSetDriverRva);
    if (!want && car[kDriverShownOff] != 0) {
        setVisible(car, false);
        g_weHid = true; g_hidCar = car;
        LOG("cockpit: driver hidden");
    } else if (want && g_weHid && car[kDriverShownOff] == 0) {
        setVisible(car, true);
        g_weHid = false;
        LOG("cockpit: driver shown");
    }
}

// The local player's car, from the game's own test (a driver whose virtual +0xa8 yields a non-null object),
// for when the FFB site is not running (free roam, loading).
void detectPlayer(uint8_t* car) {
    const uint64_t now = GetTickCount64();
    if (now - g_ffbTick.load(std::memory_order_relaxed) <= kPlayerFreshMs) return;
    uint8_t* cur = g_playerCar.load(std::memory_order_relaxed);
    if (cur != car && cur && now - g_playerTick.load(std::memory_order_relaxed) <= kPlayerFreshMs) return;   // keep the first match
    // no readable() here: this runs for every car every frame and the game dereferences the same pointers
    void* driver;
    std::memcpy(&driver, car + kDriverOff, sizeof(driver));
    if (!driver) return;
    auto getter = reinterpret_cast<void** (*)(void*)>((*reinterpret_cast<uint8_t***>(driver))[kDriverGetterSlot / 8]);
    void** p = getter(driver);
    if (!p || !*p) return;
    uint32_t id;
    std::memcpy(&id, car + kCarIdOff, sizeof(id));
    if (cur != car) LOG("car: player car found from the car update (id %u)", id);
    g_playerCar.store(car, std::memory_order_relaxed);
    g_playerTick.store(now, std::memory_order_relaxed);
    AssistHook_SetPlayerCar(id);
    SteerLock_SetPlayerCar(id);
}

void hookCarUpdate(uint8_t* car, float a, float b, int c) {
    reinterpret_cast<CarUpdateFn>(g_base + kCarUpdateRva)(car, a, b, c);
    if (car && g_trackOn) detectPlayer(car);
    if (!car || car != g_playerCar.load(std::memory_order_relaxed)) return;
    if (GetTickCount64() - g_playerTick.load(std::memory_order_relaxed) > kPlayerFreshMs) return;
    if (!readable(car, kCarReadSize)) return;
    AssistHook_ApplyToCar(car);
    if (g_wheelOn) {
        static uint32_t updates = 0, writes = 0, skipped = 0;      // game thread only
        static uint64_t lastDiag = GetTickCount64();
        static float steer = 0.0f;
        ++updates;
        WheelInput in;
        if (WheelDevice_Read(in)) {
            steer = in.steering;
            if (applyWheel(car, VirtualWheel_GameAngle(in.steering), VirtualWheel_PhysicalLockDeg())) ++writes; else ++skipped;
            if (!g_loggedWheel.exchange(true))
                LOG("cockpit: in-game wheel follows the real wheel (lock %.0f deg)", VirtualWheel_PhysicalLockDeg());
        }
        if (g_diag && GetTickCount64() - lastDiag >= 5000) {
            float ang; std::memcpy(&ang, car + kWheelAngleOff, sizeof(ang));
            LOG("cockpit diag: updates %u writes %u skipped %u angle %.1f deg lock %.0f deg steer %.3f",
                updates, writes, skipped, ang * 180.0f / kPi, VirtualWheel_PhysicalLockDeg(), steer);
            updates = writes = skipped = 0;
            lastDiag = GetTickCount64();
        }
    }
    if (g_hideOn) applyDriver(car);
}

} // namespace

bool CockpitHook_NeedsCarTracking(const Config& cfg) {
    return cfg.sim.realisticAbs || cfg.sim.realisticTc || cfg.sim.realisticStm || cfg.sim.realisticClutch ||
           cfg.sim.realisticAutomatic || cfg.sim.realisticSteeringLock ||
           cfg.sim.realCockpitWheel || cfg.sim.hideDriverInCockpit;
}

// The FFB site is authoritative: g_ffbTick keeps detectPlayer quiet while it reports.
void CockpitHook_SetPlayerCar(uint8_t* car) {
    const uint64_t now = GetTickCount64();
    g_playerCar.store(car, std::memory_order_relaxed);
    g_playerTick.store(now, std::memory_order_relaxed);
    g_ffbTick.store(now, std::memory_order_relaxed);
}

void CockpitHook_Install(uintptr_t base, const Config& cfg) {
    g_wheelOn = cfg.sim.realCockpitWheel;
    g_hideOn = cfg.sim.hideDriverInCockpit;
    g_dir = cfg.sim.cockpitWheelDirection < 0 ? -1.0f : 1.0f;
    g_diag = cfg.diagnostics;
    g_trackOn = CockpitHook_NeedsCarTracking(cfg);
    if (!g_trackOn) return;
    g_base = base;
    if (g_hideOn) {
        if (std::memcmp(reinterpret_cast<const void*>(base + kSetDriverRva), kSetDriverBytes, sizeof(kSetDriverBytes)) != 0) {
            LOG("cockpit: Car_SetDriverVisible bytes mismatch; driver hiding off");
            g_hideOn = false;
        } else {
            g_hideReady = true;
            LOG("cockpit: driver hide ready");
        }
    }
    uint64_t* slot = reinterpret_cast<uint64_t*>(base + kCarUpdateSlot);
    if (*slot != static_cast<uint64_t>(base + kCarUpdateRva)) {
        LOG("cockpit: car update slot mismatch; unchanged");
        return;
    }
    DWORD prot;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &prot)) {
        LOG("cockpit: VirtualProtect failed %lu; unchanged", GetLastError());
        return;
    }
    *slot = reinterpret_cast<uint64_t>(&hookCarUpdate);
    VirtualProtect(slot, 8, prot, &prot);
    LOG("cockpit: car update hook installed (in-game wheel %d, hide driver %d, car tracking %d, direction %+d)", g_wheelOn, g_hideOn, g_trackOn, (int)g_dir);
}

} // namespace fh2

// Test hook for the integration harness: runs the wheel angle computation on a fake car buffer as if it were
// the player car with a display lock of 900 degrees.
extern "C" __declspec(dllexport) void FH2SI_TestCockpitWheel(uint8_t* car, float s) {
    fh2::applyWheel(car, s, 900.0f);
}

extern "C" __declspec(dllexport) uint8_t* FH2SI_TestDetectPlayer(uint8_t* car, bool ffb) { if (ffb) fh2::CockpitHook_SetPlayerCar(car); else fh2::detectPlayer(car); return fh2::g_playerCar.load(); }
