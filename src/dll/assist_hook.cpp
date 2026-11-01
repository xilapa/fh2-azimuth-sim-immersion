// SPDX-License-Identifier: MIT
// Replaces the ABS / traction control / stability control / shifting-mode getters of LocalPlayerChoices so that
// each car gets the assists its real counterpart had (data/car_assists.csv), or a per-car override.
// The same choices are also written to the bytes on the player's car every frame, because the game copies
// them to the car before the car is known. The assist state and the physics fields are forced too, and again
// whenever the pause menu hands new assists to the car dynamics.
#include "assist_hook.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include "../common/car_db.h"
#include "log.h"
#include "patch_util.h"

namespace fh2 {
namespace {

constexpr uintptr_t kVtableRva[2]  = {0x2c5bfc8, 0x2cf39c0};   // LocalPlayerChoices, TRefCountedObject<ForzaLocalPlayerChoices>
constexpr size_t    kAbsSlotOff    = 0xd8;                      // braking mode: 0 assisted, 1 ABS on, 2 ABS off
constexpr size_t    kClutchSlotOff = 0xf8;                   // shifting mode: 0 automatic, 1 manual, 2 manual with clutch
constexpr size_t    kStmSlotOff    = 0xe8;                      // stability control: 0 on, 1 off
constexpr size_t    kTcSlotOff     = 0xf0;                      // traction control: 0 on, 1 off
constexpr uintptr_t kAbsGetterRva  = 0xfbcb50;
constexpr uintptr_t kStmGetterRva  = 0xfbcb80;
constexpr uintptr_t kTcGetterRva   = 0x1391660;
constexpr uintptr_t kClutchGetterRva = 0xfbcb70;
constexpr size_t    kAbsFieldOff   = 0x2b1;
constexpr size_t    kStmFieldOff   = 0x2b3;
constexpr size_t    kTcFieldOff    = 0x2b4;
constexpr size_t    kClutchFieldOff = 0x2b5;
constexpr size_t    kCarAutobrakeOff = 0x328;                   // car bytes the game copies its choices to (0/1)
constexpr size_t    kCarAbsOff     = 0x329;
constexpr size_t    kCarTcOff      = 0x32a;
constexpr size_t    kCarStmOff     = 0x32b;
constexpr size_t    kCarShiftOff   = 0x330;                     // int32 shifting mode
constexpr size_t    kCarStateOff   = 0x1200;                    // assist state the pause menu writes, passed to the dynamics
constexpr size_t    kStateOffs[3]  = {0x4abc, 0x4ac0, 0x4ac4};  // int32 ABS, TC, STM in the car (0 off)
constexpr size_t    kStateShiftOff = 0x4ab0;                    // int32 shifting mode
constexpr size_t    kCarDynOff     = 0x4c08;                    // pointer to the car dynamics (secondary base of the object)
constexpr size_t    kDynObjAdj     = 0x3a0;                     // dynamics pointer - complete object
constexpr size_t    kObjOffs[3]    = {0x1364, 0x1360, 0x135c};  // int32 ABS, TC, STM the physics reads
constexpr uintptr_t kDynVtableRva[2] = {0x2eb4bb8, 0x2eb5e18};  // CCarDynamics, TRefCountedObjectThreadSafe<CCarDynamics>
constexpr uintptr_t kDynApplySlotRva[2] = {0x2eb4df0, 0x2eb6050};   // slot 0x238 of those vtables
constexpr uintptr_t kDynApplyRva   = 0x12da500;                 // copies the state to the physics fields
constexpr uintptr_t kRealVtableRva = 0x2c5de38;               // ForzaPlayerChoices at +0xae8: the object the game really reads
constexpr uintptr_t kRealAbsRva = 0x291f40, kRealStmRva = 0x292024, kRealTcRva = 0x292134, kRealClutchRva = 0x291fd4;
using GetterFn = uint8_t (*)(void*);

uintptr_t g_base = 0;
bool      g_absOn = false, g_tcOn = false, g_stmOn = false, g_clutchOn = false, g_autoOn = false;
AssistMode g_optional = AssistMode::Game;
std::vector<CarAssistOverride> g_overrides;

std::atomic<bool>     g_installed{false};
std::atomic<int64_t>  g_carId{-1};
std::atomic<const char*> g_carName{nullptr};
std::atomic<int>      g_absMode{0}, g_tcMode{0}, g_stmMode{0}, g_clutchMode{0};   // AssistMode of the current car (clutch: On = has one)
std::atomic<bool>     g_forceAuto{false};                                          // current car: always shift automatically
std::atomic<uint8_t*> g_playerCarPtr{nullptr};
std::atomic<bool>     g_loggedDynVtable{false};
std::atomic<bool>     g_loggedAbs{false}, g_loggedTc{false}, g_loggedStm{false}, g_loggedClutch{false}, g_loggedAuto{false};
std::atomic<uint32_t> g_callsAbs{0}, g_callsTc{0}, g_callsStm{0}, g_callsClutch{0};

const char* word(char c) {
    switch (c) { case 'y': return "yes"; case 'n': return "no"; case 'o': return "optional"; default: return "unknown"; }
}

AssistMode carValue(char c) {
    switch (c) {
        case 'y': return AssistMode::On;
        case 'n': return AssistMode::Off;
        case 'o': return g_optional;
        default:  return AssistMode::Game;
    }
}

const char* gearboxWord(char c) {
    switch (c) {
        case 'm': return "manual"; case 'x': return "mixed"; case 'a': return "at"; case 't': return "atm"; case 'd': return "dct";
        case 's': return "amt"; case 'c': return "cvt"; case 'e': return "ev"; case 'q': return "seq"; default: return "unknown";
    }
}

const char* modeName(AssistMode m) { return m == AssistMode::On ? "on" : m == AssistMode::Off ? "off" : "game"; }

// ---- getters (same signature as the originals: movzx eax, byte [rcx+X]) --------------------------
uint8_t mapAbs(uint8_t real) {
    uint8_t out = real;
    switch (static_cast<AssistMode>(g_absMode.load(std::memory_order_relaxed))) {
        case AssistMode::On:  out = real == 2 ? 1 : real; break;
        case AssistMode::Off: out = 2; break;
        default: break;
    }
    if (out != real && !g_loggedAbs.exchange(true)) {
        const char* n = g_carName.load();
        LOG("assists: ABS forced %s for %s", out == 2 ? "off" : "on", n ? n : "unknown car");
    }
    return out;
}

uint8_t mapTcLike(uint8_t real, std::atomic<int>& mode, std::atomic<bool>& logged, const char* what) {
    uint8_t out = real;
    switch (static_cast<AssistMode>(mode.load(std::memory_order_relaxed))) {
        case AssistMode::On:  out = 0; break;
        case AssistMode::Off: out = 1; break;
        default: break;
    }
    if (out != real && !logged.exchange(true)) {
        const char* n = g_carName.load();
        LOG("assists: %s forced %s for %s", what, out == 0 ? "on" : "off", n ? n : "unknown car");
    }
    return out;
}

uint8_t mapClutch(uint8_t real) {
    uint8_t out = real;
    if (g_forceAuto.load(std::memory_order_relaxed)) {
        out = 0;
        if (!g_loggedAuto.exchange(true)) {
            const char* n = g_carName.load();
            LOG("assists: automatic shifting forced for %s", n ? n : "unknown car");
        }
        return out;
    }
    switch (static_cast<AssistMode>(g_clutchMode.load(std::memory_order_relaxed))) {
        case AssistMode::On:  if (real == 1) out = 2; break;           // automatic stays automatic
        case AssistMode::Off: if (real == 2) out = 1; break;
        default: break;
    }
    if (out != real && !g_loggedClutch.exchange(true)) {
        const char* n = g_carName.load();
        LOG("assists: clutch forced %s for %s", out == 2 ? "on" : "off", n ? n : "unknown car");
    }
    return out;
}

uint8_t absGetter(void* self) {
    g_callsAbs.fetch_add(1, std::memory_order_relaxed);
    return mapAbs(*(reinterpret_cast<uint8_t*>(self) + kAbsFieldOff));
}

uint8_t tcGetter(void* self) {
    g_callsTc.fetch_add(1, std::memory_order_relaxed);
    return mapTcLike(*(reinterpret_cast<uint8_t*>(self) + kTcFieldOff), g_tcMode, g_loggedTc, "TC");
}

uint8_t stmGetter(void* self) {
    g_callsStm.fetch_add(1, std::memory_order_relaxed);
    return mapTcLike(*(reinterpret_cast<uint8_t*>(self) + kStmFieldOff), g_stmMode, g_loggedStm, "stability control");
}

uint8_t clutchGetter(void* self) {
    g_callsClutch.fetch_add(1, std::memory_order_relaxed);
    return mapClutch(*(reinterpret_cast<uint8_t*>(self) + kClutchFieldOff));
}

// Getters of the vtable the game really reads: they compute the value, so the original is called.
uint8_t realAbsGetter(void* self) {
    g_callsAbs.fetch_add(1, std::memory_order_relaxed);
    return mapAbs(reinterpret_cast<GetterFn>(g_base + kRealAbsRva)(self));
}

uint8_t realTcGetter(void* self) {
    g_callsTc.fetch_add(1, std::memory_order_relaxed);
    return mapTcLike(reinterpret_cast<GetterFn>(g_base + kRealTcRva)(self), g_tcMode, g_loggedTc, "TC");
}

uint8_t realStmGetter(void* self) {
    g_callsStm.fetch_add(1, std::memory_order_relaxed);
    return mapTcLike(reinterpret_cast<GetterFn>(g_base + kRealStmRva)(self), g_stmMode, g_loggedStm, "stability control");
}

uint8_t realClutchGetter(void* self) {
    g_callsClutch.fetch_add(1, std::memory_order_relaxed);
    return mapClutch(reinterpret_cast<GetterFn>(g_base + kRealClutchRva)(self));
}

// 0 / 1 for ABS (which 0), TC (1) or STM (2) when its mode forces a value, -1 for the game's setting.
int32_t forcedState(int which) {
    const int mode = which == 0 ? g_absMode.load() : which == 1 ? g_tcMode.load() : g_stmMode.load();
    return mode == static_cast<int>(AssistMode::On) ? 1 : mode == static_cast<int>(AssistMode::Off) ? 0 : -1;
}

// The assist state the pause menu writes straight from its choices (the getters are bypassed).
void applyStateFields(uint8_t* car) {
    for (int i = 0; i < 3; ++i) {
        const int32_t v = forcedState(i);
        if (v >= 0) std::memcpy(car + kStateOffs[i], &v, sizeof(v));
    }
    int32_t shift;
    std::memcpy(&shift, car + kStateShiftOff, sizeof(shift));
    if (shift >= 0 && shift <= 2) {
        const int32_t out = mapClutch(static_cast<uint8_t>(shift));
        if (out != shift) std::memcpy(car + kStateShiftOff, &out, sizeof(out));
    }
}

// The car dynamics object whose int32 fields the physics reads, or null when the car has none or it is unknown.
uint8_t* dynObject(uint8_t* car) {
    uint8_t* dyn;
    std::memcpy(&dyn, car + kCarDynOff, sizeof(dyn));
    if (!dyn || !readable(dyn, 8)) return nullptr;
    uint64_t vt;
    std::memcpy(&vt, dyn, sizeof(vt));
    if (vt != g_base + kDynVtableRva[0] && vt != g_base + kDynVtableRva[1]) {
        if (!g_loggedDynVtable.exchange(true)) LOG("assists: unknown car dynamics vtable; physics fields not set");
        return nullptr;
    }
    uint8_t* obj = dyn - kDynObjAdj;
    return readable(obj + kObjOffs[2], 12) ? obj : nullptr;
}

// The game copied the choices to the car before we knew which car it was; map them again with the same rules.
void applyCarBytes(uint8_t* car) {
    const uint8_t absChoice = car[kCarAbsOff] ? (car[kCarAutobrakeOff] ? 0 : 1) : 2;
    const uint8_t absOut = mapAbs(absChoice);
    car[kCarAutobrakeOff] = absOut == 0;
    car[kCarAbsOff] = absOut < 2;
    car[kCarTcOff] = mapTcLike(car[kCarTcOff] ? 0 : 1, g_tcMode, g_loggedTc, "TC") != 1;
    car[kCarStmOff] = mapTcLike(car[kCarStmOff] ? 0 : 1, g_stmMode, g_loggedStm, "stability control") != 1;
    int32_t shift;
    std::memcpy(&shift, car + kCarShiftOff, sizeof(shift));
    if (shift >= 0 && shift <= 2) {
        const int32_t out = mapClutch(static_cast<uint8_t>(shift));
        if (out != shift) std::memcpy(car + kCarShiftOff, &out, sizeof(out));
    }
    applyStateFields(car);
    uint8_t* obj = dynObject(car);
    if (!obj) return;
    for (int i = 0; i < 3; ++i) {
        const int32_t v = forcedState(i);
        if (v >= 0) std::memcpy(obj + kObjOffs[i], &v, sizeof(v));
    }
}

// ---- decision per car ----------------------------------------------------------------------------
void decide(uint32_t id, const CarInfo* car) {
    AssistMode a = AssistMode::Game, t = AssistMode::Game, s = AssistMode::Game, c = AssistMode::Game;
    if (car) {
        if (g_absOn) a = carValue(car->abs);
        if (g_tcOn)  t = carValue(car->tc);
        if (g_stmOn) s = carValue(car->stm);
        if (g_clutchOn) c = carValue(car->clutch);
    }
    // automatic-only gearbox: forced unless the car has an explicit clutch override (on / off)
    bool forceAuto = car && g_autoOn && (car->gearbox == 'a' || car->gearbox == 'c' || car->gearbox == 'e');
    for (const auto& o : g_overrides) {
        if (o.id != id) continue;
        if (o.clutchSet) forceAuto = o.forceAutomatic;      // "automatic" forces; on / off cancel; auto (not set) keeps the rule
        if (o.clutchSet && g_clutchOn && !o.forceAutomatic) c = o.clutch;
        if (o.absSet && g_absOn) a = o.abs;
        if (o.tcSet && g_tcOn)   t = o.tc;
        if (o.stmSet && g_stmOn) s = o.stm;
        break;
    }
    g_absMode = static_cast<int>(a);
    g_tcMode  = static_cast<int>(t);
    g_stmMode = static_cast<int>(s);
    g_clutchMode = static_cast<int>(c);
    g_forceAuto = forceAuto;
}

DWORD WINAPI diagThread(void*) {
    for (;;) {
        Sleep(5000);
        int64_t id = g_carId.load();
        LOG("assists diag: car %u getters abs %u tc %u stm %u clutch %u (calls since last)", id < 0 ? 0u : (unsigned)id,
            g_callsAbs.exchange(0), g_callsTc.exchange(0), g_callsStm.exchange(0), g_callsClutch.exchange(0));
    }
}

// order abs, stm, tc, clutch; a null fn leaves its slot alone
bool patchVtable(uintptr_t vtableRva, const uintptr_t expected[4], void* const fns[4], const char* name) {
    uint64_t* vt = reinterpret_cast<uint64_t*>(g_base + vtableRva);
    uint64_t* slots[4] = {vt + kAbsSlotOff / 8, vt + kStmSlotOff / 8, vt + kTcSlotOff / 8, vt + kClutchSlotOff / 8};
    for (int i = 0; i < 4; ++i) {
        if (*slots[i] != g_base + expected[i]) {
            LOG("assists: vtable %s mismatch; unchanged", name);
            return false;
        }
    }
    struct Job { uint64_t* slot; void* fn; };
    const Job jobs[4] = {{slots[0], fns[0]}, {slots[1], fns[1]}, {slots[2], fns[2]}, {slots[3], fns[3]}};
    for (const Job& j : jobs) {
        if (!j.fn) continue;
        DWORD prot;
        if (!VirtualProtect(j.slot, 8, PAGE_READWRITE, &prot)) {
            LOG("assists: vtable %s VirtualProtect failed %lu; slot unchanged", name, GetLastError());
            continue;
        }
        *j.slot = reinterpret_cast<uint64_t>(j.fn);
        VirtualProtect(j.slot, 8, prot, &prot);
    }
    return true;
}

// The pause menu changes the assists, then calls this; the physics gearbox setup is only read inside it.
uint64_t dynApplyHook(void* self, uint8_t* state, uint64_t a3, uint64_t a4) {
    uint8_t* car = g_playerCarPtr.load(std::memory_order_relaxed);
    if (car && state == car + kCarStateOff) applyStateFields(car);
    return reinterpret_cast<uint64_t (*)(void*, uint8_t*, uint64_t, uint64_t)>(g_base + kDynApplyRva)(self, state, a3, a4);
}

bool patchDynApplySlot(uintptr_t slotRva) {
    uint64_t* slot = reinterpret_cast<uint64_t*>(g_base + slotRva);
    if (*slot != g_base + kDynApplyRva) {
        LOG("assists: dynamics apply slot %#llx mismatch; unchanged", static_cast<unsigned long long>(slotRva));
        return false;
    }
    DWORD prot;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &prot)) {
        LOG("assists: dynamics apply slot VirtualProtect failed %lu; slot unchanged", GetLastError());
        return false;
    }
    *slot = reinterpret_cast<uint64_t>(&dynApplyHook);
    VirtualProtect(slot, 8, prot, &prot);
    return true;
}

} // namespace

