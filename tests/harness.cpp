// SPDX-License-Identifier: MIT
// Integration test (runs under Windows or Wine):
//   1. maps the real forza_durango_release_final.exe into memory the way a loader would,
//   2. loads FH2SimImmersion.dll, which must find, verify and patch the image,
//   3. sends OpenTrack UDP packets,
//   4. calls the game's own CameraLayer::EvaluateInput on fake layers and checks results.
// usage: harness.exe <path-to-game-exe> <path-to-FH2SimImmersion.dll> [--deep]
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "../src/dll/wheel_ffb.h"
#include "../src/dll/wheel_device.h"
#include "../src/common/wheel_pick.h"
#include "../src/common/car_db.h"

#ifdef _MSC_VER
#define MSABI
#else
#define MSABI __attribute__((ms_abi))
#endif

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { printf("  ok   "); } else { printf("  FAIL "); ++failures; } printf(__VA_ARGS__); printf("\n"); } while (0)

static uint8_t* mapImage(const wchar_t* path, size_t pad = 0) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD size = GetFileSize(f, nullptr);
    std::vector<uint8_t> file(size);
    DWORD got; ReadFile(f, file.data(), size, &got, nullptr); CloseHandle(f);
    auto dos = (IMAGE_DOS_HEADER*)file.data();
    auto nt = (IMAGE_NT_HEADERS64*)(file.data() + dos->e_lfanew);
    // pad > 0 places the image inside a larger allocation, which defeats the plugin's
    // fast path and exercises its deep memory scan.
    uint8_t* alloc = (uint8_t*)VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage + pad, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!alloc) return nullptr;
    uint8_t* base = alloc + pad;
    memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].SizeOfRawData)
            memcpy(base + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData,
                   std::min(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData));
    // relocations
    uint64_t delta = (uint64_t)base - nt->OptionalHeader.ImageBase;
    auto& rd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint8_t* r = base + rd.VirtualAddress; uint8_t* rend = r + rd.Size;
    while (r < rend) {
        auto blk = (IMAGE_BASE_RELOCATION*)r;
        if (!blk->SizeOfBlock) break;
        auto ent = (WORD*)(blk + 1);
        int n = (blk->SizeOfBlock - sizeof(*blk)) / 2;
        for (int i = 0; i < n; ++i)
            if ((ent[i] >> 12) == IMAGE_REL_BASED_DIR64)
                *(uint64_t*)(base + blk->VirtualAddress + (ent[i] & 0xFFF)) += delta;
        r += blk->SizeOfBlock;
    }
    return base;
}

// Fake car object: EvaluateInput calls car->vtbl[0x70/8]() twice and needs non-null.
struct FakeCar;
static void* MSABI carSlot14(FakeCar* self) { return self; }
static float MSABI otherModFov(void*) { return 50.0f; }   // stands for another mod's field-of-view hook
static int g_otherCarUpdateCalls = 0;
static void MSABI otherModCarUpdate(void*, float, float, int) { ++g_otherCarUpdateCalls; }   // another mod's car update hook
static void* g_carVtbl[32];
struct FakeCar { void** vtbl; uint8_t pad[0x400]; };

typedef void (MSABI *EvalFn)(void* layer, void* car, void* r8, float dt);

struct alignas(16) Layer { uint8_t b[0x200]; };

static Layer makeLayer(int inputType, int outputType, float paramX) {
    Layer L; memset(&L, 0, sizeof(L));
    *(int*)(L.b + 0x48) = inputType;
    *(int*)(L.b + 0x50) = outputType;
    *(float*)(L.b + 0x58) = -25.0f;
    *(float*)(L.b + 0x5C) = 25.0f;
    *(int*)(L.b + 0xAC) = 4;          // FivePoint
    *(float*)(L.b + 0xE0) = paramX;
    return L;
}
static float f(const Layer& L, int off) { return *(const float*)(L.b + off); }
static int   i32(const Layer& L, int off) { return *(const int*)(L.b + off); }

static void sendPose(SOCKET s, double x, double y, double z, double yaw, double pitch, double roll) {
    double p[6] = {x, y, z, yaw, pitch, roll};
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(4242);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    sendto(s, (const char*)p, sizeof(p), 0, (sockaddr*)&a, sizeof(a));
}


// ---------------------------------------------------------------------------------------------
// Emulation of what Azimuth provides for the wheel tests: an activation-factory import that
// returns a statics object whose controller list is empty.
// ---------------------------------------------------------------------------------------------
static const GUID kIidUnknownH = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIidStaticsH = {0x7400624f, 0x144b, 0x47fc, {0x9f, 0x3b, 0x1d, 0xca, 0xfb, 0x48, 0x28, 0x73}};
static const GUID kIidGamepadH = {0x082e402c, 0x07df, 0x45e1, {0xa5, 0xab, 0xa3, 0x12, 0x7a, 0xf1, 0x97, 0xb5}};
static const GUID kIidWheelH   = {0x646979cf, 0x6b71, 0x4e96, {0x8d, 0xf9, 0x59, 0xe3, 0x98, 0xd7, 0x42, 0x0c}};

struct HObj { void** vtbl; };
static HRESULT MSABI h_QI(void* self, const GUID* iid, void** out) {
    if (!out) return E_POINTER;
    if (IsEqualGUID(*iid, kIidUnknownH) || IsEqualGUID(*iid, kIidStaticsH)) { *out = self; return S_OK; }
    *out = nullptr;
    return E_NOINTERFACE;
}
static ULONG MSABI h_AddRef(void*) { return 2; }
static ULONG MSABI h_Release(void*) { return 1; }
static HRESULT MSABI h_Stub(void*) { return S_OK; }
// Two real controllers in the fake list: a plain pad (does not answer IWheel) and a wheel that Azimuth
// "provides" (answers IWheel). The plugin must hide the second one.
static HRESULT MSABI h_CtlQI(void* self, const GUID* iid, void** out);
static HRESULT MSABI h_GetUserNull(void*, void** out) { if (out) *out = nullptr; return S_OK; }
static void* g_hPadVtbl[16], *g_hWheelVtbl[16];
static HObj g_hPad = {g_hPadVtbl}, g_hWheelCtl = {g_hWheelVtbl};
static HRESULT MSABI h_ViewGetAt(void*, uint32_t i, void** out) {
    if (out) *out = nullptr;
    if (i > 1) return (HRESULT)0x8000000BL;
    if (out) *out = i == 0 ? (void*)&g_hPad : (void*)&g_hWheelCtl;
    return S_OK;
}
static HRESULT MSABI h_ViewSize(void*, uint32_t* n) { *n = 2; return S_OK; }
static HRESULT MSABI h_ViewIndexOf(void*, void* item, uint32_t* i, uint8_t* f) {
    *i = item == (void*)&g_hWheelCtl ? 1 : 0;
    *f = item == (void*)&g_hPad || item == (void*)&g_hWheelCtl;
    return S_OK;
}
static HRESULT MSABI h_ViewGetMany(void*, uint32_t, uint32_t, void**, uint32_t* a) { *a = 0; return S_OK; }
static HRESULT MSABI h_CtlQI(void* self, const GUID* iid, void** out) {
    if (!out) return E_POINTER;
    if (IsEqualGUID(*iid, kIidUnknownH) || (self == (void*)&g_hWheelCtl && IsEqualGUID(*iid, kIidWheelH))) { *out = self; return S_OK; }
    *out = nullptr;
    return E_NOINTERFACE;
}
static void* g_hViewVtbl[10] = {(void*)h_QI, (void*)h_AddRef, (void*)h_Release, (void*)h_Stub, (void*)h_Stub,
                                (void*)h_Stub, (void*)h_ViewGetAt, (void*)h_ViewSize, (void*)h_ViewIndexOf,
                                (void*)h_ViewGetMany};
static HObj g_hView = {g_hViewVtbl};
static HRESULT MSABI h_GetControllers(void*, void** out) { *out = &g_hView; return S_OK; }
static void* g_hStaticsVtbl[16];
static HObj g_hStatics = {g_hStaticsVtbl};
static int g_unexpectedClass = 0;
static int g_userClassRequests = 0;
static HRESULT MSABI fakeGetActivationFactory(const wchar_t* name, const GUID* iid, void** out) {
    if (wcscmp(name, L"Windows.Xbox.Input.Controller") == 0 && IsEqualGUID(*iid, kIidStaticsH)) {
        *out = &g_hStatics;
        return S_OK;
    }
    if (wcscmp(name, L"Windows.Xbox.System.User") == 0) ++g_userClassRequests;   // system user fallback probe
    else ++g_unexpectedClass;
    if (out) *out = nullptr;
    return (HRESULT)0x80040154L;   // REGDB_E_CLASSNOTREG
}

typedef HRESULT (MSABI *Fn0)(void*);
typedef HRESULT (MSABI *FnU32Out)(void*, uint32_t*);
typedef HRESULT (MSABI *FnU64Out)(void*, uint64_t*);
typedef HRESULT (MSABI *FnF32Out)(void*, float*);
typedef HRESULT (MSABI *FnByteOut)(void*, uint8_t*);
typedef HRESULT (MSABI *FnPtrOut)(void*, void**);
typedef HRESULT (MSABI *FnQI)(void*, const GUID*, void**);
typedef HRESULT (MSABI *FnGetAt)(void*, uint32_t, void**);
typedef HRESULT (MSABI *FnSetRot)(void*, uint32_t);
typedef HRESULT (MSABI *FnSetVarF)(void*, void*, float);
typedef HRESULT (MSABI *FnEquation)(void*, uint8_t, void*, uint32_t, uint32_t*);
typedef void* (MSABI *FnWrapper)();
typedef HRESULT (WINAPI *FnWCS)(const wchar_t*, uint32_t, void**);
typedef const wchar_t* (WINAPI *FnRawBuf)(void*, uint32_t*);

static void** vt(void* o) { return *(void***)o; }
static void* slotOf(void* o, int i) { return vt(o)[i]; }

static void wheelTests(uint8_t* base, const uint8_t origFrame[5], FnWCS createString) {
    printf("virtual wheel:\n");
    bool hooked = false;
    for (int i = 0; i < 150 && !hooked; ++i) { hooked = memcmp(base + 0x226893, origFrame, 5) != 0; if (!hooked) Sleep(100); }
    CHECK(hooked, "frame hook call site re-pointed");
    CHECK(base[0x226893] == 0xE8, "frame hook keeps the call opcode");
    CHECK(*(void**)(base + 0x1dd1330) != (void*)&fakeGetActivationFactory, "activation import now points at the hook");

    void* view = ((FnWrapper)(base + 0x1078b80))();
    CHECK(view != nullptr, "game wrapper returned a controller list (%p)", view);
    if (!view) return;
    uint32_t size = 99;
    ((FnU32Out)slotOf(view, 7))(view, &size);
    CHECK(size == 2, "Azimuth's wheel hidden: list size ==real count (2) - 1 + 1 (got %u)", size);
    {
        bool sawHarnessWheel = false;
        for (uint32_t i = 0; i < size; ++i) {
            void* c = nullptr;
            if (SUCCEEDED(((FnGetAt)slotOf(view, 6))(view, i, &c)) && c == (void*)&g_hWheelCtl) sawHarnessWheel = true;
        }
        CHECK(!sawHarnessWheel, "GetAt never returns the harness's (Azimuth's) wheel");
        void* c0 = nullptr; void* w0 = nullptr;
        ((FnGetAt)slotOf(view, 6))(view, 0, &c0);
        CHECK(c0 == (void*)&g_hPad && FAILED(((FnQI)slotOf(c0, 0))(c0, &kIidWheelH, &w0)), "GetAt(0) is the real pad");
        void* c2 = (void*)1;
        CHECK(((FnGetAt)slotOf(view, 6))(view, 2, &c2) == (HRESULT)0x8000000BL && c2 == nullptr, "GetAt(2) out of bounds");
        uint32_t idx = 9; uint8_t found = 0;
        typedef HRESULT (MSABI *FnIndexOf)(void*, void*, uint32_t*, uint8_t*);
        ((FnIndexOf)slotOf(view, 8))(view, &g_hWheelCtl, &idx, &found);
        CHECK(!found, "IndexOf(Azimuth's wheel) -> not found");
        ((FnIndexOf)slotOf(view, 8))(view, &g_hPad, &idx, &found);
        CHECK(found && idx == 0, "IndexOf(pad) == 0");
    }
    void* ctrl = nullptr;
    HRESULT hr = ((FnGetAt)slotOf(view, 6))(view, 1, &ctrl);
    CHECK(hr == S_OK && ctrl && ctrl != (void*)&g_hWheelCtl, "GetAt(1) is the plugin's wheel");
    if (!ctrl) return;
    void* gp = (void*)1;
    hr = ((FnQI)slotOf(ctrl, 0))(ctrl, &kIidGamepadH, &gp);
    CHECK(FAILED(hr) && gp == nullptr, "QI(IGamepad) fails (0x%08lx)", (unsigned long)hr);
    void* wheel = nullptr;
    hr = ((FnQI)slotOf(ctrl, 0))(ctrl, &kIidWheelH, &wheel);
    CHECK(hr == S_OK && wheel, "QI(IWheel) succeeds");
    if (!wheel) return;
    uint64_t id = 0;
    ((FnU64Out)slotOf(ctrl, 6))(ctrl, &id);
    CHECK(id == 0x4648325748454C31ull, "controller id constant (%llx)", (unsigned long long)id);
    void* user = (void*)1;
    hr = ((FnPtrOut)slotOf(ctrl, 8))(ctrl, &user);
    CHECK(hr == S_OK && user == nullptr, "get_User -> null with no real controller");

    uint32_t vp = 0;
    ((FnU32Out)slotOf(wheel, 10))(wheel, &vp);
    CHECK(vp == 0x046DC261, "IWheel slot 10 vendor/product 0x%08x", vp);

    void* reading = nullptr;
    hr = ((FnPtrOut)slotOf(wheel, 12))(wheel, &reading);
    CHECK(hr == S_OK && reading, "get_CurrentReading");
    if (!reading) return;
    float steer = -9, thr = -9;
    uint8_t a = 0;
    uint64_t ts = 0;
    ((FnF32Out)slotOf(reading, 16))(reading, &steer);
    ((FnF32Out)slotOf(reading, 18))(reading, &thr);
    ((FnByteOut)slotOf(reading, 6))(reading, &a);
    ((FnU64Out)slotOf(reading, 33))(reading, &ts);
    CHECK(std::fabs(steer - 0.25f) < 1e-5f, "reading steering 0.25 (got %f)", steer);
    CHECK(std::fabs(thr - 0.5f) < 1e-5f, "reading throttle 0.5 (got %f)", thr);
    for (int i = 0; i < 60 && a != 1; ++i) {   // test mode taps A every second: wait for a reading with it down
        Sleep(25);
        void* r2 = nullptr;
        if (((FnPtrOut)slotOf(wheel, 12))(wheel, &r2) == S_OK && r2) ((FnByteOut)slotOf(r2, 6))(r2, &a);
    }
    CHECK(a == 1, "reading A pressed");
    CHECK(ts != 0, "reading timestamp non-zero");
    ((Fn0)slotOf(reading, 2))(reading);

    hr = ((FnSetRot)slotOf(wheel, 14))(wheel, 450);
    CHECK(hr == S_OK, "SetRotation(450)");
    reading = nullptr;
    ((FnPtrOut)slotOf(wheel, 12))(wheel, &reading);
    steer = -9;
    if (reading) { ((FnF32Out)slotOf(reading, 16))(reading, &steer); ((Fn0)slotOf(reading, 2))(reading); }
    CHECK(std::fabs(steer - 0.5f) < 1e-5f, "steering 0.5 after SetRotation(450) (got %f)", steer);

    void* hs = nullptr;
    createString(L"Torque", 6, &hs);
    hr = ((FnSetVarF)slotOf(wheel, 38))(wheel, hs, 0.3f);
    CHECK(hr == S_OK, "SetVariableFloat returns S_OK");
    uint32_t outLen = 0xDEADBEEF;
    hr = ((FnEquation)slotOf(wheel, 26))(wheel, 1, hs, 0, &outLen);
    CHECK(hr == S_OK && outLen == 0, "SetEquation: outLen == 0 (got %u)", outLen);

    void* cfg = nullptr;
    hr = ((FnPtrOut)slotOf(wheel, 13))(wheel, &cfg);
    CHECK(hr == S_OK && cfg, "get_StaticConfiguration");
    if (cfg) {
        uint32_t maxRot = 0;
        ((FnU32Out)slotOf(cfg, 12))(cfg, &maxRot);
        CHECK(maxRot == 900, "StaticConfig max rotation 900 (got %u)", maxRot);
        ((Fn0)slotOf(cfg, 2))(cfg);
    }
    CHECK(g_userClassRequests > 0, "empty controller list: plugin asked for the system user and survived its failure (%d)", g_userClassRequests);
    CHECK(g_unexpectedClass == 0, "no unexpected activation requests (%d)", g_unexpectedClass);
}

