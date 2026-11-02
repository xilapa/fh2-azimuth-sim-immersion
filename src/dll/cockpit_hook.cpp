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
#include "spindle.h"
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
constexpr uintptr_t kPresVtableRva[2] = {0x2ec1450, 0x2ec2b70};  // CarPresentation, its ref-counted form (behind the deferred wrapper)
constexpr uintptr_t kPresDeferredVtableRva[2] = {0x2ebe7e0, 0x2ebef68};  // CCarPresentationDeferred, what car + kDriverModelOff really holds; the CarPresentation is at +8
constexpr size_t    kPresRigOff    = 0x1298;        // qword pointer: the driver rig, which owns the steering wheel model
constexpr uintptr_t kRigAttachSite = 0x137d8c9;     // CarPresentation calls: AttachWheel(rig, carMatrix, angle, a4, a5)
constexpr uintptr_t kRigAttachRva  = 0x13ac5c0;
constexpr uintptr_t kSetBoneSite   = 0x13ac80a;     // inside AttachWheel: SetBone(model, bone, matrix, a4, a5, a6) for the wheel spindle
constexpr uintptr_t kSetBoneRva    = 0x13f4ec0;
constexpr uint64_t  kCockpitFreshMs = 250;
constexpr uint64_t  kPlayerFreshMs  = 1000;

using CarUpdateFn = void (*)(uint8_t*, float, float, int);
using SetDriverFn = void (*)(uint8_t*, bool);
using RigAttachFn = uint64_t (*)(void*, const float*, float, void*, void*);
using SetBoneFn = uint64_t (*)(void*, uint32_t, const float*, uint64_t, uint64_t, uint64_t);

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
std::atomic<void*> g_playerRig{nullptr};
std::atomic<bool> g_loggedPresVt{false};
thread_local const float* t_carMatrix = nullptr;     // set while the player's AttachWheel runs
thread_local float t_wheelAngle = 0.0f;
SRWLOCK           g_spindleLock = SRWLOCK_INIT;      // the game runs AttachWheel on several threads; they share one state
Spindle           g_spindle;
void*             g_spindleModel = nullptr;
uint64_t          g_spindleDiag = 0;

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

// The driver rig of the player's car; the wheel model copies its rotation from this rig's animation.
void updatePlayerRig(uint8_t* car) {
    uint8_t* pres;
    std::memcpy(&pres, car + kDriverModelOff, sizeof(pres));
    void* rig = nullptr;
    if (pres && readable(pres, 16)) {
        uint64_t vt;
        std::memcpy(&vt, pres, sizeof(vt));
        if (vt == g_base + kPresDeferredVtableRva[0] || vt == g_base + kPresDeferredVtableRva[1]) {
            std::memcpy(&pres, pres + 8, sizeof(pres));
            vt = 0;
            if (pres && readable(pres, 8)) std::memcpy(&vt, pres, sizeof(vt));
        }
        if (vt == g_base + kPresVtableRva[0] || vt == g_base + kPresVtableRva[1]) {
            if (readable(pres, kPresRigOff + 8)) std::memcpy(&rig, pres + kPresRigOff, sizeof(rig));
        } else if (vt && !g_loggedPresVt.exchange(true)) {
            LOG("cockpit: unknown driver presentation vtable rva 0x%llx; wheel stays at the animation's limit", (unsigned long long)(vt - g_base));
        }
    }
    g_playerRig.store(rig, std::memory_order_relaxed);
}

uint64_t hookRigAttach(void* rig, const float* carMatrix, float angle, void* a4, void* a5) {
    if (rig && rig == g_playerRig.load(std::memory_order_relaxed)) {
        t_carMatrix = carMatrix;
        t_wheelAngle = angle;
    }
    const uint64_t r = reinterpret_cast<RigAttachFn>(g_base + kRigAttachRva)(rig, carMatrix, angle, a4, a5);
    t_carMatrix = nullptr;
    return r;
}