void AssistHook_SetPlayerCar(uint32_t ordinal) {
    if (!g_installed.load(std::memory_order_acquire)) return;
    if (g_carId.load(std::memory_order_relaxed) == static_cast<int64_t>(ordinal)) return;
    const CarInfo* car = FindCar(ordinal);
    decide(ordinal, car);
    g_carName.store(car ? car->name : nullptr);
    g_loggedAbs = false; g_loggedTc = false; g_loggedStm = false; g_loggedClutch = false; g_loggedAuto = false;
    g_carId.store(static_cast<int64_t>(ordinal));
    LOG("car: %u %s (ABS %s, TC %s, STM %s, clutch %s, gearbox %s)", ordinal, car ? car->name : "unknown car",
        car ? word(car->abs) : "unknown", car ? word(car->tc) : "unknown", car ? word(car->stm) : "unknown",
        car ? word(car->clutch) : "unknown", car ? gearboxWord(car->gearbox) : "unknown");
}

// shortcut: a car swap keeps a forced value when the next car uses the game's setting, until the game copies the choices again; fix by remembering the game's values if players report it.
void AssistHook_ApplyToCar(uint8_t* car) {
    if (!g_installed.load(std::memory_order_acquire)) return;
    g_playerCarPtr.store(car, std::memory_order_relaxed);
    applyCarBytes(car);
}