// Calls fn(a, b) with rsi = rsiValue (the game's FFB call site has the car vector element in rsi).
// rsi is callee-saved in the Windows x64 ABI, so the helper saves and restores it.
extern "C" void callWithRsiAsm(void* fn, void* a, void* b, void* rsiValue);
asm(R"(
.text
.globl callWithRsiAsm
callWithRsiAsm:
    push %rsi
    sub $32, %rsp
    mov %r9, %rsi
    mov %rcx, %rax
    mov %rdx, %rcx
    mov %r8, %rdx
    call *%rax
    add $32, %rsp
    pop %rsi
    ret
)");
static void callWithRsi(void* fn, uint8_t* slot, const float* in, void* rsiValue) {
    callWithRsiAsm(fn, slot, (void*)in, rsiValue);
}

typedef bool (*FnLastFfb)(fh2::GameFfbInput*);
typedef void (MSABI *FnUpdateFfb)(uint8_t*, const float*);
typedef float (MSABI *FnGetFov)(void*);

static void ffbTests(uint8_t* base, HMODULE plugin) {
    printf("game FFB input hook:\n");
    const uintptr_t kSite = 0x2fe134, kOrigTarget = 0x10743c0;
    bool hooked = false;
    for (int i = 0; i < 150 && !hooked; ++i) { hooked = base[kSite + 1] != 0x87 || base[kSite + 2] != 0x62; if (!hooked) Sleep(100); }
    int32_t rel; memcpy(&rel, base + kSite + 1, 4);
    uint8_t* target = base + kSite + 5 + rel;
    CHECK(base[kSite] == 0xE8, "call opcode preserved");
    CHECK(target != base + kOrigTarget && hooked, "call site no longer points at UpdateFFB (%p)", (void*)target);
    FnLastFfb last = (FnLastFfb)GetProcAddress(plugin, "FH2SI_TestLastGameFfbInput");
    CHECK(last != nullptr, "plugin test accessor exported");
    if (!hooked || !last) return;

    alignas(16) static uint8_t slot[0x1400];
    alignas(16) static float driver[32];
    memset(slot, 0, sizeof(slot)); memset(driver, 0, sizeof(driver));
    *(float**)(slot + 0x10c0) = driver;
    driver[0] = 1.0f;
    driver[0x28 / 4] = 0.42f; driver[0x2c / 4] = 1.0f; driver[0x48 / 4] = 0.5f;
    driver[0x54 / 4] = 0.32f; driver[0x58 / 4] = 0.55f; driver[0x5c / 4] = 1.0f; driver[0x60 / 4] = 0.0f;
    driver[0x64 / 4] = 2.0f; driver[0x68 / 4] = 15.0f; driver[0x6c / 4] = 0.15f; driver[0x70 / 4] = 1.0f;
    const float in[3] = {0.5f, 20.0f, 0.1f};
    float inCopy[3] = {in[0], in[1], in[2]};
    callWithRsi((void*)target, slot, inCopy, (void*)(uintptr_t)1);   // garbage rsi must be tolerated
    CHECK(std::fabs(driver[1] - 0.21f) < 1e-4f, "original UpdateFFB ran: torque output %f", driver[1]);
    CHECK(memcmp(in, inCopy, sizeof(in)) == 0, "hook left the input untouched");
    fh2::GameFfbInput g;
    bool got = last(&g);
    CHECK(got, "plugin captured the input");
    CHECK(std::fabs(g.rawTorque - 0.5f) < 1e-6f && std::fabs(g.speed - 20.0f) < 1e-6f && std::fabs(g.slip - 0.1f) < 1e-6f &&
          std::fabs(g.gain - 1.0f) < 1e-6f, "captured raw %f speed %f slip %f gain %f", g.rawTorque, g.speed, g.slip, g.gain);
    CHECK(std::fabs(g.dynScale - 0.42f) < 1e-6f && std::fabs(g.dynMax - 1.0f) < 1e-6f, "captured driver parameters");

    // fake car: the plugin reads the car id from it
    alignas(16) static uint8_t carBuf[0x4c10];
    memset(carBuf, 0, sizeof(carBuf));
    static void* carElemFake;
    carElemFake = carBuf;
    float inCopy2[3] = {in[0], in[1], in[2]};
    callWithRsi((void*)target, slot, inCopy2, &carElemFake);
    CHECK(last(&g), "plugin captured the input with a fake car");

    // steering lock per car (RealisticSteeringLock=1, WheelRange 900, game rotation 450, TestMode steering 0.25)
    printf("steering lock:\n");
    auto readSteer = [&]() -> float {
        float st = -9;
        void* view = ((FnWrapper)(base + 0x1078b80))();
        void* ctrl = nullptr; void* wheel = nullptr; void* reading = nullptr;
        if (view && SUCCEEDED(((FnGetAt)slotOf(view, 6))(view, 1, &ctrl)) && ctrl &&
            SUCCEEDED(((FnQI)slotOf(ctrl, 0))(ctrl, &kIidWheelH, &wheel)) && wheel &&
            SUCCEEDED(((FnPtrOut)slotOf(wheel, 12))(wheel, &reading)) && reading) {
            ((FnF32Out)slotOf(reading, 16))(reading, &st);
            ((Fn0)slotOf(reading, 2))(reading);
        }
        return st;
    };
    auto setCar = [&](uint32_t id) {
        *(uint32_t*)(carBuf + 0x118) = id;
        float inCopy3[3] = {in[0], in[1], in[2]};
        callWithRsi((void*)target, slot, inCopy3, &carElemFake);
    };
    const fh2::CarInfo *low = nullptr, *high = nullptr;
    for (size_t i = 0; i < fh2::kCarCount; ++i) {
        if (!low && fh2::kCars[i].lockDeg < 850) low = &fh2::kCars[i];
        if (!high && fh2::kCars[i].lockDeg > 950) high = &fh2::kCars[i];
    }
    CHECK(low && high, "car list has cars with a lock below 850 and above 950");
    if (low && high) {
        setCar(low->id);
        float expect = 0.25f * 900.0f / (float)low->lockDeg;
        float st = readSteer();
        CHECK(std::fabs(st - expect) < 1e-4f, "car %u (lock %u): steering %f, expected %f", low->id, (unsigned)low->lockDeg, st, expect);
        setCar(high->id);
        st = readSteer();
        CHECK(std::fabs(st - 0.25f) < 1e-4f, "car %u (lock %u, capped at WheelRange): steering %f, expected 0.25", high->id, (unsigned)high->lockDeg, st);
        setCar(0xFFFFF);
        st = readSteer();
        CHECK(std::fabs(st - 0.5f) < 1e-4f, "unknown car: the game's rotation (450) -> steering %f, expected 0.5", st);
    }
}

typedef void (*FnTestCockpit)(uint8_t*, float);
typedef void (*FnTestCarAssist)(uint8_t*, int, int, int, int, bool, bool);
typedef void* (*FnTestRig)(uint8_t*);

