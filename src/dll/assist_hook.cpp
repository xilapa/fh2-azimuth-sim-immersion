// Replaces the ABS / traction control / stability control getters of LocalPlayerChoices so that
// each car gets the assists its real counterpart had (data/car_assists.csv), or a per-car override.
#include "assist_hook.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include "../common/car_db.h"
#include "log.h"

namespace fh2 {
namespace {

constexpr uintptr_t kVtableRva[2]  = {0x2c5bfc8, 0x2cf39c0};   // LocalPlayerChoices, TRefCountedObject<ForzaLocalPlayerChoices>
constexpr size_t    kAbsSlotOff    = 0xd8;                      // braking mode: 0 assisted, 1 ABS on, 2 ABS off
constexpr size_t    kStmSlotOff    = 0xe8;                      // stability control: 0 on, 1 off
constexpr size_t    kTcSlotOff     = 0xf0;                      // traction control: 0 on, 1 off
constexpr uintptr_t kAbsGetterRva  = 0xfbcb50;
constexpr uintptr_t kStmGetterRva  = 0xfbcb80;
constexpr uintptr_t kTcGetterRva   = 0x1391660;
constexpr size_t    kAbsFieldOff   = 0x2b1;
constexpr size_t    kStmFieldOff   = 0x2b3;
constexpr size_t    kTcFieldOff    = 0x2b4;

uintptr_t g_base = 0;
bool      g_absOn = false, g_tcOn = false, g_tieStm = false;
AssistMode g_optional = AssistMode::Game;
std::vector<CarAssistOverride> g_overrides;

std::atomic<bool>     g_installed{false};
std::atomic<int64_t>  g_carId{-1};
std::atomic<uint64_t> g_carTick{0};
std::atomic<const char*> g_carName{nullptr};
std::atomic<int>      g_absMode{0}, g_tcMode{0};              // AssistMode of the current car
std::atomic<bool>     g_loggedAbs{false}, g_loggedTc{false}, g_loggedStm{false};
std::atomic<uint32_t> g_callsAbs{0}, g_callsTc{0}, g_callsStm{0};

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

const char* modeName(AssistMode m) { return m == AssistMode::On ? "on" : m == AssistMode::Off ? "off" : "game"; }

// ---- getters (same signature as the originals: movzx eax, byte [rcx+X]) --------------------------
uint8_t absGetter(void* self) {
    uint8_t real = *(reinterpret_cast<uint8_t*>(self) + kAbsFieldOff);
    g_callsAbs.fetch_add(1, std::memory_order_relaxed);
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

uint8_t tcLike(void* self, size_t field, std::atomic<bool>& logged, const char* what) {
    uint8_t real = *(reinterpret_cast<uint8_t*>(self) + field);
    uint8_t out = real;
    switch (static_cast<AssistMode>(g_tcMode.load(std::memory_order_relaxed))) {
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

uint8_t tcGetter(void* self) {
    g_callsTc.fetch_add(1, std::memory_order_relaxed);
    return tcLike(self, kTcFieldOff, g_loggedTc, "TC");
}

uint8_t stmGetter(void* self) {
    g_callsStm.fetch_add(1, std::memory_order_relaxed);
    return tcLike(self, kStmFieldOff, g_loggedStm, "stability control");
}

// ---- decision per car ----------------------------------------------------------------------------
void decide(uint32_t id, const CarInfo* car) {
    AssistMode a = AssistMode::Game, t = AssistMode::Game;
    if (car) {
        if (g_absOn) a = carValue(car->abs);
        if (g_tcOn)  t = carValue(car->tc);
    }
    for (const auto& o : g_overrides) {
        if (o.id != id) continue;
        if (o.absSet && g_absOn) a = o.abs;
        if (o.tcSet && g_tcOn)   t = o.tc;
        break;
    }
    g_absMode = static_cast<int>(a);
    g_tcMode  = static_cast<int>(t);
}

DWORD WINAPI diagThread(void*) {
    for (;;) {
        Sleep(5000);
        int64_t id = g_carId.load();
        LOG("assists diag: car %u getters abs %u tc %u stm %u (calls since last)", id < 0 ? 0u : (unsigned)id,
            g_callsAbs.exchange(0), g_callsTc.exchange(0), g_callsStm.exchange(0));
    }
}

bool patchVtable(int which, void* absFn, void* stmFn, void* tcFn) {
    uint64_t* vt = reinterpret_cast<uint64_t*>(g_base + kVtableRva[which]);
    uint64_t* sAbs = vt + kAbsSlotOff / 8;
    uint64_t* sStm = vt + kStmSlotOff / 8;
    uint64_t* sTc  = vt + kTcSlotOff / 8;
    if (*sAbs != g_base + kAbsGetterRva || *sStm != g_base + kStmGetterRva || *sTc != g_base + kTcGetterRva) {
        LOG("assists: vtable %d mismatch; unchanged", which);
        return false;
    }
    struct Job { uint64_t* slot; void* fn; };
    const Job jobs[3] = {{sAbs, absFn}, {sStm, stmFn}, {sTc, tcFn}};
    for (const Job& j : jobs) {
        if (!j.fn) continue;
        DWORD prot;
        if (!VirtualProtect(j.slot, 8, PAGE_READWRITE, &prot)) {
            LOG("assists: vtable %d VirtualProtect failed %lu; slot unchanged", which, GetLastError());
            continue;
        }
        *j.slot = reinterpret_cast<uint64_t>(j.fn);
        VirtualProtect(j.slot, 8, prot, &prot);
    }
    return true;
}

} // namespace

void AssistHook_SetPlayerCar(uint32_t ordinal) {
    if (!g_installed.load(std::memory_order_acquire)) return;
    g_carTick.store(GetTickCount64(), std::memory_order_relaxed);
    if (g_carId.load(std::memory_order_relaxed) == static_cast<int64_t>(ordinal)) return;
    const CarInfo* car = FindCar(ordinal);
    decide(ordinal, car);
    g_carName.store(car ? car->name : nullptr);
    g_loggedAbs = false; g_loggedTc = false; g_loggedStm = false;
    g_carId.store(static_cast<int64_t>(ordinal));
    LOG("car: %u %s (ABS %s, TC %s)", ordinal, car ? car->name : "unknown car", car ? word(car->abs) : "unknown",
        car ? word(car->tc) : "unknown");
}

void AssistHook_Install(uintptr_t base, const Config& cfg) {
    g_absOn = cfg.sim.realisticAbs;
    g_tcOn  = cfg.sim.realisticTc;
    if (!g_absOn && !g_tcOn) return;
    g_base = base;
    g_tieStm = cfg.sim.tieStabilityToTc;
    g_optional = cfg.sim.optionalAssists;
    g_overrides = cfg.sim.carOverrides;
    void* absFn = g_absOn ? reinterpret_cast<void*>(&absGetter) : nullptr;
    void* tcFn  = g_tcOn ? reinterpret_cast<void*>(&tcGetter) : nullptr;
    void* stmFn = g_tieStm ? reinterpret_cast<void*>(&stmGetter) : nullptr;
    int ok = 0;
    for (int i = 0; i < 2; ++i) if (patchVtable(i, absFn, stmFn, tcFn)) ++ok;
    if (!ok) { LOG("assists: no vtable verified; realistic assists inactive"); return; }
    LOG("assists: getters installed in %d vtable(s) (ABS %d, TC %d, stability %d, optional %s)", ok, g_absOn, g_tcOn,
        g_tieStm, modeName(g_optional));
    g_installed.store(true, std::memory_order_release);
    if (cfg.diagnostics)
        if (HANDLE t = CreateThread(nullptr, 0, diagThread, nullptr, 0, nullptr)) CloseHandle(t);
}

} // namespace fh2