void AssistHook_Install(uintptr_t base, const Config& cfg) {
    g_absOn = cfg.sim.realisticAbs;
    g_tcOn  = cfg.sim.realisticTc;
    g_stmOn = cfg.sim.realisticStm;
    g_clutchOn = cfg.sim.realisticClutch;
    g_autoOn = cfg.sim.realisticAutomatic;
    if (!g_absOn && !g_tcOn && !g_stmOn && !g_clutchOn && !g_autoOn) return;
    g_base = base;
    g_optional = cfg.sim.optionalAssists;
    g_overrides = cfg.sim.carOverrides;
    const bool clutchWanted = g_clutchOn || g_autoOn;
    void* const fns[4] = {g_absOn ? reinterpret_cast<void*>(&absGetter) : nullptr,
                          g_stmOn ? reinterpret_cast<void*>(&stmGetter) : nullptr,
                          g_tcOn ? reinterpret_cast<void*>(&tcGetter) : nullptr,
                          clutchWanted ? reinterpret_cast<void*>(&clutchGetter) : nullptr};
    void* const realFns[4] = {g_absOn ? reinterpret_cast<void*>(&realAbsGetter) : nullptr,
                              g_stmOn ? reinterpret_cast<void*>(&realStmGetter) : nullptr,
                              g_tcOn ? reinterpret_cast<void*>(&realTcGetter) : nullptr,
                              clutchWanted ? reinterpret_cast<void*>(&realClutchGetter) : nullptr};
    const uintptr_t expected[4] = {kAbsGetterRva, kStmGetterRva, kTcGetterRva, kClutchGetterRva};
    const uintptr_t realExpected[4] = {kRealAbsRva, kRealStmRva, kRealTcRva, kRealClutchRva};
    int ok = 0;
    if (patchVtable(kVtableRva[0], expected, fns, "LocalPlayerChoices")) ++ok;
    if (patchVtable(kVtableRva[1], expected, fns, "ForzaLocalPlayerChoices")) ++ok;
    if (patchVtable(kRealVtableRva, realExpected, realFns, "real")) ++ok;
    if (!ok) { LOG("assists: no vtable verified; realistic assists inactive"); return; }
    LOG("assists: getters installed in %d vtable(s) (ABS %d, TC %d, STM %d, clutch %d, automatic %d, optional %s)", ok, g_absOn, g_tcOn,
        g_stmOn, g_clutchOn, g_autoOn, modeName(g_optional));
    const int dynSlots = patchDynApplySlot(kDynApplySlotRva[0]) + patchDynApplySlot(kDynApplySlotRva[1]);
    LOG("assists: dynamics apply hooked in %d slot(s)", dynSlots);
    g_installed.store(true, std::memory_order_release);
    if (cfg.diagnostics)
        if (HANDLE t = CreateThread(nullptr, 0, diagThread, nullptr, 0, nullptr)) CloseHandle(t);
}

} // namespace fh2

// Test hook for the integration harness: applies the given modes to a fake car buffer, then restores the defaults.
extern "C" __declspec(dllexport) void FH2SI_TestCarAssistBytes(uint8_t* car, int absMode, int tcMode, int stmMode, int clutchMode, bool forceAuto) {
    fh2::g_absMode = absMode; fh2::g_tcMode = tcMode; fh2::g_stmMode = stmMode; fh2::g_clutchMode = clutchMode;
    fh2::g_forceAuto = forceAuto;
    fh2::applyCarBytes(car);
    fh2::g_absMode = 0; fh2::g_tcMode = 0; fh2::g_stmMode = 0; fh2::g_clutchMode = 0;
    fh2::g_forceAuto = false;
}