static void cockpitTests(uint8_t* base, HMODULE plugin, uint64_t slotOrig, const std::wstring& logPath) {
    printf("cockpit wheel and driver:\n");
    const uintptr_t kSlot = 0x2c5cc00, kCarUpdate = 0x2a9400, kSetDriver = 0xf75500;
    CHECK(slotOrig == (uint64_t)(base + kCarUpdate), "car update slot held the original before loading");
    uint64_t now = *(uint64_t*)(base + kSlot);
    CHECK(now != slotOrig && now != (uint64_t)&otherModCarUpdate, "car update slot re-pointed over another mod's hook");
    ((void (MSABI *)(void*, float, float, int))now)(nullptr, 0.0f, 0.0f, 0);
    CHECK(g_otherCarUpdateCalls == 1, "car update: another mod's hook still runs (%d)", g_otherCarUpdateCalls);
    static const uint8_t expect[10] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
    CHECK(memcmp(base + kSetDriver, expect, sizeof(expect)) == 0, "Car_SetDriverVisible first 10 bytes");
    {
        FILE* f = _wfopen(logPath.c_str(), L"rb");
        std::string text;
        if (f) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof(b), f)) > 0) text.append(b, n); fclose(f); }
        CHECK(text.find("cockpit: driver hide ready") != std::string::npos, "log: cockpit: driver hide ready");
        CHECK(text.find("cockpit: car update hook installed") != std::string::npos, "log: car update hook installed");
        CHECK(text.find("cockpit: wheel spindle hook installed") != std::string::npos, "log: wheel spindle hook installed");
    }
    FnTestCockpit fn = (FnTestCockpit)GetProcAddress(plugin, "FH2SI_TestCockpitWheel");
    CHECK(fn != nullptr, "FH2SI_TestCockpitWheel exported");
    if (!fn) return;
    static uint8_t car[0x5000];
    memset(car, 0, sizeof(car));
    fn(car, 0.5f);
    float a; memcpy(&a, car + 0x4f30, 4);
    CHECK(std::fabs(a - 3.92699f) < 1e-4f, "steering 0.5 at lock 900 -> %f rad (3.92699)", a);
    const float one = 1.0f;
    memcpy(car + 0x4f38, &one, 4);
    fn(car, 0.5f);
    memcpy(&a, car + 0x4f38, 4);
    CHECK(a == 0.0f, "animation offset (0x4f38) cleared: %f", a);
    fn(car, -1.0f);
    memcpy(&a, car + 0x4f30, 4);
    CHECK(std::fabs(a + 7.85398f) < 1e-4f, "steering -1 -> %f rad (-7.85398)", a);
    car[0x4f11] = 1;
    float before = a;
    fn(car, 0.25f);
    memcpy(&a, car + 0x4f30, 4);
    CHECK(a == before, "frozen car (0x4f11 != 0): angle untouched");

    // The car holds a deferred wrapper of the presentation; the rig is only found through it.
    FnTestRig rigOf = (FnTestRig)GetProcAddress(plugin, "FH2SI_TestPlayerRig");
    CHECK(rigOf != nullptr, "FH2SI_TestPlayerRig exported");
    if (!rigOf) return;
    CHECK(*(uint64_t*)(base + 0x2ebe7e0 + 0x1c8) == (uint64_t)(base + 0x134fb20), "deferred presentation vtable matches this build");
    CHECK(*(uint64_t*)(base + 0x2ec1450 + 0x1c8) == (uint64_t)(base + 0x13ade90), "presentation vtable matches this build");
    static uint8_t pres[0x12a0];
    void* const rig = pres + 0x100;
    *(uint64_t*)pres = (uint64_t)(base + 0x2ec1450);
    memcpy(pres + 0x1298, &rig, 8);
    uint64_t wrapper[2] = {(uint64_t)(base + 0x2ebe7e0), (uint64_t)pres};
    auto rigThrough = [&](void* p) { memcpy(car + 0x4d38, &p, 8); return rigOf(car); };
    CHECK(rigThrough(wrapper) == rig, "rig found through the deferred wrapper");
    wrapper[0] = (uint64_t)(base + 0x2ebef68);
    CHECK(rigThrough(wrapper) == rig, "rig found through the ref-counted deferred wrapper");
    CHECK(rigThrough(pres) == rig, "rig found on a plain presentation");
    wrapper[1] = 0;
    CHECK(rigThrough(wrapper) == nullptr, "no rig when the wrapper is empty");
    wrapper[0] = (uint64_t)(base + 0x2eb8570);
    wrapper[1] = (uint64_t)pres;
    CHECK(rigThrough(wrapper) == nullptr, "no rig behind an unknown presentation type");
    CHECK(rigThrough(nullptr) == nullptr, "no rig without a driver model");

    FnTestCarAssist assist = (FnTestCarAssist)GetProcAddress(plugin, "FH2SI_TestCarAssistBytes");
    CHECK(assist != nullptr, "FH2SI_TestCarAssistBytes exported");
    if (!assist) return;
    enum { Game = 0, On = 1, Off = 2 };      // AssistMode
    static uint8_t buf[0x5000];
    auto run = [&](uint8_t autobrake, uint8_t abs, uint8_t tc, uint8_t stm, int32_t shift, int absMode, int tcMode, int stmMode, int clutchMode,
                   bool forceAuto, int times = 1) {
        memset(buf, 0, sizeof(buf));
        buf[0x328] = autobrake; buf[0x329] = abs; buf[0x32a] = tc; buf[0x32b] = stm;
        memcpy(buf + 0x330, &shift, 4);
        for (int i = 0; i < times; ++i) assist(buf, absMode, tcMode, stmMode, clutchMode, forceAuto, i == 0);
    };
    auto shiftOf = [&]() { int32_t v; memcpy(&v, buf + 0x330, 4); return v; };
    run(0, 1, 1, 1, 0, Off, Game, Game, Game, false);
    CHECK(buf[0x329] == 0 && buf[0x328] == 0, "ABS on in game settings, mode off: ABS %u autobrake %u", buf[0x329], buf[0x328]);
    run(0, 0, 1, 1, 0, On, Game, Game, Game, false);
    CHECK(buf[0x329] == 1 && buf[0x328] == 0, "ABS off in game settings, mode on: ABS %u autobrake %u", buf[0x329], buf[0x328]);
    run(1, 1, 1, 1, 0, On, Game, Game, Game, false, 2);
    CHECK(buf[0x329] == 1 && buf[0x328] == 1, "assisted braking, mode on, applied twice: ABS %u autobrake %u", buf[0x329], buf[0x328]);
    run(1, 1, 1, 1, 0, Game, Game, Game, Game, false);
    CHECK(buf[0x329] == 1 && buf[0x328] == 1, "assisted braking, mode game: ABS %u autobrake %u", buf[0x329], buf[0x328]);
    run(0, 0, 1, 0, 0, Game, Off, Off, Game, false);
    CHECK(buf[0x32a] == 0 && buf[0x32b] == 0, "TC and STM on in game settings, mode off: TC %u STM %u", buf[0x32a], buf[0x32b]);
    run(0, 0, 0, 0, 0, Game, On, On, Game, false);
    CHECK(buf[0x32a] == 1 && buf[0x32b] == 1, "TC and STM off in game settings, mode on: TC %u STM %u", buf[0x32a], buf[0x32b]);
    run(0, 0, 0, 0, 1, Game, Game, Game, On, false);
    CHECK(shiftOf() == 2, "manual, clutch on: shifting %d (2)", shiftOf());
    run(0, 0, 0, 0, 2, Game, Game, Game, Off, false);
    CHECK(shiftOf() == 1, "manual with clutch, clutch off: shifting %d (1)", shiftOf());
    run(0, 0, 0, 0, 0, Game, Game, Game, On, false);
    CHECK(shiftOf() == 0, "automatic, clutch on: shifting %d (0)", shiftOf());
    run(0, 0, 0, 0, 1, Game, Game, Game, Game, true);
    CHECK(shiftOf() == 0, "manual, forced automatic: shifting %d (0)", shiftOf());
    run(0, 0, 0, 0, 7, Game, Game, Game, On, true);
    CHECK(shiftOf() == 7, "unknown shifting value stays: %d (7)", shiftOf());

    // assist state the pause menu writes (car + 0x4abc ABS, 0x4ac0 TC, 0x4ac4 STM, 0x4ab0 shifting)
    auto i32At = [&](const uint8_t* p, size_t off) { int32_t v; memcpy(&v, p + off, 4); return v; };
    auto setI32 = [&](uint8_t* p, size_t off, int32_t v) { memcpy(p + off, &v, 4); };
    auto stateRun = [&](int32_t absV, int32_t tcV, int32_t stmV, int32_t shiftV, int absMode, int tcMode, int stmMode, int clutchMode, bool forceAuto) {
        memset(buf, 0, sizeof(buf));
        setI32(buf, 0x4abc, absV); setI32(buf, 0x4ac0, tcV); setI32(buf, 0x4ac4, stmV); setI32(buf, 0x4ab0, shiftV);
        assist(buf, absMode, tcMode, stmMode, clutchMode, forceAuto, true);
    };
    stateRun(1, 1, 1, 0, Off, Off, Off, Game, false);
    CHECK(i32At(buf, 0x4abc) == 0 && i32At(buf, 0x4ac0) == 0 && i32At(buf, 0x4ac4) == 0, "state fields, modes off: ABS %d TC %d STM %d (0)", i32At(buf, 0x4abc), i32At(buf, 0x4ac0), i32At(buf, 0x4ac4));
    stateRun(0, 0, 0, 0, On, On, On, Game, false);
    CHECK(i32At(buf, 0x4abc) == 1 && i32At(buf, 0x4ac0) == 1 && i32At(buf, 0x4ac4) == 1, "state fields, modes on: ABS %d TC %d STM %d (1)", i32At(buf, 0x4abc), i32At(buf, 0x4ac0), i32At(buf, 0x4ac4));
    stateRun(7, 7, 7, 0, Game, Game, Game, Game, false);
    CHECK(i32At(buf, 0x4abc) == 7 && i32At(buf, 0x4ac0) == 7 && i32At(buf, 0x4ac4) == 7, "state fields, modes game: ABS %d TC %d STM %d (7)", i32At(buf, 0x4abc), i32At(buf, 0x4ac0), i32At(buf, 0x4ac4));
    stateRun(0, 0, 0, 1, Game, Game, Game, On, false);
    CHECK(i32At(buf, 0x4ab0) == 2, "state shifting 1, clutch on: %d (2)", i32At(buf, 0x4ab0));
    stateRun(0, 0, 0, 2, Game, Game, Game, Off, false);
    CHECK(i32At(buf, 0x4ab0) == 1, "state shifting 2, clutch off: %d (1)", i32At(buf, 0x4ab0));
    stateRun(0, 0, 0, 1, Game, Game, Game, On, true);
    CHECK(i32At(buf, 0x4ab0) == 0, "state shifting 1, forced automatic: %d (0)", i32At(buf, 0x4ab0));
    stateRun(0, 0, 0, 1, Game, Game, Game, Game, false);
    CHECK(i32At(buf, 0x4ab0) == 1, "state shifting 1, clutch game: %d (1)", i32At(buf, 0x4ab0));

    // physics fields of a fake car dynamics object (the vtable sits at obj + 0x3a0, the car points there)
    static uint8_t obj[0x2000];
    auto dynRun = [&](uint64_t vtable, int absMode, int tcMode, int stmMode) {
        memset(buf, 0, sizeof(buf)); memset(obj, 0, sizeof(obj));
        memcpy(obj + 0x3a0, &vtable, 8);
        uint8_t* dyn = obj + 0x3a0;
        memcpy(buf + 0x4c08, &dyn, 8);
        setI32(obj, 0x1364, 7); setI32(obj, 0x1360, 7); setI32(obj, 0x135c, 7);
        assist(buf, absMode, tcMode, stmMode, Game, false, true);
    };
    dynRun((uint64_t)(base + 0x2eb4bb8), On, Off, On);
    CHECK(i32At(obj, 0x1364) == 1 && i32At(obj, 0x1360) == 0 && i32At(obj, 0x135c) == 1, "physics fields, ABS on TC off STM on: %d %d %d (1 0 1)", i32At(obj, 0x1364), i32At(obj, 0x1360), i32At(obj, 0x135c));
    dynRun((uint64_t)(base + 0x2eb5e18), Off, On, Off);
    CHECK(i32At(obj, 0x1364) == 0 && i32At(obj, 0x1360) == 1 && i32At(obj, 0x135c) == 0, "physics fields (refcounted vtable), ABS off TC on STM off: %d %d %d (0 1 0)", i32At(obj, 0x1364), i32At(obj, 0x1360), i32At(obj, 0x135c));
    dynRun((uint64_t)(base + 0x2eb4bb8), Game, Game, Game);
    CHECK(i32At(obj, 0x1364) == 7 && i32At(obj, 0x1360) == 7 && i32At(obj, 0x135c) == 7, "physics fields, modes game stay: %d %d %d (7)", i32At(obj, 0x1364), i32At(obj, 0x1360), i32At(obj, 0x135c));
    dynRun(0x1234, On, On, On);
    CHECK(i32At(obj, 0x1364) == 7 && i32At(obj, 0x1360) == 7 && i32At(obj, 0x135c) == 7, "physics fields, unknown dynamics vtable stay: %d %d %d (7)", i32At(obj, 0x1364), i32At(obj, 0x1360), i32At(obj, 0x135c));

    // The next car on the game's settings gets the game's values back from the same car object.
    memset(buf, 0, sizeof(buf));
    buf[0x328] = 1; buf[0x329] = 1; buf[0x32a] = 1; buf[0x32b] = 1;   // assisted braking, TC and STM on
    setI32(buf, 0x330, 1);                                             // manual
    setI32(buf, 0x4abc, 1); setI32(buf, 0x4ac0, 1); setI32(buf, 0x4ac4, 1); setI32(buf, 0x4ab0, 1);
    assist(buf, Off, Off, Off, On, false, true);
    CHECK(buf[0x329] == 0 && buf[0x32a] == 0 && buf[0x32b] == 0 && shiftOf() == 2 && i32At(buf, 0x4abc) == 0,
          "forced car: ABS %u TC %u STM %u shifting %d state ABS %d", buf[0x329], buf[0x32a], buf[0x32b], shiftOf(), i32At(buf, 0x4abc));
    assist(buf, Game, Game, Game, Game, false, false);
    CHECK(buf[0x328] == 1 && buf[0x329] == 1 && buf[0x32a] == 1 && buf[0x32b] == 1 && shiftOf() == 1 && i32At(buf, 0x4abc) == 1 &&
              i32At(buf, 0x4ac0) == 1 && i32At(buf, 0x4ac4) == 1 && i32At(buf, 0x4ab0) == 1,
          "game's values back after a forced car: autobrake %u ABS %u TC %u STM %u shifting %d state %d %d %d %d", buf[0x328], buf[0x329],
          buf[0x32a], buf[0x32b], shiftOf(), i32At(buf, 0x4abc), i32At(buf, 0x4ac0), i32At(buf, 0x4ac4), i32At(buf, 0x4ab0));
    buf[0x32a] = 0;                                                    // the game copies a new choice: TC off
    assist(buf, Game, Game, Game, Game, false, false);
    CHECK(buf[0x32a] == 0, "a new game value is followed: TC %u (0)", buf[0x32a]);

    // player car detection (state persists between steps; freshness is 1000 ms)
    typedef uint8_t* (*FnDetect)(uint8_t*, bool);
    FnDetect detect = (FnDetect)GetProcAddress(plugin, "FH2SI_TestDetectPlayer");
    CHECK(detect != nullptr, "FH2SI_TestDetectPlayer exported");
    if (!detect) return;
    static uint8_t carA[0x4f40], carB[0x4f40];
    memset(carA, 0, sizeof(carA)); memset(carB, 0, sizeof(carB));
    static void** g_fakeResult;
    struct FakeDriver { void** vtbl; };
    static void* table[0xb0 / 8];
    struct Getter { static void** get(void*) { return g_fakeResult; } };
    table[0xa8 / 8] = (void*)&Getter::get;
    static FakeDriver driver = {table};
    static void* pDriver = &driver;
    uint32_t idA = 1234, idB = 5678;
    memcpy(carA + 0x118, &idA, 4); memcpy(carB + 0x118, &idB, 4);
    static void* nullQ = nullptr, *setQ = (void*)1;
    Sleep(1100);
    CHECK(detect(carA, false) != carA, "player detection: car without a driver is not the player");
    memcpy(carA + 0x4d00, &pDriver, 8);
    g_fakeResult = nullptr;
    CHECK(detect(carA, false) != carA, "player detection: getter returning null is not the player");
    g_fakeResult = &nullQ;
    CHECK(detect(carA, false) != carA, "player detection: getter returning a null qword is not the player");
    g_fakeResult = &setQ;
    CHECK(detect(carA, false) == carA, "player detection: non-null qword makes car A the player");
    memcpy(carB + 0x4d00, &pDriver, 8);
    CHECK(detect(carB, false) == carA, "player detection: first match kept while fresh");
    Sleep(1100);
    CHECK(detect(carB, false) == carB, "player detection: old match expired, car B is the player");
    CHECK(detect(carA, true) == carA, "player detection: FFB site sets the player car");
    CHECK(detect(carB, false) == carA, "player detection: FFB authority keeps car A");
}

typedef uint8_t (MSABI *FnGetter)(void*);

static bool autoOnlyBox(char g) { return g == 'a' || g == 'c' || g == 'e'; }

// abs / stm '*' = any
// clutch car, skipping the cars used by the assist override lines
static const fh2::CarInfo* findClutch(char clutch, int skip, const fh2::CarInfo* x1 = nullptr, const fh2::CarInfo* x2 = nullptr) {
    for (size_t i = 0; i < fh2::kCarCount; ++i) {
        const fh2::CarInfo& c = fh2::kCars[i];
        if (c.clutch == clutch && !autoOnlyBox(c.gearbox) && &c != x1 && &c != x2 && skip-- == 0) return &c;
    }
    return nullptr;
}

// car with this gearbox (and clutch, '*' = any), skipping up to four cars already used by override lines
static const fh2::CarInfo* findGearbox(char gearbox, char clutch, int skip, const fh2::CarInfo* x1 = nullptr, const fh2::CarInfo* x2 = nullptr,
                                       const fh2::CarInfo* x3 = nullptr, const fh2::CarInfo* x4 = nullptr) {
    for (size_t i = 0; i < fh2::kCarCount; ++i) {
        const fh2::CarInfo& c = fh2::kCars[i];
        if (c.gearbox == gearbox && (clutch == '*' || c.clutch == clutch) && &c != x1 && &c != x2 && &c != x3 && &c != x4 && skip-- == 0) return &c;
    }
    return nullptr;
}

// Cars for the shifting-mode checks; the start-up code writes the override lines for the same picks.
struct ShiftPicks {
    const fh2::CarInfo *ov, *ov3, *cyOv, *cnOv, *cy, *manualAuto, *atOn, *atPlain, *dct;
};
static ShiftPicks pickShiftCars();

static const fh2::CarInfo* findCarBy(char abs, char tc, char stm, int skip) {
    for (size_t i = 0; i < fh2::kCarCount; ++i)
        if ((abs == '*' || fh2::kCars[i].abs == abs) && fh2::kCars[i].tc == tc && (stm == '*' || fh2::kCars[i].stm == stm) && skip-- == 0)
            return &fh2::kCars[i];
    return nullptr;
}

