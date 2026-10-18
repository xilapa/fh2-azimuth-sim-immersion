// Integration test (runs under Windows or Wine):
//   1. maps the real forza_durango_release_final.exe into memory the way a loader would,
//   2. loads FH2SimImmersion.dll, which must find, verify and patch the image,
//   3. sends OpenTrack UDP packets,
//   4. calls the game's own CameraLayer::EvaluateInput on fake layers and checks results.
// usage: harness.exe <path-to-game-exe> <path-to-FH2SimImmersion.dll> [--deep]
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

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
static HRESULT MSABI h_ViewGetAt(void*, uint32_t, void** out) { if (out) *out = nullptr; return (HRESULT)0x8000000BL; }
static HRESULT MSABI h_ViewSize(void*, uint32_t* n) { *n = 0; return S_OK; }
static HRESULT MSABI h_ViewIndexOf(void*, void*, uint32_t* i, uint8_t* f) { *i = 0; *f = 0; return S_OK; }
static HRESULT MSABI h_ViewGetMany(void*, uint32_t, uint32_t, void**, uint32_t* a) { *a = 0; return S_OK; }
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
    CHECK(size == 1, "list size == 1 (got %u)", size);
    void* ctrl = nullptr;
    HRESULT hr = ((FnGetAt)slotOf(view, 6))(view, 0, &ctrl);
    CHECK(hr == S_OK && ctrl, "GetAt(0) succeeds");
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

int wmain(int argc, wchar_t** argv) {
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
    g_hStaticsVtbl[6] = (void*)h_GetControllers;
    for (int i = 7; i < 16; ++i) g_hStaticsVtbl[i] = (void*)h_Stub;
    *(void**)(base + 0x1dd1330) = (void*)&fakeGetActivationFactory;
    *(void**)(base + 0x1dd0ed8) = (void*)rawBuf;

    // No real wheel here: run the virtual wheel on synthetic input (TestMode) for this run.
    std::wstring iniPath = argv[2];
    {
        size_t dot = iniPath.find_last_of(L'.');
        iniPath = (dot == std::wstring::npos ? iniPath : iniPath.substr(0, dot)) + L".ini";
        WritePrivateProfileStringW(L"Input", L"ConfigVersion", L"4", iniPath.c_str());
        WritePrivateProfileStringW(L"Wheel", L"TestMode", L"1", iniPath.c_str());
    }
    struct RestoreIni {
        std::wstring p;
        ~RestoreIni() { WritePrivateProfileStringW(L"Wheel", L"TestMode", L"0", p.c_str()); }
    } restoreIni{iniPath};

    if (!LoadLibraryW(argv[2])) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }

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

    printf(failures ? "\n%d check(s) FAILED\n" : "\nall integration checks passed\n", failures);
    return failures ? 1 : 0;
}