uint64_t hookSetBone(void* model, uint32_t bone, const float* m, uint64_t a4, uint64_t a5, uint64_t a6) {
    if (t_carMatrix) {
        alignas(16) float out[16];
        AcquireSRWLockExclusive(&g_spindleLock);
        if (g_spindleModel != model) { g_spindle = Spindle(); g_spindleModel = model; }
        const int axisBefore = g_spindle.axis;
        const bool turned = SpindleRotate(g_spindle, m, t_carMatrix, t_wheelAngle, out);
        const Spindle now = g_spindle;
        const uint64_t tick = GetTickCount64();
        const bool diag = g_diag && tick - g_spindleDiag >= 5000;
        if (diag) g_spindleDiag = tick;
        ReleaseSRWLockExclusive(&g_spindleLock);
        if (now.axis != axisBefore) LOG("cockpit: wheel turns around bone axis %d, direction %+.0f", now.axis, now.sign);
        if (diag) LOG("cockpit diag: spindle centre seen %d axis %d angle %.0f deg turned %d", now.hasRef, now.axis, t_wheelAngle * 180.0f / kPi, turned);
        if (turned) return reinterpret_cast<SetBoneFn>(g_base + kSetBoneRva)(model, bone, out, a4, a5, a6);
    }
    return reinterpret_cast<SetBoneFn>(g_base + kSetBoneRva)(model, bone, m, a4, a5, a6);
}

// A call site is patched only if it still calls the expected function.
bool hookCallSite(uintptr_t siteRva, uintptr_t targetRva, void* hook) {
    const uintptr_t site = g_base + siteRva;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(site);
    int32_t rel;
    std::memcpy(&rel, p + 1, sizeof(rel));
    if (p[0] != 0xE8 || site + 5 + rel != g_base + targetRva) return false;
    void* mem = allocNear(site, 64);
    return mem && patchCallRel32(site, hook, mem);
}

void hookCarUpdate(uint8_t* car, float a, float b, int c) {
    reinterpret_cast<CarUpdateFn>(g_base + kCarUpdateRva)(car, a, b, c);
    if (car && g_trackOn) detectPlayer(car);
    if (!car || car != g_playerCar.load(std::memory_order_relaxed)) return;
    if (GetTickCount64() - g_playerTick.load(std::memory_order_relaxed) > kPlayerFreshMs) return;
    if (!readable(car, kCarReadSize)) return;
    AssistHook_ApplyToCar(car);
    if (g_wheelOn) {
        updatePlayerRig(car);
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
    if (g_wheelOn) {
        // SetBone first: nothing happens until AttachWheel, the outer call, is hooked too.
        const bool ok = hookCallSite(kSetBoneSite, kSetBoneRva, reinterpret_cast<void*>(&hookSetBone)) &&
                        hookCallSite(kRigAttachSite, kRigAttachRva, reinterpret_cast<void*>(&hookRigAttach));
        LOG("cockpit: wheel spindle hook %s", ok ? "installed" : "mismatch; the wheel stays within the animation's range");
    }
    LOG("cockpit: car update hook installed (in-game wheel %d, hide driver %d, car tracking %d, direction %+d)", g_wheelOn, g_hideOn, g_trackOn, (int)g_dir);
}

} // namespace fh2

// Test hook for the integration harness: runs the wheel angle computation on a fake car buffer as if it were
// the player car with a display lock of 900 degrees.
extern "C" __declspec(dllexport) void FH2SI_TestCockpitWheel(uint8_t* car, float s) {
    fh2::applyWheel(car, s, 900.0f);
}

extern "C" __declspec(dllexport) uint8_t* FH2SI_TestDetectPlayer(uint8_t* car, bool ffb) { if (ffb) fh2::CockpitHook_SetPlayerCar(car); else fh2::detectPlayer(car); return fh2::g_playerCar.load(); }

extern "C" __declspec(dllexport) void* FH2SI_TestPlayerRig(uint8_t* car) { fh2::updatePlayerRig(car); return fh2::g_playerRig.load(); }