static ShiftPicks pickShiftCars() {
    ShiftPicks p{};
    p.ov = findCarBy('y', 'y', 'y', 1);
    p.ov3 = findCarBy('y', 'y', 'y', 2);
    p.cyOv = findClutch('y', 0, p.ov, p.ov3);
    p.cnOv = findClutch('n', 0, p.ov, p.ov3);
    p.cy = findClutch('y', 1, p.ov, p.ov3);
    p.manualAuto = findGearbox('m', 'y', 0, p.ov, p.ov3, p.cyOv, p.cy);          // manual car forced to automatic shifting
    p.atOn = findGearbox('a', '*', 0);                                           // automatic-only car with clutch override "on"
    p.atPlain = findGearbox('a', '*', 1);
    p.dct = findGearbox('d', 'n', 0, p.ov, p.ov3, p.cyOv, p.cnOv);
    return p;
}

static void assistTests(uint8_t* base, uint64_t absOrig, uint64_t stmOrig, uint64_t tcOrig, uint64_t clutchOrig) {
    printf("realistic assists:\n");
    const uintptr_t kVt[2] = {0x2c5bfc8, 0x2cf39c0};
    bool patched = false;
    for (int i = 0; i < 150 && !patched; ++i) {
        patched = *(uint64_t*)(base + kVt[0] + 0xd8) != absOrig && *(uint64_t*)(base + kVt[1] + 0xf0) != tcOrig;
        if (!patched) Sleep(100);
    }
    for (int v = 0; v < 2; ++v) {
        CHECK(*(uint64_t*)(base + kVt[v] + 0xd8) != absOrig && *(uint64_t*)(base + kVt[v] + 0xd8) != 0, "vtable %d ABS slot re-pointed", v);
        CHECK(*(uint64_t*)(base + kVt[v] + 0xf0) != tcOrig && *(uint64_t*)(base + kVt[v] + 0xf0) != 0, "vtable %d TC slot re-pointed", v);
        CHECK(*(uint64_t*)(base + kVt[v] + 0xe8) != stmOrig && *(uint64_t*)(base + kVt[v] + 0xe8) != 0, "vtable %d STM slot re-pointed", v);
        CHECK(*(uint64_t*)(base + kVt[v] + 0xf8) != clutchOrig && *(uint64_t*)(base + kVt[v] + 0xf8) != 0, "vtable %d clutch slot re-pointed", v);
    }
    const struct { size_t slot; uintptr_t orig; const char* what; } kReal[4] = {
        {0xd8, 0x291f40, "ABS"}, {0xe8, 0x292024, "STM"}, {0xf0, 0x292134, "TC"}, {0xf8, 0x291fd4, "clutch"}};
    for (const auto& r : kReal)
        CHECK(*(uint64_t*)(base + 0x2c5de38 + r.slot) != (uint64_t)(base + r.orig), "real vtable %s slot re-pointed", r.what);
    CHECK(*(uint64_t*)(base + 0x2eb4df0) != (uint64_t)(base + 0x12da500) && *(uint64_t*)(base + 0x2eb6050) != (uint64_t)(base + 0x12da500),
          "dynamics apply slot re-pointed");
    if (!patched) return;

    // fake LocalPlayerChoices: ABS off and TC off in the game settings
    alignas(16) static uint8_t choices[0x300];
    memset(choices, 0, sizeof(choices));
    *(uint64_t*)choices = (uint64_t)(base + kVt[0]);
    choices[0x2b1] = 2;
    choices[0x2b4] = 1;
    FnGetter absG = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xd8);
    FnGetter tcG  = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xf0);
    FnGetter stmG = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xe8);
    choices[0x2b3] = 1;
    CHECK(absG(choices) == 2 && tcG(choices) == 1 && stmG(choices) == 1, "no car seen yet: game values pass through (abs %u tc %u stm %u)", absG(choices), tcG(choices), stmG(choices));

    // call the patched UpdateFFB call-site target with rsi = &elem
    const uintptr_t kSite = 0x2fe134;
    int32_t rel; memcpy(&rel, base + kSite + 1, 4);
    void* target = base + kSite + 5 + rel;
    alignas(16) static uint8_t slot[0x1400];
    alignas(16) static float driver[32];
    memset(slot, 0, sizeof(slot)); memset(driver, 0, sizeof(driver));
    *(float**)(slot + 0x10c0) = driver;
    driver[0] = 1.0f;
    const float in[3] = {0.5f, 20.0f, 0.1f};
    alignas(16) static uint8_t carObj[0x200];
    void* elem = carObj;

    const fh2::CarInfo* yy = findCarBy('y', 'y', '*', 0);
    const fh2::CarInfo* yy2 = findCarBy('y', 'y', 'y', 1);      // 2-field override "off,on" (STM stays auto)
    const fh2::CarInfo* nn = findCarBy('n', 'n', '*', 0);
    const fh2::CarInfo* tcNoStm = findCarBy('*', 'y', 'n', 0);   // TC yes, STM no
    const fh2::CarInfo* allYes = findCarBy('y', 'y', 'y', 0);
    const fh2::CarInfo* ov3 = findCarBy('y', 'y', 'y', 2);       // 3-field override "auto,off,on"
    CHECK(yy && yy2 && nn && tcNoStm && allYes && ov3, "test cars found: ABS+TC car %u (%s), 2-field override car %u, neither car %u (%s), "
          "TC-yes/STM-no car %u, all-yes car %u, 3-field override car %u", yy ? yy->id : 0, yy ? yy->name : "-",
          yy2 ? yy2->id : 0, nn ? nn->id : 0, nn ? nn->name : "-", tcNoStm ? tcNoStm->id : 0, allYes ? allYes->id : 0,
          ov3 ? ov3->id : 0);
    if (!yy || !yy2 || !nn || !tcNoStm || !allYes || !ov3) return;

    memset(carObj, 0, sizeof(carObj));
    *(uint32_t*)(carObj + 0x118) = yy->id;
    callWithRsi(target, slot, in, &elem);
    CHECK(absG(choices) == 1 && tcG(choices) == 0, "car %u (ABS yes, TC yes): ABS on, TC on (abs %u tc %u)", yy->id, absG(choices), tcG(choices));
    CHECK(((FnGetter)*(uint64_t*)(base + kVt[1] + 0xd8))(choices) == 1 && ((FnGetter)*(uint64_t*)(base + kVt[1] + 0xf0))(choices) == 0,
          "second vtable behaves the same");
    choices[0x2b1] = 0;
    CHECK(absG(choices) == 0, "assisted braking (mode 0) is left alone (got %u)", absG(choices));
    choices[0x2b1] = 2;

    *(uint32_t*)(carObj + 0x118) = nn->id;
    callWithRsi(target, slot, in, &elem);
    choices[0x2b1] = 1; choices[0x2b4] = 0; choices[0x2b3] = 0;       // user has all three on
    CHECK(absG(choices) == 2 && tcG(choices) == 1 && stmG(choices) == (nn->stm == 'n' ? 1u : 0u),
          "car %u (ABS no, TC no): ABS off, TC off (abs %u tc %u stm %u)", nn->id, absG(choices), tcG(choices), stmG(choices));
    choices[0x2b1] = 2; choices[0x2b4] = 1; choices[0x2b3] = 1;

    *(uint32_t*)(carObj + 0x118) = tcNoStm->id;   // TC yes, STM no: TC forced on, STM forced off
    callWithRsi(target, slot, in, &elem);
    choices[0x2b4] = 1; choices[0x2b3] = 0;       // user has TC off, STM on
    CHECK(tcG(choices) == 0 && stmG(choices) == 1, "car %u (TC yes, STM no): TC getter 0, STM getter 1 (tc %u stm %u)", tcNoStm->id, tcG(choices), stmG(choices));
    choices[0x2b4] = 1; choices[0x2b3] = 1;

    *(uint32_t*)(carObj + 0x118) = allYes->id;    // TC yes, STM yes: both on
    callWithRsi(target, slot, in, &elem);
    choices[0x2b4] = 1; choices[0x2b3] = 1;       // user has both off
    CHECK(tcG(choices) == 0 && stmG(choices) == 0, "car %u (TC yes, STM yes): TC getter 0, STM getter 0 (tc %u stm %u)", allYes->id, tcG(choices), stmG(choices));

    *(uint32_t*)(carObj + 0x118) = yy2->id;       // per-car override file says <id>=off,on (old 2-field format)
    callWithRsi(target, slot, in, &elem);
    choices[0x2b1] = 1; choices[0x2b4] = 1; choices[0x2b3] = 1;
    CHECK(absG(choices) == 2 && tcG(choices) == 0, "car %u override off,on: ABS off, TC on (abs %u tc %u)", yy2->id, absG(choices), tcG(choices));
    CHECK(stmG(choices) == 0, "car %u 2-field override leaves STM auto (research: yes -> getter %u)", yy2->id, stmG(choices));
    choices[0x2b1] = 2;

    *(uint32_t*)(carObj + 0x118) = ov3->id;       // <id>=auto,off,on
    callWithRsi(target, slot, in, &elem);
    choices[0x2b4] = 0; choices[0x2b3] = 1;       // user has TC on, STM off
    CHECK(tcG(choices) == 1 && stmG(choices) == 0, "car %u override auto,off,on: TC 1, STM 0 (tc %u stm %u)", ov3->id, tcG(choices), stmG(choices));
    choices[0x2b4] = 1; choices[0x2b3] = 1;

    *(uint32_t*)(carObj + 0x118) = 999999;        // not in the table
    callWithRsi(target, slot, in, &elem);
    choices[0x2b1] = 1; choices[0x2b4] = 0; choices[0x2b3] = 0;
    CHECK(absG(choices) == 1 && tcG(choices) == 0 && stmG(choices) == 0, "unknown car: game values (abs %u tc %u stm %u)", absG(choices), tcG(choices), stmG(choices));

    callWithRsi(target, slot, in, nullptr);       // unreadable car vector element: no crash, car unchanged
    callWithRsi(target, slot, in, (void*)(uintptr_t)8);
    void* badElem = (void*)(uintptr_t)16;
    callWithRsi(target, slot, in, &badElem);
    CHECK(true, "garbage rsi values tolerated");

    // ---- realistic clutch: shifting mode 0 automatic, 1 manual, 2 manual with clutch ----
    {
        FnGetter clG  = (FnGetter)*(uint64_t*)(base + kVt[0] + 0xf8);
        FnGetter clG1 = (FnGetter)*(uint64_t*)(base + kVt[1] + 0xf8);
        const ShiftPicks sp = pickShiftCars();                       // same picks as the override lines written at start-up
        const fh2::CarInfo *a1 = sp.ov, *a2 = sp.ov3;
        const fh2::CarInfo* cyOv = sp.cyOv;
        const fh2::CarInfo* cnOv = sp.cnOv;
        const fh2::CarInfo* cy = sp.cy;                              // plain cars: no clutch override
        const fh2::CarInfo* cn = findClutch('n', 1, a1, a2);
        const fh2::CarInfo* co = findClutch('o', 0, a1, a2);
        CHECK(cy && cn && co && cyOv && cnOv, "clutch test cars found: yes %u, no %u, optional %u, override yes->off %u, override no->on %u",
              cy ? cy->id : 0, cn ? cn->id : 0, co ? co->id : 0, cyOv ? cyOv->id : 0, cnOv ? cnOv->id : 0);
        if (cy && cn && co && cyOv && cnOv) {
            auto setCar = [&](uint32_t id) {
                *(uint32_t*)(carObj + 0x118) = id;
                callWithRsi(target, slot, in, &elem);
            };
            auto mode = [&](FnGetter g, uint8_t real) { choices[0x2b5] = real; return (unsigned)g(choices); };
            setCar(999999);
            CHECK(mode(clG, 0) == 0 && mode(clG, 1) == 1 && mode(clG, 2) == 2, "unknown car: shifting mode unchanged");
            setCar(cy->id);
            CHECK(mode(clG, 1) == 2 && mode(clG, 2) == 2 && mode(clG, 0) == 0,
                  "car %u (clutch yes): manual 1 -> 2, 2 -> 2, automatic 0 -> 0 (got %u %u %u)", cy->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
            CHECK(mode(clG1, 1) == 2, "second vtable behaves the same (clutch yes, 1 -> 2)");
            setCar(cn->id);
            CHECK(mode(clG, 2) == 1 && mode(clG, 1) == 1 && mode(clG, 0) == 0,
                  "car %u (clutch no): 2 -> 1, 1 -> 1, 0 -> 0 (got %u %u %u)", cn->id, mode(clG, 2), mode(clG, 1), mode(clG, 0));
            CHECK(mode(clG1, 2) == 1, "second vtable behaves the same (clutch no, 2 -> 1)");
            setCar(co->id);
            CHECK(mode(clG, 0) == 0 && mode(clG, 1) == 1 && mode(clG, 2) == 2, "car %u (clutch optional, OptionalAssists=game): unchanged", co->id);
            setCar(cyOv->id);              // <id>=auto,auto,auto,off
            CHECK(mode(clG, 2) == 1 && mode(clG, 1) == 1 && mode(clG, 0) == 0,
                  "car %u (clutch yes, 4-field override clutch off): 2 -> 1, 1 -> 1, 0 -> 0", cyOv->id);
            setCar(cnOv->id);              // <id>=auto,auto,auto,on
            CHECK(mode(clG, 1) == 2 && mode(clG, 2) == 2 && mode(clG, 0) == 0,
                  "car %u (clutch no, 4-field override clutch on): 1 -> 2, 2 -> 2, 0 -> 0", cnOv->id);
            // ---- RealisticAutomatic=1 (set in the harness ini) ----
            CHECK(sp.atOn && sp.atPlain && sp.dct && sp.manualAuto, "automatic test cars found: at %u / %u, dct %u, manual %u",
                  sp.atOn ? sp.atOn->id : 0, sp.atPlain ? sp.atPlain->id : 0, sp.dct ? sp.dct->id : 0, sp.manualAuto ? sp.manualAuto->id : 0);
            if (sp.atOn && sp.atPlain && sp.dct && sp.manualAuto) {
                setCar(sp.atPlain->id);        // gearbox 'a', no override
                CHECK(mode(clG, 1) == 0 && mode(clG, 2) == 0 && mode(clG, 0) == 0,
                      "car %u (gearbox at): 1 -> 0, 2 -> 0, 0 -> 0 (got %u %u %u)", sp.atPlain->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
                CHECK(mode(clG1, 2) == 0, "second vtable behaves the same (gearbox at, 2 -> 0)");
                setCar(sp.dct->id);            // gearbox 'd', clutch 'n': existing behaviour
                CHECK(mode(clG, 2) == 1 && mode(clG, 1) == 1 && mode(clG, 0) == 0,
                      "car %u (gearbox dct, clutch no): 2 -> 1, 1 -> 1, 0 -> 0 (got %u %u %u)", sp.dct->id, mode(clG, 2), mode(clG, 1), mode(clG, 0));
                setCar(sp.manualAuto->id);     // <id>=auto,auto,auto,automatic
                CHECK(mode(clG, 1) == 0 && mode(clG, 2) == 0 && mode(clG, 0) == 0,
                      "car %u (manual, 4-field override automatic): 1 -> 0, 2 -> 0 (got %u %u %u)", sp.manualAuto->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
                setCar(sp.atOn->id);           // <id>=auto,auto,auto,on cancels forcing
                CHECK(mode(clG, 1) == 2 && mode(clG, 2) == 2 && mode(clG, 0) == 0,
                      "car %u (gearbox at, override clutch on): forcing cancelled, 1 -> 2 (got %u %u %u)", sp.atOn->id, mode(clG, 1), mode(clG, 2), mode(clG, 0));
            }
            choices[0x2b5] = 0;
        }
    }
}

static void cameraTests(uint8_t* base) {
    printf("cockpit camera tracker:\n");
    const uintptr_t kGetFov = 0x10340f0;
    uint64_t cockpit = *(uint64_t*)(base + 0x2e77c30 + 14 * 8);
    uint64_t driver  = *(uint64_t*)(base + 0x2e78120 + 14 * 8);
    uint64_t hood    = *(uint64_t*)(base + 0x2e77ea8 + 14 * 8);
    CHECK(cockpit != (uint64_t)(base + kGetFov), "cockpit camera GetFOV slot re-pointed");
    CHECK(driver != (uint64_t)&otherModFov, "driver camera GetFOV slot re-pointed over another mod's hook");
    CHECK(hood == (uint64_t)(base + kGetFov), "hood camera GetFOV slot unchanged");
    alignas(16) static uint8_t cam[0x400];
    memset(cam, 0, sizeof(cam));
    *(float*)(cam + 0x1d0) = 45.0f;
    cam[0x218] = 0;
    float v = ((FnGetFov)cockpit)(cam);
    CHECK(std::fabs(v - 45.0f) < 1e-4f, "cockpit camera: the game's field of view, unchanged (%f)", v);
    v = ((FnGetFov)driver)(cam);
    CHECK(std::fabs(v - 50.0f) < 1e-4f, "driver camera: another mod's field of view still applies (%f)", v);
}

static void drawDistTests(uint8_t* base, HMODULE plugin) {
    printf("draw distance:\n");
    // A. the tail-jump thunk in front of the track settings deserializer points at our hook
    uint8_t* jmp = base + 0x134af97;
    int32_t rel; memcpy(&rel, jmp + 1, 4);
    uint8_t* target = jmp + 5 + rel;
    CHECK(jmp[0] == 0xE9, "thunk keeps the jmp opcode");
    CHECK(target != base + 0x1144320, "track settings jmp re-pointed away from Serialize (%p)", (void*)target);
    typedef void (MSABI *FnApplyTrack)(uint8_t*);
    auto applyTrack = (FnApplyTrack)GetProcAddress(plugin, "FH2SI_TestApplyTrackOverrides");
    CHECK(applyTrack != nullptr, "test export found");
    if (!applyTrack) return;

    alignas(16) static uint8_t ts[0x600];
    memset(ts, 0, sizeof(ts));
    auto F = [&](int off) -> float& { return *(float*)(ts + off); };
    F(0x234) = 85.0f; F(0x5e4) = 360.0f; F(0x32c) = 4000.0f; F(0x168) = 100.0f; F(0x404) = 250.0f;
    applyTrack(ts);
    CHECK(F(0x234) == 85.0f && F(0x5f4) == 0.0f && F(0x404) == 250.0f && F(0x40c) == 0.0f, "shadows and crowd keep the game's values");
    CHECK(F(0x5e4) == 720.0f && F(0x32c) == 4000.0f, "trees and scenery x2: drawn to %.0f (720); props: the game's 4000 is past 3000 and kept (%.0f)", F(0x5e4), F(0x32c));
    applyTrack(ts);                       // a track file without these keys keeps the values of the last load
    CHECK(F(0x5e4) == 720.0f && F(0x32c) == 4000.0f, "a second load does not grow them again: %.0f %.0f (720 4000)", F(0x5e4), F(0x32c));
    F(0x32c) = 450.0f;                    // the next track sets the props distance
    applyTrack(ts);
    CHECK(F(0x32c) == 900.0f, "props x2: 450 -> %.0f (900)", F(0x32c));
    CHECK(F(0x168) == 200.0f && F(0x164) == 150.0f, "GrassDetail x2: grass 100 -> %.0f, its start %.0f (200 150)", F(0x168), F(0x164));
    // Grass lines: derived from GrassDetail, and stable over track loads
    {
        typedef void (MSABI *FnTrackLines)(uint8_t*, float);
        auto trackLines = (FnTrackLines)GetProcAddress(plugin, "FH2SI_TestTrackLines");
        CHECK(trackLines != nullptr, "FH2SI_TestTrackLines exported");
        if (trackLines) {
            alignas(16) static uint8_t ts2[0x600];
            memset(ts2, 0, sizeof(ts2));
            auto G = [&](int off) -> float& { return *(float*)(ts2 + off); };
            G(0x168) = 100.0f; G(0x164) = 75.0f; G(0x358) = 40.0f; G(0x35c) = 50.0f;
            trackLines(ts2, 2.0f);
            CHECK(G(0x168) == 200.0f && G(0x164) == 150.0f && G(0x358) == 80.0f && G(0x35c) == 100.0f,
                  "grass x2, the other lines follow: %.0f %.0f %.0f %.0f (200 150 80 100)", G(0x168), G(0x164), G(0x358), G(0x35c));
            G(0x168) = 100.0f;                    // the next track file sets only the grass end
            trackLines(ts2, 2.0f);
            CHECK(G(0x358) == 80.0f && G(0x35c) == 100.0f, "grass detail lines stable on a second load: %.0f %.0f (80 100)", G(0x358), G(0x35c));
            trackLines(ts2, 2.0f);   // a track file without grass keys keeps every value
            CHECK(G(0x358) == 80.0f && G(0x35c) == 100.0f, "grass detail lines stable when nothing is reloaded: %.0f %.0f (80 100)", G(0x358), G(0x35c));
        }
    }
    // C. the calls of the level selection go through stubs that keep the caller's model (r15), renderer (rbx) and view type (rsi) in TLS
    for (uintptr_t rva : {0x1353e2e, 0x13545b3, 0x135464a}) {
        int32_t r; memcpy(&r, base + rva + 1, 4);
        const uint8_t* stub = base + rva + 5 + r;
        CHECK(base[rva] == 0xE8 && stub != base + 0x1139200, "level selection call %llx re-pointed", (unsigned long long)rva);
        const uint8_t keepModel[] = {0x65, 0x4C, 0x89, 0x3C, 0x25}, keepRender[] = {0x65, 0x48, 0x89, 0x1C, 0x25}, keepView[] = {0x65, 0x48, 0x89, 0x34, 0x25};
        CHECK(memcmp(stub, keepModel, 5) == 0 && memcmp(stub + 9, keepRender, 5) == 0 && memcmp(stub + 18, keepView, 5) == 0,
              "call %llx: its stub keeps r15, rbx and rsi in TLS", (unsigned long long)rva);
    }
    int32_t movedRel; memcpy(&movedRel, base + 0x11391d4 + 1, 4);
    CHECK(base + 0x11391d4 + 5 + movedRel == base + 0x1139210, "the call Azimuth moved to its later entry is left alone");
    // E. placed libraries: near detail switches move out as they load; a fence's stand-in moves along, a tree's billboard stays
    CHECK(base[0x1867a9d] == 0xE8 && memcmp(base + 0x1867aa2, "\x0F\x1F\x44\x00\x00", 5) == 0, "the placed objects' pass calls the plugin first");
    {
        typedef void (MSABI *FnScaleLib)(uint8_t*, float, float, float);
        auto scaleLib = (FnScaleLib)GetProcAddress(plugin, "FH2SI_TestScaleLibrary");
        CHECK(scaleLib != nullptr, "FH2SI_TestScaleLibrary exported");
        // ranges: start, start - fade-in, end, end + fade-out, two squares, fade-in, fade-out, 4 more floats; in list 4 only
        static float tree[3][12] = {{0, 0, 100, 100}, {100, 100, 190, 190}, {190, 190, 190, 190, 0, 0, 0, 30}};
        static float board[2][12] = {{190, 190, 1000, 1000, 0, 0, 30, 50}, {1000, 1000, 2750, 2750}};
        static float bush[2][12] = {{0, 0, 70, 70}, {70, 70, 150, 150}};
        static float farBoard[1][12] = {{0, 0, 2000, 2000}};
        static float tree2[2][12] = {{0, 0, 190, 190}, {190, 190, 190, 190, 0, 0, 0, 30}};   // a second tree sharing the billboard
        float* const ranges[5][2] = {{tree[0], tree[0] + 36}, {board[0], board[0] + 24}, {bush[0], bush[0] + 24}, {farBoard[0], farBoard[0] + 12},
                                     {tree2[0], tree2[0] + 24}};
        const uint32_t type3d = 3;                       // the tree and bush models are 3D ranges (type in the last slot)
        for (float* r : {tree[0], tree[1], bush[0], bush[1], tree2[0]}) memcpy(r + 11, &type3d, 4);
        alignas(16) static uint8_t objs[5 * 0x2a8];
        memset(objs, 0, sizeof objs);
        for (int o = 0; o < 5; ++o) { memcpy(objs + 0x2a8 * o + 0x28 * 4, &ranges[o][0], 8); memcpy(objs + 0x2a8 * o + 0x28 * 4 + 8, &ranges[o][1], 8); }
        alignas(16) static uint8_t lib[0x60];
        const auto setLib = [&](const char* n, uint8_t* first, uint8_t* last) {   // the name as an MSVC std::string on the heap, then the objects
            const uint64_t len = strlen(n), cap = 31;
            memset(lib, 0, sizeof lib);
            memcpy(lib, &n, 8); memcpy(lib + 0x10, &len, 8); memcpy(lib + 0x18, &cap, 8); memcpy(lib + 0x28, &first, 8); memcpy(lib + 0x30, &last, 8);
        };
        if (scaleLib) {
            setLib("TREEANDBUSHTEMPLATE", objs, objs + 5 * 0x2a8);
            scaleLib(lib, 8.0f, 0.0f, 0.0f);
            CHECK(tree[0][2] == 800.0f && tree[0][3] == 800.0f && tree[1][0] == 800.0f && tree[1][1] == 800.0f && tree[1][2] == 1520.0f &&
                  tree[2][0] == 1520.0f && tree[2][2] == 1520.0f && tree[2][7] == 30.0f,
                  "trees x8: full model to %.0f, simpler one to %.0f, its fade at %.0f (800 1520 1520)", tree[0][2], tree[1][2], tree[2][0]);
            CHECK(board[0][0] == 190.0f && board[0][1] == 190.0f && board[0][2] == 1000.0f && board[0][6] == 30.0f && board[1][0] == 1000.0f && board[1][2] == 2750.0f,
                  "the tree's billboards keep the game's ranges: %.0f to %.0f, %.0f to %.0f (190 to 1000, 1000 to 2750)", board[0][0], board[0][2], board[1][0], board[1][2]);
            CHECK(bush[0][2] == 560.0f && bush[1][0] == 560.0f && bush[1][2] == 1200.0f,
                  "a bush with nothing taking over: %.0f, %.0f to %.0f (560, 560 to 1200)", bush[0][2], bush[1][0], bush[1][2]);
            CHECK(farBoard[0][2] == 2000.0f, "a billboard from 0 to %.0f is left alone (2000)", farBoard[0][2]);
            CHECK(tree2[0][2] == 1520.0f && tree2[1][0] == 1520.0f, "a second tree: full model to %.0f (1520)", tree2[0][2]);
            scaleLib(lib, 0.0f, 2.0f, 2.0f);
            CHECK(bush[1][2] == 1200.0f, "trees at the game's x1: not moved (%.0f, 1200)", bush[1][2]);
            static float nearFence[1][12] = {{0, 0, 100, 100, 0, 0, 0, 20}};
            static float fenceStandIn[1][12] = {{100, 100, 500, 500, 0, 0, 20, 50}};
            static float nearFence2[1][12] = {{0, 0, 100, 100}};                     // a second fence sharing the stand-in
            float* const fr[3][2] = {{nearFence[0], nearFence[0] + 12}, {fenceStandIn[0], fenceStandIn[0] + 12}, {nearFence2[0], nearFence2[0] + 12}};
            alignas(16) static uint8_t fences[3 * 0x2a8];
            memset(fences, 0, sizeof fences);
            for (int o = 0; o < 3; ++o) { memcpy(fences + 0x2a8 * o + 0x28 * 4, &fr[o][0], 8); memcpy(fences + 0x2a8 * o + 0x28 * 4 + 8, &fr[o][1], 8); }
            setLib("BARRIERTEMPLATE", fences, fences + 3 * 0x2a8);
            scaleLib(lib, 0.0f, 8.0f, 0.0f);
            CHECK(nearFence[0][2] == 500.0f && fenceStandIn[0][0] == 500.0f && fenceStandIn[0][2] == 500.0f && nearFence2[0][2] == 500.0f,
                  "fences x8: both near fences to %.0f and %.0f, capped by the simpler one's end; it starts at %.0f (500 500 500)",
                  nearFence[0][2], nearFence2[0][2], fenceStandIn[0][0]);
            setLib("CROWDTEMPLATE", fences, fences + 3 * 0x2a8);
            scaleLib(lib, 8.0f, 8.0f, 8.0f);
            CHECK(nearFence[0][2] == 500.0f, "another library is not scaled: %.0f (500)", nearFence[0][2]);
        }
    }
    // F. loaded grass patches are added to the frame's item list
    CHECK(base[0x1336445] == 0xE8 && memcmp(base + 0x133644a, "\x0F\x1F\x44\x00\x00\x0F\x1F\x44\x00\x00", 10) == 0 && base[0x1336454] == 0x48,
          "the view's item list set-up calls the plugin first");
    {
        typedef void (MSABI *FnShowGrass)(uint8_t*, uint8_t*);
        auto showGrass = (FnShowGrass)GetProcAddress(plugin, "FH2SI_TestShowGrass");
        CHECK(showGrass != nullptr, "FH2SI_TestShowGrass exported");
        // six streamed items: 0 grass (listed), 1 grass, 2 a tree patch, 3 not loaded, 4 grass still loading, 5 another class
        const uint64_t resVt = (uint64_t)(base + 0x2f5a830), grassVt = (uint64_t)(base + 0x2f5a470), treeVt = (uint64_t)(base + 0x2f5a300);
        static uint64_t item[6];
        alignas(16) static uint8_t res[6][0x120], handle[6][0x40], zone[0x1c0];
        static uint8_t view[0x35b8];
        static uint64_t entries[6][2];
        static uint32_t listA[8], listB[8];
        memset(res, 0, sizeof res); memset(handle, 0, sizeof handle); memset(zone, 0, sizeof zone); memset(entries, 0, sizeof entries);
        for (int i = 0; i < 6; ++i) {
            item[i] = i == 2 ? treeVt : grassVt;
            const uint64_t vt = i == 5 ? treeVt : resVt, it = (uint64_t)&item[i], r = (uint64_t)res[i];
            memcpy(res[i], &vt, 8); res[i][0x34] = 4; res[i][0x35] = i == 4 ? 3 : 4; memcpy(res[i] + 0x118, &it, 8);
            memcpy(handle[i] + 0x28, &r, 8);
            if (i != 3) entries[i][0] = (uint64_t)handle[i];
        }
        const uint64_t arr[2] = {(uint64_t)entries, (uint64_t)(entries + 6)};
        const uint64_t arrPtr = (uint64_t)arr;
        memcpy(view + 0x35b0, &arrPtr, 8);
        const auto setList = [&](int which, uint32_t* buf, int size, int cap) {
            const uint64_t v[3] = {(uint64_t)buf, (uint64_t)(buf + size), (uint64_t)(buf + cap)};
            memcpy(zone + 0x188 + 0x18 * which, v, 24);
        };
        const auto listed = [&](int which) { uint64_t v[2]; memcpy(v, zone + 0x188 + 0x18 * which, 16); return (int)((v[1] - v[0]) / 4); };
        if (showGrass) {
            listA[0] = 2; listA[1] = 0;
            setList(0, listA, 2, 8);
            setList(1, listB, 0, 8);
            showGrass(zone, view);
            CHECK(listed(0) == 3 && listA[0] == 2 && listA[1] == 0 && listA[2] == 1,
                  "loaded grass not on the list is added once: %d items, the last is %u (3, 1)", listed(0), listA[2]);
            showGrass(zone, view);
            CHECK(listed(0) == 3, "a second frame adds nothing: %d items (3)", listed(0));
            res[4][0x35] = 4;
            showGrass(zone, view);
            CHECK(listed(0) == 4 && listA[3] == 4, "grass that finished loading is added: %d items, the last is %u (4, 4)", listed(0), listA[3]);
            const uint32_t one = 1;
            memcpy(zone + 0x1b8, &one, 4);                       // the game switched to its other list, still empty
            showGrass(zone, view);
            CHECK(listed(1) == 3 && listed(0) == 4, "a new list gets every loaded grass patch at once: %d items, the old list keeps %d (3, 4)", listed(1), listed(0));
            res[1][0x34] = 0;                                     // patch 1 is unloaded
            setList(1, listB, 0, 8);
            showGrass(zone, view);
            CHECK(listed(1) == 2 && listB[0] != 1 && listB[1] != 1, "an unloaded patch is not added again: %d items (2)", listed(1));
            setList(1, listB, 0, 1);
            showGrass(zone, view);
            CHECK(listed(1) == 1, "a full list is not written past its end: %d items (1)", listed(1));
            const uint32_t two = 2;
            memcpy(zone + 0x1b8, &two, 4);
            setList(0, listA, 0, 8);
            showGrass(zone, view);
            CHECK(listed(0) == 0, "an unknown list number changes nothing: %d items (0)", listed(0));
        }
    }
    // Azimuth's Scenery detail: a jump at the level selection to a function that loads its factor 0x4e bytes in
    {
        typedef bool (MSABI *FnAzimuthLod)(float, float*, float*);
        auto azimuthLod = (FnAzimuthLod)GetProcAddress(plugin, "FH2SI_TestAzimuthLod");
        CHECK(azimuthLod != nullptr, "FH2SI_TestAzimuthLod exported");
        alignas(16) static uint8_t fakeAzimuth[0x80];
        memset(fakeAzimuth, 0xCC, sizeof(fakeAzimuth));
        const uint8_t load[] = {0xF3, 0x0F, 0x10, 0x0D}, mul[] = {0xF3, 0x0F, 0x59, 0xC8};
        memcpy(fakeAzimuth + 0x4e, load, 4);
        const int32_t factorRel = 0x70 - 0x56;                     // the factor sits at fakeAzimuth + 0x70
        memcpy(fakeAzimuth + 0x52, &factorRel, 4);
        memcpy(fakeAzimuth + 0x5b, mul, 4);
        float* factor = (float*)(fakeAzimuth + 0x70);
        uint8_t saved[14];
        memcpy(saved, base + 0x1139200, 14);
        DWORD prot;
        VirtualProtect(base + 0x1139200, 14, PAGE_EXECUTE_READWRITE, &prot);
        uint8_t jmp[14] = {0xFF, 0x25, 0, 0, 0, 0};
        const uint64_t to = (uint64_t)fakeAzimuth;
        memcpy(jmp + 6, &to, 8);
        memcpy(base + 0x1139200, jmp, 14);
        float after = -1.0f, wrap = -1.0f;
        if (azimuthLod) {
            *factor = 0.0625f;                                         // Ultra: 4x
            CHECK(azimuthLod(1.0f / 64.0f, &after, &wrap) && after == 0.0625f && wrap == 1.0f / 64.0f,
                  "SceneryDetail 8x over Azimuth's 4x: Azimuth's factor stays %.4f, the hook scales %.5f (0.01563)", after, wrap);
            CHECK(azimuthLod(0.25f, &after, &wrap) && after == 0.0625f && wrap == 0.0625f,
                  "SceneryDetail 2x under Azimuth's 4x: Azimuth's stays %.4f, the hook scales %.4f (0.0625)", after, wrap);
            fakeAzimuth[0x5b] = 0x90;                                  // not Azimuth's code
            CHECK(!azimuthLod(0.25f, &after, &wrap) && wrap == 0.25f,
                  "unknown code behind the jump: the hook scales %.3f (0.25) on its own", wrap);
        }
        memcpy(base + 0x1139200, saved, 14);
        VirtualProtect(base + 0x1139200, 14, prot, &prot);
    }
    // the hooked selection on a fake model: one level per model, the finest ready one at or past the scaled pick
    {
        typedef char (MSABI *FnTestSelect)(void*, void*, float, float, uint64_t, int*, uint8_t, float, float, float, float);
        auto testSelect = (FnTestSelect)GetProcAddress(plugin, "FH2SI_TestSelect");
        CHECK(testSelect != nullptr, "FH2SI_TestSelect exported");
        struct FakeResource { const uint64_t* vt; };
        static uint64_t vtSettled[16], vtChanging[16];
        vtSettled[0x70 / 8] = (uint64_t)(bool (MSABI *)(uintptr_t))[](uintptr_t) -> bool { return false; };
        vtChanging[0x70 / 8] = (uint64_t)(bool (MSABI *)(uintptr_t))[](uintptr_t) -> bool { return true; };
        static FakeResource settled{vtSettled}, changing{vtChanging};
        alignas(16) static uint8_t model[0x100], hopA[0x10], hopB[0x10], desc[0x80], render[0x35e0], emptyModel[0x100];
        static uint64_t resources[8];
        static uint64_t* resourceTable = resources;
        *(uint64_t*)(model + 0xb0) = (uint64_t)hopA;
        *(uint64_t*)(hopA + 8) = (uint64_t)hopB;
        *(uint64_t*)(hopB + 8) = (uint64_t)desc;
        *(uint64_t*)(render + 0x35d0) = (uint64_t)&resourceTable;
        const uint32_t none = *(const uint32_t*)(base + 0x2ebd1cc);     // the id of a level without a resource
        CHECK(none < 4 || none > 7, "the no-resource id %x is not one of the fake ids 4..7", none);
        // level j has resource id 4 + j; state: 's' settled, 'c' changing, '0' not loaded, '-' no resource
        const auto levels = [&](const char* state) {
            for (int j = 0; j < 4; ++j) {
                *(uint32_t*)(desc + 0x6c + 4 * j) = state[j] == '-' ? none : 4 + j;
                resources[4 + j] = state[j] == 's' ? (uint64_t)&settled : state[j] == 'c' ? (uint64_t)&changing : 0;
            }
        };
        const uint64_t all = 0x3cULL << 48;
        const float t0 = 900.0f, t1 = 3600.0f, t2 = 8100.0f, t3 = 10000.0f;   // 30/60/90/100 m, scaled x2 by m = 0.25
        int level = -1;
        if (testSelect) {
            levels("ssss");
            char culled = testSelect(model, render, 0.25f, 150.0f * 150.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(!culled && level == 2, "150 m, all levels ready: level %d, culled %d (want 2, 0)", level, (int)culled);
            culled = testSelect(model, render, 0.25f, 250.0f * 250.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(culled && level == 4, "250 m is past the last scaled distance: level %d, culled %d (want 4, 1)", level, (int)culled);
            levels("sscs");
            culled = testSelect(model, render, 0.25f, 150.0f * 150.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(!culled && level == 3, "150 m, level 2 still changing: level %d, culled %d (want 3, 0)", level, (int)culled);
            levels("ss00");
            culled = testSelect(model, render, 0.25f, 150.0f * 150.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(culled && level == 4, "150 m, levels 2 and 3 not loaded: the game's own pick (level %d, culled %d; want 4, 1)", level, (int)culled);
            levels("ss-s");
            culled = testSelect(model, render, 0.25f, 150.0f * 150.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(!culled && level == 2, "150 m, level 2 has no resource to wait for: level %d, culled %d (want 2, 0)", level, (int)culled);
            levels("ssss");
            culled = testSelect(model, render, 0.25f, 150.0f * 150.0f, 1ULL << 53, &level, 0, t0, t1, t2, t3);
            CHECK(culled && level == 4, "150 m, an item holding only level 3 is not drawn over level 2: level %d, culled %d (want 4, 1)", level, (int)culled);
            culled = testSelect(model, render, 0.25f, 45.0f * 45.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(!culled && level == 0, "45 m, the game picks level 1 and the item holds level 0: level %d, culled %d (want 0, 0)", level, (int)culled);
            levels("ccss");
            culled = testSelect(model, render, 0.25f, 45.0f * 45.0f, 1ULL << 52, &level, 0, t0, t1, t2, t3);
            CHECK(culled && level == 1, "45 m, levels 0 and 1 still changing: level 2 is not drawn closer than the game's level 1 (level %d, culled %d; want 1, 1)", level, (int)culled);
            levels("ssss");
            culled = testSelect(model, render, 0.25f, 45.0f * 45.0f, 1ULL << 51, &level, 0, t0, t1, t2, t3);
            CHECK(!culled && level == 1, "45 m, an item holding only the game's level 1 is still drawn although level 0 is picked: level %d, culled %d (want 1, 0)", level, (int)culled);
            culled = testSelect(emptyModel, render, 0.25f, 150.0f * 150.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(culled && level == 4, "150 m, a model laid out differently: the game's own pick (level %d, culled %d; want 4, 1)", level, (int)culled);
            culled = testSelect(model, render, 1.0f, 50.0f * 50.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(!culled && level == 1, "no scaling, 50 m: the game's level %d, culled %d (want 1, 0)", level, (int)culled);
            culled = testSelect(model, render, 0.25f, 10.0f * 10.0f, all, &level, 1, t0, t1, t2, t3);
            CHECK(!culled && level == 1, "10 m with the finest level barred: level %d (want 1)", level);
            culled = testSelect(model, render, 0.25f, 10.0f * 10.0f, all, &level, 0, t0, t1, t2, t3);
            CHECK(!culled && level == 0, "10 m: level %d (want 0)", level);
            // without scaling the hook is the game's own function (still unpatched at 0x1139200), on every input
            typedef char (MSABI *FnGameSelect)(float, uint64_t*, int*, float, uint8_t, float, float, float, float);
            auto game = (FnGameSelect)(base + 0x1139200);
            int differ = 0;
            for (float d : {0.0f, 10.0f, 29.9f, 30.0f, 45.0f, 60.0f, 75.0f, 90.0f, 95.0f, 100.0f, 120.0f})
                for (uint64_t f : {all, 1ULL << 50, 1ULL << 51, 1ULL << 52, 1ULL << 53, 3ULL << 50, 0ULL})
                    for (uint8_t noFinest : {0, 1}) {
                        int lg = -7, lh = -7;
                        const char cg = game(d * d, &f, &lg, 0.0f, noFinest, t0, t1, t2, t3);
                        const char ch = testSelect(model, render, 1.0f, d * d, f, &lh, noFinest, t0, t1, t2, t3);
                        if (cg != ch || lg != lh) ++differ;
                    }
            CHECK(differ == 0, "the hook's copy of the game's level selection matches the game's on 154 inputs (%d differ)", differ);
            // main view: the object of the game's level hides only while the finer object of the same placed model is in
            // the frame's list, gets through the render loop's checks, and drew at its last call
            typedef void (MSABI *FnTestSetView)(uint32_t);
            auto setView = (FnTestSetView)GetProcAddress(plugin, "FH2SI_TestSetView");
            CHECK(setView != nullptr, "FH2SI_TestSetView exported");
            if (setView) {
                enum { kCoarse, kFine, kPart, kObjects };          // list order: the coarser object is called first
                alignas(16) static uint8_t list[kObjects][0x100], blocks[kObjects][0xd0], otherHop[0x10], otherDesc[0x80];
                static uint64_t listEnds[2];
                const uint64_t fineFlags = 1ULL << 50, coarseFlags = 1ULL << 51;
                const float place[3] = {100.0f, 5.0f, -200.0f};
                const uint16_t ends[4] = {30, 60, 90, 100};
                for (int i = 0; i < kObjects; ++i) {
                    *(uint64_t*)(list[i] + 0xb0) = (uint64_t)blocks[i];
                    *(uint64_t*)(blocks[i] + 8) = (uint64_t)hopB;              // every object shares the level resources
                    *(uint64_t*)(list[i] + 0xb8) = i == kCoarse ? coarseFlags : fineFlags;
                    memcpy(list[i] + 0x24, place, 12);
                    memcpy(list[i] + 0xf0, ends, 8);
                    list[i][0xc4] = 0x1f;
                }
                *(uint64_t*)(otherHop + 8) = (uint64_t)otherDesc;
                const uint32_t otherIds[4] = {4, 5, 6, 3};
                memcpy(otherDesc + 0x6c, otherIds, 16);
                listEnds[0] = (uint64_t)list[0];
                listEnds[1] = (uint64_t)(list + kObjects);
                *(uint64_t*)(render + 0x35a8) = (uint64_t)listEnds;
                // 45 m: the game picks level 1, the scaled distance level 0
                const auto call = [&](int i) {
                    level = -1;
                    return testSelect(list[i], render, 0.25f, 45.0f * 45.0f, *(uint64_t*)(list[i] + 0xb8), &level, 0, t0, t1, t2, t3);
                };
                const auto coarseShown = [&] { const char c = call(kCoarse); return !c && level == 1; };
                levels("ssss");
                setView(18);
                CHECK(coarseShown(), "main view, 45 m: the finer object has not drawn yet, so the game's level 1 is drawn");
                culled = call(kFine);
                CHECK(!culled && level == 0, "main view, 45 m: the finer object draws level 0 (level %d, culled %d)", level, (int)culled);
                CHECK(call(kCoarse) == 1, "main view, 45 m: the game's level 1 object is hidden now");
                setView(6);
                CHECK(coarseShown(), "shadow casters (view 6): the game's level 1 object stays");
                setView(18);
                listEnds[1] = (uint64_t)list[kFine];
                CHECK(coarseShown(), "the finer object left the frame's list: level 1 is drawn again at once");
                listEnds[1] = (uint64_t)(list + kObjects);
                blocks[kFine][0xc1] = 1;
                CHECK(coarseShown(), "the finer object is retired: level 1 is drawn");
                blocks[kFine][0xc1] = 0;
                *(uint64_t*)(blocks[kFine] + 0x10) = 1ULL << 39;
                CHECK(coarseShown(), "the finer object is switched off: level 1 is drawn");
                *(uint64_t*)(blocks[kFine] + 0x10) = 0;
                *(uint64_t*)(list[kFine] + 0xb0) = 0;
                CHECK(coarseShown(), "the finer object has no block: level 1 is drawn");
                *(uint64_t*)(list[kFine] + 0xb0) = (uint64_t)blocks[kFine];
                list[kFine][0xc4] = 0x1e;
                CHECK(coarseShown(), "the finer object fails the render loop's own test: level 1 is drawn");
                list[kFine][0xc4] = 0x7f;
                CHECK(call(kCoarse) == 1, "the finer object differs only in bits the main view does not test: level 1 stays hidden");
                list[kFine][0xc4] = 0x1f;
                const float moved = 101.0f;
                memcpy(list[kFine] + 0x24, &moved, 4);
                CHECK(coarseShown(), "another placement sits at the finer object's place in the list: level 1 is drawn");
                memcpy(list[kFine] + 0x24, place, 4);
                *(uint64_t*)(list[kFine] + 0xb8) = fineFlags | 2;
                CHECK(coarseShown(), "an object with other flags sits there: level 1 is drawn");
                *(uint64_t*)(list[kFine] + 0xb8) = fineFlags;
                *(uint64_t*)(blocks[kFine] + 8) = (uint64_t)otherHop;
                CHECK(coarseShown(), "a model with other level resources sits there: level 1 is drawn");
                *(uint64_t*)(blocks[kFine] + 8) = (uint64_t)hopB;
                CHECK(call(kCoarse) == 1, "with the finer object back as it was, level 1 is hidden again");
                levels("0sss");
                culled = call(kFine);
                CHECK(culled, "level 0 unloads: the finer object is not drawn (level %d, culled %d)", level, (int)culled);
                CHECK(coarseShown(), "level 0 unloads: level 1 is drawn");
                levels("ssss");
                call(kFine);
                CHECK(call(kCoarse) == 1, "level 0 is back and drew: level 1 is hidden");
                call(kPart);
                CHECK(coarseShown(), "two objects hold level 0 (a level drawn in parts): the game's level 1 is never hidden");
                call(kFine);
                CHECK(coarseShown(), "and it stays so after either part draws");
                setView(0);
                *(uint64_t*)(render + 0x35a8) = 0;
            }
        }
    }

}

typedef HRESULT (WINAPI *FnProxyCreate)(HINSTANCE, DWORD, REFIID, void**, void*);

static HRESULT askProxy(FnProxyCreate create) {
    static const GUID kIid = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};  // IDirectInput8W
    void* obj = nullptr;
    const HRESULT hr = create(GetModuleHandleW(nullptr), 0x0800, kIid, &obj, nullptr);
    if (SUCCEEDED(hr) && obj) (*(ULONG (WINAPI***)(void*))obj)[2](obj);    // Release
    return hr;
}

static void writeText(const std::wstring& path, const char* text) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (f) { fputs(text, f); fclose(f); }
}

// dinput8.dll keeps Azimuth (the main exe, here the harness) from the wheel only when ExtraOptions will load the mod and
// the mod wants the wheel, and the tab checks turn their part of the settings off. Runs in build\proxy_test, which is
// overwritten on each run.
static void enableCheckTests(HMODULE plugin) {
    printf("\ntab checks and dinput8.dll\n");
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(plugin, self, MAX_PATH);
    std::wstring build(self);
    build.resize(build.find_last_of(L"\\/") + 1);
    const std::wstring dir = build + L"proxy_test\\";
    const std::wstring mod = dir + L"mods\\Sim Immersion\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW((dir + L"mods").c_str(), nullptr);
    CreateDirectoryW(mod.c_str(), nullptr);
    CreateDirectoryW((mod + L"plugins").c_str(), nullptr);
    const std::wstring dll = mod + L"plugins\\FH2SimImmersion.dll", ini = mod + L"FH2SimImmersion.ini",
                       winmm = dir + L"winmm.dll", cfg = dir + L"cfg.ini";
    DeleteFileW(dll.c_str());
    DeleteFileW(ini.c_str());
    DeleteFileW(winmm.c_str());
    CopyFileW((build + L"dinput8.dll").c_str(), (dir + L"dinput8.dll").c_str(), FALSE);

    HMODULE proxy = LoadLibraryW((dir + L"dinput8.dll").c_str());
    CHECK(proxy != nullptr, "dinput8.dll loads");
    FnProxyCreate create = proxy ? (FnProxyCreate)GetProcAddress(proxy, "DirectInput8Create") : nullptr;
    CHECK(create != nullptr, "dinput8.dll exports DirectInput8Create");
    if (create) {
        auto ask = [&]() { return askProxy(create); };
        CHECK(SUCCEEDED(ask()), "no mod: Azimuth keeps the wheel");
        writeText(dll, "");
        CHECK(SUCCEEDED(ask()), "mod but no ExtraOptions: Azimuth keeps the wheel");
        writeText(winmm, "");
        CHECK(ask() == E_FAIL, "ExtraOptions and the mod, no settings file yet: Azimuth is refused");
        writeText(ini, "[Wheel]\nEnabled=1\n");
        CHECK(ask() == E_FAIL, "Wheel Enabled=1: Azimuth is refused");
        writeText(ini, "[Wheel]\nEnabled=0\n");
        CHECK(SUCCEEDED(ask()), "Wheel Enabled=0: Azimuth keeps the wheel");
    }

    typedef void (MSABI *FnMapInput)(const DIJOYSTATE2*, const DIJOYSTATE2*, const DIJOYSTATE2*, const DIJOYSTATE2*, fh2::PedalRest*, fh2::WheelInput*);
    FnMapInput mapInput = (FnMapInput)GetProcAddress(plugin, "FH2SI_TestMapInput");
    CHECK(mapInput != nullptr, "FH2SI_TestMapInput exported");
    if (mapInput) {
        DIJOYSTATE2 wheel{}, pedals{};
        wheel.lY = 0;            // the wheel's own pedal axes read fully pressed: they must be ignored
        wheel.lRz = 0;
        wheel.rgbButtons[12] = 0x80;
        pedals.lY = 65535; pedals.lRz = 65535;
        fh2::PedalRest rest[4];
        fh2::WheelInput in;
        mapInput(&wheel, &pedals, nullptr, nullptr, rest, &in);
        CHECK(in.throttle == 0 && in.brake == 0, "separate pedals are read, not the wheel's axes (%.2f %.2f)", in.throttle, in.brake);
        pedals.lY = 0;
        mapInput(&wheel, &pedals, nullptr, nullptr, rest, &in);
        CHECK(in.throttle == 1.0f && in.brake == 0, "separate throttle pressed (%.2f %.2f)", in.throttle, in.brake);
        mapInput(&wheel, nullptr, nullptr, nullptr, rest, &in);
        CHECK(in.throttle == 0 && in.brake == 0 && in.gear == 0 && !in.handbrake && !in.b, "a missing device counts as released (%.2f %d)", in.throttle, in.gear);
        fh2::PedalRest fresh[4];
        pedals.lY = 32768;
        mapInput(&wheel, &pedals, nullptr, nullptr, fresh, &in);
        CHECK(in.throttle == 0, "a pedal resting half pressed counts as released (%.2f)", in.throttle);
        pedals.lY = 0;
        mapInput(&wheel, &pedals, nullptr, nullptr, fresh, &in);
        CHECK(in.throttle == 1.0f, "and reads normally once it moved (%.2f)", in.throttle);
        DIJOYSTATE2 shifter{};
        shifter.rgbButtons[13] = 0x80;
        mapInput(&wheel, &pedals, &shifter, nullptr, rest, &in);
        CHECK(in.gear == 2, "gear comes from the separate shifter (%d)", in.gear);
        mapInput(&wheel, &pedals, &wheel, nullptr, rest, &in);
        CHECK(in.gear == 1, "gear comes from the wheel when the shifter is on it (%d)", in.gear);
        DIJOYSTATE2 hb{};
        hb.lX = 0;
        mapInput(&wheel, &pedals, nullptr, &hb, rest, &in);
        CHECK(!in.handbrake, "handbrake at rest is off");
        hb.lX = 65535;
        mapInput(&wheel, &pedals, nullptr, &hb, rest, &in);
        CHECK(in.handbrake && in.b, "separate handbrake pulled presses B (%.2f)", in.handbrakeAxis);
    }

    typedef void (MSABI *FnLoadCfg)(const wchar_t*, int*);
    FnLoadCfg loadCfg = (FnLoadCfg)GetProcAddress(plugin, "FH2SI_TestLoadConfig");
    CHECK(loadCfg != nullptr, "FH2SI_TestLoadConfig exported");
    if (loadCfg) {
        int out[5] = {};
        writeText(cfg, "[Input]\nConfigVersion=12\nDiagnostics=1\n[General]\nOverrideAzimuthHeadTracking=1\n"
                       "[Simulation]\nEnabled=1\nRealisticABS=1\n[DrawDistance]\nEnabled=1\nSceneryDetail=2\n[Logging]\nEnabled=1\n");
        loadCfg(cfg.c_str(), out);
        CHECK(out[0] == 1 && out[1] == 1 && out[2] == 2 && out[3] == 1 && out[4] == 1, "checks on: settings used (%d %d %d %d %d)", out[0], out[1], out[2], out[3], out[4]);
        writeText(cfg, "[Input]\nConfigVersion=12\nDiagnostics=1\n[General]\nOverrideAzimuthHeadTracking=0\n"
                       "[Simulation]\nEnabled=0\nRealisticABS=1\n[DrawDistance]\nEnabled=0\nSceneryDetail=2\n[Logging]\nEnabled=0\n");
        loadCfg(cfg.c_str(), out);
        CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0 && out[4] == 0, "checks off: settings ignored (%d %d %d %d %d)", out[0], out[1], out[2], out[3], out[4]);
        writeText(cfg, "[Input]\nConfigVersion=6\n[Wheel]\nFFBGain=0.90\nFFBTorqueGain=2.0\nFFBSlipGain=0.7\nFFBAlignGain=0.60\n[Rotation]\nYawLeftIsPositive=1\n");
        loadCfg(cfg.c_str(), out);
        wchar_t yaw[8] = L"";
        GetPrivateProfileStringW(L"Rotation", L"YawLeftIsPositive", L"", yaw, 8, cfg.c_str());
        CHECK(!wcscmp(yaw, L"0"), "upgrade: the old YawLeftIsPositive=1 default follows the new 0 (%ls)", yaw);
        auto saved = [&](const wchar_t* key) {
            wchar_t v[32] = L"";
            GetPrivateProfileStringW(L"Wheel", key, L"", v, 32, cfg.c_str());
            return std::wstring(v);
        };
        CHECK(saved(L"FFBTorqueGain") == L"1.0" && saved(L"FFBAlignGain").empty() && saved(L"FFBSlipGain").empty(), "upgrade: old force defaults follow the new ones, removed keys are dropped");
        CHECK(saved(L"FFBGain") == L"0.90", "upgrade: changed force values stay");
    }
}

static void xinputTests(const std::wstring& logPath) {
    printf("xinput:\n");
    HMODULE x = LoadLibraryW(L"xinput1_4.dll");
    typedef DWORD (WINAPI *FnGetState)(DWORD, void*);
    FnGetState get = x ? (FnGetState)GetProcAddress(x, "XInputGetState") : nullptr;
    CHECK(get != nullptr, "xinput1_4.dll XInputGetState found");
    if (!get) return;
    const uint8_t* p = (const uint8_t*)get;
    int32_t rel = 0;
    memcpy(&rel, p + 1, 4);
    const uint8_t* stub = p + 5 + rel;
    CHECK(p[0] == 0xE9 && p[5] == 0x56 && p[6] == 0x57 && p[7] == 0x41, "XInputGetState starts with a 5-byte jump, the rest untouched");
    CHECK(p[0] == 0xE9 && stub[0] == 0x48 && stub[1] == 0xB8 && stub[10] == 0xFF && stub[11] == 0xE0, "XInputGetState redirected to the plugin");
    uint8_t state[16] = {};   // XINPUT_STATE
    DWORD r = get(0, state);
    for (int i = 0; i < 60 && r == 0 && !(state[5] & 0x10); ++i) { Sleep(25); r = get(0, state); }   // A is tapped every second
    CHECK(r == 0, "user 0 reads as connected while the wheel is active (%lu)", r);
    WORD buttons; memcpy(&buttons, state + 4, 2);
    CHECK((buttons & 0x1000) && state[7] == 128, "test mode wheel: A and half throttle read as A and right trigger (buttons 0x%04x, RT %u)", buttons, state[7]);
    // the log line
    FILE* f = _wfopen(logPath.c_str(), L"rb");
    std::string text;
    if (f) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof(b), f)) > 0) text.append(b, n); fclose(f); }
    CHECK(text.find("xinput: the wheel's buttons are reported to XInput readers") != std::string::npos, "log: xinput hook installed");
}

int wmain(int argc, wchar_t** argv) {
    if (HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, L"FT_SharedMem")) { printf("note: a head tracker is running (FT_SharedMem exists); the tracker checks will fail because it overrides the test pose\n"); CloseHandle(h); }
    if (argc < 3) { printf("usage: harness <game.exe> <FH2SimImmersion.dll> [--deep]\n"); return 2; }
    bool deep = argc > 3 && wcscmp(argv[3], L"--deep") == 0;
    uint8_t* base = mapImage(argv[1], deep ? 0x230000 : 0);
    if (!base) { printf("could not map game image\n"); return 2; }
    printf("game image mapped at %p\n", base);
    uint8_t original[5]; memcpy(original, base + 0x103832A, 5);
    uint8_t origFrame[5]; memcpy(origFrame, base + 0x226893, 5);

    // Emulate Azimuth for the wheel tests: bind the imports the plugin and the game use.
    HMODULE combase = LoadLibraryW(L"combase.dll");
    FnRawBuf rawBuf = combase ? (FnRawBuf)GetProcAddress(combase, "WindowsGetStringRawBuffer") : nullptr;
    FnWCS createString = combase ? (FnWCS)GetProcAddress(combase, "WindowsCreateString") : nullptr;
    if (!rawBuf || !createString) { printf("combase string functions missing\n"); return 2; }
    for (int i = 0; i < 3; ++i) g_hStaticsVtbl[i] = g_hViewVtbl[i];
    g_hStaticsVtbl[3] = g_hStaticsVtbl[4] = g_hStaticsVtbl[5] = (void*)h_Stub;
    for (int i = 0; i < 16; ++i) g_hPadVtbl[i] = g_hWheelVtbl[i] = (void*)h_Stub;
    g_hPadVtbl[0] = g_hWheelVtbl[0] = (void*)h_CtlQI;
    g_hPadVtbl[1] = g_hWheelVtbl[1] = (void*)h_AddRef;
    g_hPadVtbl[2] = g_hWheelVtbl[2] = (void*)h_Release;
    g_hPadVtbl[8] = g_hWheelVtbl[8] = (void*)h_GetUserNull;
    g_hStaticsVtbl[6] = (void*)h_GetControllers;
    for (int i = 7; i < 16; ++i) g_hStaticsVtbl[i] = (void*)h_Stub;
    *(void**)(base + 0x1dd1330) = (void*)&fakeGetActivationFactory;
    *(void**)(base + 0x1dd0ed8) = (void*)rawBuf;

    // No real wheel here: run the virtual wheel on synthetic input (TestMode) for this run.
    std::wstring iniPath = argv[2];
    std::wstring carsPath, logPath;
    {
        size_t dot = iniPath.find_last_of(L'.');
        iniPath = (dot == std::wstring::npos ? iniPath : iniPath.substr(0, dot)) + L".ini";
        size_t sep = iniPath.find_last_of(L"\\/");
        carsPath = (sep == std::wstring::npos ? std::wstring() : iniPath.substr(0, sep + 1)) + L"FH2SimImmersion_cars.ini";
        logPath = (sep == std::wstring::npos ? std::wstring() : iniPath.substr(0, sep + 1)) + L"FH2SimImmersion.log";
        WritePrivateProfileStringW(L"Input", L"ConfigVersion", L"12", iniPath.c_str());
        WritePrivateProfileStringW(L"Wheel", L"TestMode", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticABS", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticTC", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticSTM", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticClutch", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticAutomatic", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealisticSteeringLock", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"RealCockpitWheel", L"1", iniPath.c_str());
        WritePrivateProfileStringW(L"Simulation", L"HideDriverInCockpit", L"1", iniPath.c_str());
        for (const wchar_t* k : {L"SceneryDetail", L"TreeDetail", L"FenceDetail", L"GrassDetail"})
            WritePrivateProfileStringW(L"DrawDistance", k, L"2", iniPath.c_str());
        // per-car overrides: old two-field format (ABS off, TC on, STM auto) and the three-field format
        const ShiftPicks sp = pickShiftCars();
        const fh2::CarInfo* ov = sp.ov;
        if (ov) WritePrivateProfileStringW(L"Cars", std::to_wstring(ov->id).c_str(), L"off,on", carsPath.c_str());
        const fh2::CarInfo* ov3 = sp.ov3;
        if (ov3) WritePrivateProfileStringW(L"Cars", std::to_wstring(ov3->id).c_str(), L"auto,off,on", carsPath.c_str());
        // 4-field overrides: a clutch-yes car forced to "no clutch", a clutch-no car forced to "has a clutch"
        const fh2::CarInfo* cy = sp.cyOv;
        const fh2::CarInfo* cn = sp.cnOv;
        if (cy) WritePrivateProfileStringW(L"Cars", std::to_wstring(cy->id).c_str(), L"auto,auto,auto,off", carsPath.c_str());
        if (cn) WritePrivateProfileStringW(L"Cars", std::to_wstring(cn->id).c_str(), L"auto,auto,auto,on", carsPath.c_str());
        // automatic shifting: a manual car forced to automatic, an automatic-only car with an explicit clutch "on"
        if (sp.manualAuto) WritePrivateProfileStringW(L"Cars", std::to_wstring(sp.manualAuto->id).c_str(), L"auto,auto,auto,automatic", carsPath.c_str());
        if (sp.atOn) WritePrivateProfileStringW(L"Cars", std::to_wstring(sp.atOn->id).c_str(), L"auto,auto,auto,on", carsPath.c_str());
    }
    struct RestoreIni {
        std::wstring p, cars;
        ~RestoreIni() {
            WritePrivateProfileStringW(L"Wheel", L"TestMode", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticABS", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticTC", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticSTM", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticClutch", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticAutomatic", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealisticSteeringLock", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"RealCockpitWheel", L"0", p.c_str());
            WritePrivateProfileStringW(L"Simulation", L"HideDriverInCockpit", L"0", p.c_str());
            for (const wchar_t* k : {L"SceneryDetail", L"TreeDetail", L"FenceDetail", L"GrassDetail"})
                WritePrivateProfileStringW(L"DrawDistance", k, L"0", p.c_str());
            DeleteFileW(cars.c_str());
        }
    } restoreIni{iniPath, carsPath};

    const uintptr_t kVt0 = 0x2c5bfc8;
    const uint64_t absOrig = *(uint64_t*)(base + kVt0 + 0xd8), stmOrig = *(uint64_t*)(base + kVt0 + 0xe8),
                   tcOrig = *(uint64_t*)(base + kVt0 + 0xf0);
    const uint64_t clutchOrig = *(uint64_t*)(base + kVt0 + 0xf8);
    if (clutchOrig != (uint64_t)(base + 0xfbcb70)) { printf("clutch getter slot does not hold the expected pointer\n"); return 2; }

    const uint64_t carUpdateSlotOrig = *(uint64_t*)(base + 0x2c5cc00);
    *(uint64_t*)(base + 0x2c5cc00) = (uint64_t)&otherModCarUpdate;   // ExtraOptions hooks this slot before the plugin loads
    // another mod (ExtraOptions) replaced the driver camera's GetFOV before the plugin loads
    *(uint64_t*)(base + 0x2e78120 + 14 * 8) = (uint64_t)&otherModFov;
    // Azimuth's Scenery detail moves a call to a later entry that reads the caller's registers and stack
    { const int32_t rel = 0x1139210 - (0x11391d4 + 5); memcpy(base + 0x11391d4 + 1, &rel, 4); }
    HMODULE plugin = LoadLibraryW(argv[2]);
    if (!plugin) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }
    typedef BOOL (WINAPI *FnPluginInit)(const void*);
    FnPluginInit pluginInit = (FnPluginInit)GetProcAddress(plugin, "FH2PluginInitialize");
    CHECK(pluginInit != nullptr, "FH2PluginInitialize exported");
    CHECK(pluginInit && pluginInit(nullptr), "FH2PluginInitialize starts the mod");

    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // wait for the patch
    bool patched = false;
    for (int i = 0; i < 150 && !patched; ++i) { Sleep(100); patched = memcmp(base + 0x103832A, original, 5) != 0; }
    CHECK(patched, "plugin patched the yaw input call site");
    CHECK(base[0x103832A] == 0xE8, "call opcode preserved");

    // First packet sets the (identity) centre, then a real pose.
    sendPose(s, 0, 0, 0, 0, 0, 0); Sleep(50);
    sendPose(s, 4.0, 2.0, -3.0, 30.0, -10.0, 5.0); Sleep(150);

    g_carVtbl[0x70 / 8] = (void*)&carSlot14;
    FakeCar car; car.vtbl = g_carVtbl;
    EvalFn eval = (EvalFn)(base + 0x1037DF0);

    printf("cockpit yaw layer (HeadTrackingYaw -> CameraSpaceYPROffset, Param.x=12):\n");
    Layer rot = makeLayer(31, 4, 12.0f);
    eval(&rot, &car, nullptr, 0.016f);
    CHECK(std::fabs(f(rot, 0) - 1.0f) < 1e-4f, "input forced to 1.0 (got %f)", f(rot, 0));
    CHECK(std::fabs(f(rot, 0xE0) - 30.0f) < 1e-3f && std::fabs(f(rot, 0xE4) - 10.0f) < 1e-3f && std::fabs(f(rot, 0xE8) - 5.0f) < 1e-3f,
          "Param = yaw/pitch/roll (%f %f %f)", f(rot, 0xE0), f(rot, 0xE4), f(rot, 0xE8));
    CHECK(i32(rot, 0xAC) == 1, "mapping set to Linear");
    CHECK(f(rot, 0x58) == -2.0f && f(rot, 0x5C) == 2.0f, "clamp widened");

    printf("cockpit X layer (HeadTrackingX -> CarSpaceXYZOffset, Param.x=0.03):\n");
    Layer pos = makeLayer(28, 2, 0.03f);
    eval(&pos, &car, nullptr, 0.016f);
    CHECK(std::fabs(f(pos, 0) - 1.0f) < 1e-6f, "input forced to 1.0 (got %f)", f(pos, 0));
    CHECK(std::fabs(f(pos, 0xE0) - 0.04f) < 1e-5f && std::fabs(f(pos, 0xE4) - 0.02f) < 1e-5f && std::fabs(f(pos, 0xE8) + 0.03f) < 1e-5f,
          "Param = x/y/z metres (%f %f %f)", f(pos, 0xE0), f(pos, 0xE4), f(pos, 0xE8));

    printf("layers that must stay neutral:\n");
    Layer y = makeLayer(29, 2, 0.0f);
    *(float*)(y.b + 0xE4) = 1.0f;
    eval(&y, &car, nullptr, 0.016f);
    CHECK(f(y, 0) == 0.0f && f(y, 0xE4) == 1.0f, "HeadTrackingY layer gets 0 and is untouched");
    Layer pitch = makeLayer(32, 4, 5.0f);
    eval(&pitch, &car, nullptr, 0.016f);
    CHECK(f(pitch, 0) == 0.0f, "HeadTrackingPitch layer gets 0");
    Layer hood = makeLayer(31, 4, 15.0f);
    eval(&hood, &car, nullptr, 0.016f);
    CHECK(f(hood, 0) == 0.0f && f(hood, 0xE0) == 15.0f, "hood/bumper yaw layer untouched with CockpitOnly=1");
    Layer rot2 = makeLayer(31, 4, 12.0f);
    eval(&rot, &car, nullptr, 0.016f);
    eval(&rot2, &car, nullptr, 0.016f);
    CHECK(f(rot, 0) == 1.0f && f(rot2, 0) == 0.0f, "second cockpit rotation layer in the same frame is neutral");
    Sleep(20);

    printf("updates and staleness:\n");
    sendPose(s, 0, 0, 0, -45.0, 0, 0); Sleep(100);
    eval(&rot, &car, nullptr, 0.016f);
    CHECK(std::fabs(f(rot, 0xE0) + 45.0f) < 1e-3f, "new pose arrives (yaw %f)", f(rot, 0xE0));
    Sleep(900);   // > StaleTimeoutMs
    eval(&rot, &car, nullptr, 0.016f);
    CHECK(f(rot, 0) == 0.0f, "stale tracker -> input 0, camera returns to rest");

    printf("FreeTrack shared memory (Beam / TrackIR emulation):\n");
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"FT_SharedMem");
    float* ft = (float*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
    uint32_t* id = (uint32_t*)ft;
    // layout: DataID, CamW, CamH, Yaw, Pitch, Roll, X, Y, Z (radians, mm)
    for (int i = 0; i < 75; ++i) {
        ft[3] = -0.5f; ft[4] = 0.2f; ft[5] = 0.1f; ft[6] = 50.0f; ft[7] = -20.0f; ft[8] = 30.0f;
        *id += 1;
        sendPose(s, 0, 0, 0, 5.0, 0, 0);     // UDP keeps sending; FreeTrack must win
        Sleep(20);
    }
    eval(&rot, &car, nullptr, 0.016f);
    eval(&pos, &car, nullptr, 0.016f);
    CHECK(f(rot, 0) == 1.0f && std::fabs(f(rot, 0xE0) - 28.6479f) < 0.01f, "FreeTrack yaw -0.5 rad -> +28.65 deg (got %f)", f(rot, 0xE0));
    CHECK(std::fabs(f(rot, 0xE4) - 11.4592f) < 0.01f && std::fabs(f(rot, 0xE8) - 5.7296f) < 0.01f,
          "FreeTrack pitch/roll converted (%f %f)", f(rot, 0xE4), f(rot, 0xE8));
    CHECK(std::fabs(f(pos, 0xE0) - 0.05f) < 1e-4f && std::fabs(f(pos, 0xE4) + 0.02f) < 1e-4f && std::fabs(f(pos, 0xE8) - 0.03f) < 1e-4f,
          "FreeTrack mm -> metres (%f %f %f)", f(pos, 0xE0), f(pos, 0xE4), f(pos, 0xE8));

    wheelTests(base, origFrame, createString);
    ffbTests(base, plugin);
    assistTests(base, absOrig, stmOrig, tcOrig, clutchOrig);
    cameraTests(base);
    cockpitTests(base, plugin, carUpdateSlotOrig, logPath);
    xinputTests(logPath);
    drawDistTests(base, plugin);
    enableCheckTests(plugin);

    printf(failures ? "\n%d check(s) FAILED\n" : "\nall integration checks passed\n", failures);
    return failures ? 1 : 0;
}
