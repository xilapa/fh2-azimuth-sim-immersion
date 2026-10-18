// Hooks that present the virtual wheel to Forza Horizon 2.
#include "wheel_hook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include "log.h"
#include "patch_util.h"
#include "wheel_com.h"
#include "wheel_device.h"

// ---- forwarding thunks for the statics proxy ---------------------------------------------------
// fwd_thunk_N: swap `this` for the real object and jump into the real vtable slot N. Other
// registers and the stack are untouched, so any signature forwards correctly.
extern "C" void fwd_thunk_0();
extern "C" void fwd_thunk_1();
extern "C" void fwd_thunk_2();
extern "C" void fwd_thunk_3();
extern "C" void fwd_thunk_4();
extern "C" void fwd_thunk_5();
extern "C" void fwd_thunk_6();
extern "C" void fwd_thunk_7();
extern "C" void fwd_thunk_8();
extern "C" void fwd_thunk_9();
extern "C" void fwd_thunk_10();
extern "C" void fwd_thunk_11();
extern "C" void fwd_thunk_12();
extern "C" void fwd_thunk_13();
extern "C" void fwd_thunk_14();
extern "C" void fwd_thunk_15();
extern "C" void fwd_thunk_16();
extern "C" void fwd_thunk_17();
extern "C" void fwd_thunk_18();
extern "C" void fwd_thunk_19();
extern "C" void fwd_thunk_20();
extern "C" void fwd_thunk_21();
extern "C" void fwd_thunk_22();
extern "C" void fwd_thunk_23();
extern "C" void fwd_thunk_24();
extern "C" void fwd_thunk_25();
extern "C" void fwd_thunk_26();
extern "C" void fwd_thunk_27();
extern "C" void fwd_thunk_28();
extern "C" void fwd_thunk_29();
extern "C" void fwd_thunk_30();
extern "C" void fwd_thunk_31();
extern "C" void fwd_thunk_32();
extern "C" void fwd_thunk_33();
extern "C" void fwd_thunk_34();
extern "C" void fwd_thunk_35();
extern "C" void fwd_thunk_36();
extern "C" void fwd_thunk_37();
extern "C" void fwd_thunk_38();
extern "C" void fwd_thunk_39();
extern "C" void fwd_thunk_40();
extern "C" void fwd_thunk_41();
extern "C" void fwd_thunk_42();
extern "C" void fwd_thunk_43();
extern "C" void fwd_thunk_44();
extern "C" void fwd_thunk_45();
extern "C" void fwd_thunk_46();
extern "C" void fwd_thunk_47();
extern "C" void fwd_thunk_48();
extern "C" void fwd_thunk_49();
extern "C" void fwd_thunk_50();
extern "C" void fwd_thunk_51();
extern "C" void fwd_thunk_52();
extern "C" void fwd_thunk_53();
extern "C" void fwd_thunk_54();
extern "C" void fwd_thunk_55();
extern "C" void fwd_thunk_56();
extern "C" void fwd_thunk_57();
extern "C" void fwd_thunk_58();
extern "C" void fwd_thunk_59();
extern "C" void fwd_thunk_60();
extern "C" void fwd_thunk_61();
extern "C" void fwd_thunk_62();
extern "C" void fwd_thunk_63();
asm(".text\n.intel_syntax noprefix\n"
    ".globl fwd_thunk_0\n" "fwd_thunk_0:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+0]\n"
    ".globl fwd_thunk_1\n" "fwd_thunk_1:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+8]\n"
    ".globl fwd_thunk_2\n" "fwd_thunk_2:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+16]\n"
    ".globl fwd_thunk_3\n" "fwd_thunk_3:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+24]\n"
    ".globl fwd_thunk_4\n" "fwd_thunk_4:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+32]\n"
    ".globl fwd_thunk_5\n" "fwd_thunk_5:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+40]\n"
    ".globl fwd_thunk_6\n" "fwd_thunk_6:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+48]\n"
    ".globl fwd_thunk_7\n" "fwd_thunk_7:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+56]\n"
    ".globl fwd_thunk_8\n" "fwd_thunk_8:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+64]\n"
    ".globl fwd_thunk_9\n" "fwd_thunk_9:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+72]\n"
    ".globl fwd_thunk_10\n" "fwd_thunk_10:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+80]\n"
    ".globl fwd_thunk_11\n" "fwd_thunk_11:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+88]\n"
    ".globl fwd_thunk_12\n" "fwd_thunk_12:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+96]\n"
    ".globl fwd_thunk_13\n" "fwd_thunk_13:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+104]\n"
    ".globl fwd_thunk_14\n" "fwd_thunk_14:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+112]\n"
    ".globl fwd_thunk_15\n" "fwd_thunk_15:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+120]\n"
    ".globl fwd_thunk_16\n" "fwd_thunk_16:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+128]\n"
    ".globl fwd_thunk_17\n" "fwd_thunk_17:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+136]\n"
    ".globl fwd_thunk_18\n" "fwd_thunk_18:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+144]\n"
    ".globl fwd_thunk_19\n" "fwd_thunk_19:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+152]\n"
    ".globl fwd_thunk_20\n" "fwd_thunk_20:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+160]\n"
    ".globl fwd_thunk_21\n" "fwd_thunk_21:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+168]\n"
    ".globl fwd_thunk_22\n" "fwd_thunk_22:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+176]\n"
    ".globl fwd_thunk_23\n" "fwd_thunk_23:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+184]\n"
    ".globl fwd_thunk_24\n" "fwd_thunk_24:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+192]\n"
    ".globl fwd_thunk_25\n" "fwd_thunk_25:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+200]\n"
    ".globl fwd_thunk_26\n" "fwd_thunk_26:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+208]\n"
    ".globl fwd_thunk_27\n" "fwd_thunk_27:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+216]\n"
    ".globl fwd_thunk_28\n" "fwd_thunk_28:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+224]\n"
    ".globl fwd_thunk_29\n" "fwd_thunk_29:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+232]\n"
    ".globl fwd_thunk_30\n" "fwd_thunk_30:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+240]\n"
    ".globl fwd_thunk_31\n" "fwd_thunk_31:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+248]\n"
    ".globl fwd_thunk_32\n" "fwd_thunk_32:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+256]\n"
    ".globl fwd_thunk_33\n" "fwd_thunk_33:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+264]\n"
    ".globl fwd_thunk_34\n" "fwd_thunk_34:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+272]\n"
    ".globl fwd_thunk_35\n" "fwd_thunk_35:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+280]\n"
    ".globl fwd_thunk_36\n" "fwd_thunk_36:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+288]\n"
    ".globl fwd_thunk_37\n" "fwd_thunk_37:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+296]\n"
    ".globl fwd_thunk_38\n" "fwd_thunk_38:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+304]\n"
    ".globl fwd_thunk_39\n" "fwd_thunk_39:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+312]\n"
    ".globl fwd_thunk_40\n" "fwd_thunk_40:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+320]\n"
    ".globl fwd_thunk_41\n" "fwd_thunk_41:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+328]\n"
    ".globl fwd_thunk_42\n" "fwd_thunk_42:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+336]\n"
    ".globl fwd_thunk_43\n" "fwd_thunk_43:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+344]\n"
    ".globl fwd_thunk_44\n" "fwd_thunk_44:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+352]\n"
    ".globl fwd_thunk_45\n" "fwd_thunk_45:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+360]\n"
    ".globl fwd_thunk_46\n" "fwd_thunk_46:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+368]\n"
    ".globl fwd_thunk_47\n" "fwd_thunk_47:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+376]\n"
    ".globl fwd_thunk_48\n" "fwd_thunk_48:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+384]\n"
    ".globl fwd_thunk_49\n" "fwd_thunk_49:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+392]\n"
    ".globl fwd_thunk_50\n" "fwd_thunk_50:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+400]\n"
    ".globl fwd_thunk_51\n" "fwd_thunk_51:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+408]\n"
    ".globl fwd_thunk_52\n" "fwd_thunk_52:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+416]\n"
    ".globl fwd_thunk_53\n" "fwd_thunk_53:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+424]\n"
    ".globl fwd_thunk_54\n" "fwd_thunk_54:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+432]\n"
    ".globl fwd_thunk_55\n" "fwd_thunk_55:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+440]\n"
    ".globl fwd_thunk_56\n" "fwd_thunk_56:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+448]\n"
    ".globl fwd_thunk_57\n" "fwd_thunk_57:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+456]\n"
    ".globl fwd_thunk_58\n" "fwd_thunk_58:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+464]\n"
    ".globl fwd_thunk_59\n" "fwd_thunk_59:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+472]\n"
    ".globl fwd_thunk_60\n" "fwd_thunk_60:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+480]\n"
    ".globl fwd_thunk_61\n" "fwd_thunk_61:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+488]\n"
    ".globl fwd_thunk_62\n" "fwd_thunk_62:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+496]\n"
    ".globl fwd_thunk_63\n" "fwd_thunk_63:\n" "mov rcx, qword ptr [rcx+8]\n" "mov rax, qword ptr [rcx]\n" "jmp qword ptr [rax+504]\n"
    ".att_syntax prefix\n");

namespace fh2 {

namespace {

constexpr uintptr_t kIatGetActivation = 0x1dd1330;
constexpr uintptr_t kIatUnboundHint   = 0x2c42474;
constexpr uintptr_t kControllerMgrRva = 0x365d7c8;
constexpr uintptr_t kRescanRva        = 0x1073120;
constexpr uintptr_t kUpdateRva        = 0x1075490;
constexpr uintptr_t kFrameSiteBytes   = 0x22688c;    // 12 bytes, call at +7 (rva 0x226893)
constexpr uintptr_t kFrameCallSite    = 0x226893;
constexpr uintptr_t kFfbUpdateRva     = 0x10743c0;   // UpdateFFB(slot, const float in[3])
constexpr uintptr_t kFfbSiteBytes     = 0x2fe12d;    // 12 bytes, call at +7 (rva 0x2fe134)
constexpr uintptr_t kFfbCallSite      = 0x2fe134;
const uint8_t kFfbSiteExpected[12] = {0x48, 0x8D, 0x55, 0xA8, 0x48, 0x8B, 0xCF, 0xE8, 0x87, 0x62, 0xD7, 0x00};
const uint8_t kFrameSiteExpected[12] = {0x48, 0x8B, 0x0D, 0x35, 0x6F, 0x43, 0x03, 0xE8, 0xF8, 0xEB, 0xE4, 0x00};

constexpr GUID mkGuid(unsigned long a, unsigned short b, unsigned short c, unsigned char d0, unsigned char d1,
                      unsigned char d2, unsigned char d3, unsigned char d4, unsigned char d5, unsigned char d6,
                      unsigned char d7) {
    return GUID{a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}};
}
constexpr GUID kIidUnknown  = mkGuid(0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
constexpr GUID kIidInspect  = mkGuid(0xAF86E2E0, 0xB12D, 0x4C6A, 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90);
constexpr GUID kIidAgile    = mkGuid(0x94EA2B94, 0xE9CC, 0x49E0, 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90);
constexpr GUID kIidStatics  = mkGuid(0x7400624f, 0x144b, 0x47fc, 0x9f, 0x3b, 0x1d, 0xca, 0xfb, 0x48, 0x28, 0x73);
constexpr GUID kIidWheel    = mkGuid(0x646979cf, 0x6b71, 0x4e96, 0x8d, 0xf9, 0x59, 0xe3, 0x98, 0xd7, 0x42, 0x0c);

constexpr HRESULT kEBounds = (HRESULT)0x8000000BL;

bool same(const GUID* a, const GUID& b) { return a && std::memcmp(a, &b, sizeof(GUID)) == 0; }
bool isBaseIid(const GUID* r) { return same(r, kIidUnknown) || same(r, kIidInspect) || same(r, kIidAgile); }

uintptr_t   g_base = 0;
WheelConfig g_wc;
bool        g_diag = false;
std::atomic<bool> g_rescanPending{false};
std::atomic<bool> g_azimuthHasWheel{false};

typedef HRESULT (*GetActivationFn)(const wchar_t*, const GUID*, void**);
GetActivationFn g_origGetActivation = nullptr;

// ---- raw COM helpers -------------------------------------------------------------------------
void** vtblOf(void* obj) { return *reinterpret_cast<void***>(obj); }
ULONG comAddRef(void* o) { return reinterpret_cast<ULONG (*)(void*)>(vtblOf(o)[1])(o); }
ULONG comRelease(void* o) { return reinterpret_cast<ULONG (*)(void*)>(vtblOf(o)[2])(o); }
HRESULT comQI(void* o, const GUID& iid, void** out) {
    return reinterpret_cast<HRESULT (*)(void*, const GUID*, void**)>(vtblOf(o)[0])(o, &iid, out);
}
HRESULT viewSize(void* v, uint32_t* n) {
    return reinterpret_cast<HRESULT (*)(void*, uint32_t*)>(vtblOf(v)[7])(v, n);
}
HRESULT viewGetAt(void* v, uint32_t i, void** out) {
    return reinterpret_cast<HRESULT (*)(void*, uint32_t, void**)>(vtblOf(v)[6])(v, i, out);
}

constexpr GUID kIidUserStatics = mkGuid(0xb4f23a93, 0x7fc6, 0x4a50, 0xb1, 0x53, 0x95, 0x00, 0x16, 0x38, 0x9e, 0x50);

// The signed-in system user (Windows.Xbox.System.User statics, get_Users[0]); AddRef'd, or nullptr.
void* systemUser() {
    static std::atomic<bool> failLogged{false};
    HRESULT hr = (HRESULT)0x80004005L;
    void* statics = nullptr;
    void* view = nullptr;
    void* user = nullptr;
    if (g_origGetActivation) {
        hr = g_origGetActivation(L"Windows.Xbox.System.User", &kIidUserStatics, &statics);
        if (SUCCEEDED(hr) && !statics) hr = (HRESULT)0x80004003L;
    }
    if (SUCCEEDED(hr)) {
        hr = reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(statics)[9])(statics, &view);
        if (SUCCEEDED(hr) && !view) hr = (HRESULT)0x80004003L;
    }
    if (SUCCEEDED(hr)) {
        uint32_t n = 0;
        hr = viewSize(view, &n);
        if (SUCCEEDED(hr) && n == 0) hr = kEBounds;
        if (SUCCEEDED(hr)) {
            hr = viewGetAt(view, 0, &user);
            if (SUCCEEDED(hr) && !user) hr = (HRESULT)0x80004003L;
        }
    }
    if (view) comRelease(view);
    if (statics) comRelease(statics);
    if (FAILED(hr)) {
        if (!failLogged.exchange(true)) LOG("wheel: no system user available (0x%08lx)", (unsigned long)hr);
        return nullptr;
    }
    return user;
}

struct SeenLog {
    GUID g[16];
    int n = 0;
    void note(const GUID* id, const char* what) {
        for (int i = 0; i < n; ++i) if (std::memcmp(&g[i], id, sizeof(GUID)) == 0) return;
        if (n < 16) g[n++] = *id;
        LOG("wheel: %s QI for unknown IID {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", what, id->Data1,
            id->Data2, id->Data3, id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3], id->Data4[4],
            id->Data4[5], id->Data4[6], id->Data4[7]);
    }
};
SeenLog g_listSeen;

// ---- ListProxy -----------------------------------------------------------------------------
struct ListProxy { const void* const* vtbl; void* realView; LONG refs; bool withFake; };

uint32_t realCount(ListProxy* p) {
    uint32_t n = 0;
    if (FAILED(viewSize(p->realView, &n))) n = 0;
    return n;
}

HRESULT lp_QI(void* self, const GUID* riid, void** ppv) {
    ListProxy* p = static_cast<ListProxy*>(self);
    if (!ppv) return (HRESULT)0x80004003L;
    if (isBaseIid(riid)) { InterlockedIncrement(&p->refs); *ppv = p; return S_OK; }
    if (!riid) { *ppv = nullptr; return (HRESULT)0x80004003L; }
    g_listSeen.note(riid, "view");
    return comQI(p->realView, *riid, ppv);
}
ULONG lp_AddRef(void* self) { return (ULONG)InterlockedIncrement(&static_cast<ListProxy*>(self)->refs); }
ULONG lp_Release(void* self) {
    ListProxy* p = static_cast<ListProxy*>(self);
    LONG n = InterlockedDecrement(&p->refs);
    if (n == 0) { comRelease(p->realView); delete p; }
    return (ULONG)n;
}
template <int N> HRESULT lpForward(void* self, void* a, void* b) {   // slots 3..5: (this, a, b)
    ListProxy* p = static_cast<ListProxy*>(self);
    return reinterpret_cast<HRESULT (*)(void*, void*, void*)>(vtblOf(p->realView)[N])(p->realView, a, b);
}
HRESULT lp_GetAt(void* self, uint32_t i, void** out) {
    ListProxy* p = static_cast<ListProxy*>(self);
    if (!out) return S_OK;
    uint32_t n = realCount(p);
    if (i < n) return viewGetAt(p->realView, i, out);
    if (i == n && p->withFake) {
        void* c = VirtualWheel_Controller();
        comAddRef(c);
        *out = c;
        return S_OK;
    }
    *out = nullptr;
    return kEBounds;
}
HRESULT lp_Size(void* self, uint32_t* out) {
    ListProxy* p = static_cast<ListProxy*>(self);
    if (out) *out = realCount(p) + (p->withFake ? 1 : 0);
    return S_OK;
}
HRESULT lp_IndexOf(void* self, void* item, uint32_t* idx, uint8_t* found) {
    ListProxy* p = static_cast<ListProxy*>(self);
    if (p->withFake && item == VirtualWheel_Controller()) {
        if (idx) *idx = realCount(p);
        if (found) *found = 1;
        return S_OK;
    }
    return reinterpret_cast<HRESULT (*)(void*, void*, uint32_t*, uint8_t*)>(vtblOf(p->realView)[8])(
        p->realView, item, idx, found);
}
HRESULT lp_GetMany(void* self, uint32_t start, uint32_t cap, void** items, uint32_t* actual) {
    ListProxy* p = static_cast<ListProxy*>(self);
    uint32_t size = realCount(p) + (p->withFake ? 1 : 0);
    uint32_t got = 0;
    while (items && start + got < size && got < cap) {
        void* e = nullptr;
        if (FAILED(lp_GetAt(self, start + got, &e))) break;
        items[got++] = e;
    }
    if (actual) *actual = got;
    return S_OK;
}
const void* const g_listVtbl[10] = {
    reinterpret_cast<const void*>(&lp_QI), reinterpret_cast<const void*>(&lp_AddRef),
    reinterpret_cast<const void*>(&lp_Release), reinterpret_cast<const void*>(&lpForward<3>),
    reinterpret_cast<const void*>(&lpForward<4>), reinterpret_cast<const void*>(&lpForward<5>),
    reinterpret_cast<const void*>(&lp_GetAt), reinterpret_cast<const void*>(&lp_Size),
    reinterpret_cast<const void*>(&lp_IndexOf), reinterpret_cast<const void*>(&lp_GetMany),
};

// ---- StaticsProxy -----------------------------------------------------------------------------------
struct StaticsProxy { const void* const* vtbl; void* real; LONG refs; };
extern const void* const g_staticsVtbl[64];
StaticsProxy g_proxy = {g_staticsVtbl, nullptr, 0};
CRITICAL_SECTION g_proxyCs;

HRESULT sp_QI(void* self, const GUID* riid, void** ppv) {
    StaticsProxy* p = static_cast<StaticsProxy*>(self);
    if (!ppv) return (HRESULT)0x80004003L;
    if (isBaseIid(riid) || same(riid, kIidStatics)) {
        InterlockedIncrement(&p->refs);
        *ppv = p;
        return S_OK;
    }
    if (!riid) { *ppv = nullptr; return (HRESULT)0x80004003L; }
    return comQI(p->real, *riid, ppv);
}
ULONG sp_AddRef(void* self) { return (ULONG)InterlockedIncrement(&static_cast<StaticsProxy*>(self)->refs); }
ULONG sp_Release(void* self) { return (ULONG)InterlockedDecrement(&static_cast<StaticsProxy*>(self)->refs); }

HRESULT sp_GetControllers(void* self, void** out) {
    StaticsProxy* p = static_cast<StaticsProxy*>(self);
    void* real = p->real;
    HRESULT hr = reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(real)[6])(real, out);
    if (FAILED(hr) || !out || !*out) return hr;
    void* view = *out;

    if (g_azimuthHasWheel.load()) return hr;
    uint32_t n = 0;
    if (FAILED(viewSize(view, &n))) n = 0;
    for (uint32_t i = 0; i < n; ++i) {
        void* c = nullptr;
        if (FAILED(viewGetAt(view, i, &c)) || !c) continue;
        void* w = nullptr;
        bool isWheel = SUCCEEDED(comQI(c, kIidWheel, &w)) && w;
        if (w) comRelease(w);
        comRelease(c);
        if (isWheel) {
            g_azimuthHasWheel = true;
            LOG("wheel: Azimuth already provides a wheel; virtual wheel disabled");
            return hr;
        }
    }

    if (g_wc.userMode == WheelUserMode::Auto) {
        void* user = nullptr;
        if (n >= 1) {
            void* c = nullptr;
            if (SUCCEEDED(viewGetAt(view, 0, &c)) && c) {
                HRESULT uh = reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(c)[8])(c, &user);
                if (FAILED(uh)) user = nullptr;
                comRelease(c);
            }
        }
        if (!user) {
            user = systemUser();
            static std::atomic<bool> fallbackLogged{false};
            if (user && !fallbackLogged.exchange(true))
                LOG("wheel: no Xbox controller user, using the signed-in system user");
        }
        if (user) VirtualWheel_SetUser(user);
    }
    static std::atomic<bool> userLogged{false};
    if (!userLogged.exchange(true)) {
        // Read back through the fake's own get_User so the log shows what the game will see.
        void* u = nullptr;
        void* fake = VirtualWheel_Controller();
        reinterpret_cast<HRESULT (*)(void*, void**)>(vtblOf(fake)[8])(fake, &u);
        LOG("wheel: user %p (mode %s)", u, g_wc.userMode == WheelUserMode::Auto ? "auto" : "none");
        if (u) comRelease(u);
    }

    ListProxy* lp = new (std::nothrow) ListProxy{g_listVtbl, view, 1, true};
    if (!lp) return hr;
    static std::atomic<bool> addedLogged{false};
    if (!addedLogged.exchange(true)) LOG("wheel: virtual wheel added to the controller list (index %u)", n);
    *out = lp;
    return hr;
}

const void* const g_staticsVtbl[64] = {
    reinterpret_cast<const void*>(&sp_QI), reinterpret_cast<const void*>(&sp_AddRef),
    reinterpret_cast<const void*>(&sp_Release), (const void*)&fwd_thunk_3, (const void*)&fwd_thunk_4,
    (const void*)&fwd_thunk_5, reinterpret_cast<const void*>(&sp_GetControllers),
    (const void*)&fwd_thunk_7, (const void*)&fwd_thunk_8, (const void*)&fwd_thunk_9, (const void*)&fwd_thunk_10,
    (const void*)&fwd_thunk_11, (const void*)&fwd_thunk_12, (const void*)&fwd_thunk_13, (const void*)&fwd_thunk_14,
    (const void*)&fwd_thunk_15, (const void*)&fwd_thunk_16, (const void*)&fwd_thunk_17, (const void*)&fwd_thunk_18,
    (const void*)&fwd_thunk_19, (const void*)&fwd_thunk_20, (const void*)&fwd_thunk_21, (const void*)&fwd_thunk_22,
    (const void*)&fwd_thunk_23, (const void*)&fwd_thunk_24, (const void*)&fwd_thunk_25, (const void*)&fwd_thunk_26,
    (const void*)&fwd_thunk_27, (const void*)&fwd_thunk_28, (const void*)&fwd_thunk_29, (const void*)&fwd_thunk_30,
    (const void*)&fwd_thunk_31, (const void*)&fwd_thunk_32, (const void*)&fwd_thunk_33, (const void*)&fwd_thunk_34,
    (const void*)&fwd_thunk_35, (const void*)&fwd_thunk_36, (const void*)&fwd_thunk_37, (const void*)&fwd_thunk_38,
    (const void*)&fwd_thunk_39, (const void*)&fwd_thunk_40, (const void*)&fwd_thunk_41, (const void*)&fwd_thunk_42,
    (const void*)&fwd_thunk_43, (const void*)&fwd_thunk_44, (const void*)&fwd_thunk_45, (const void*)&fwd_thunk_46,
    (const void*)&fwd_thunk_47, (const void*)&fwd_thunk_48, (const void*)&fwd_thunk_49, (const void*)&fwd_thunk_50,
    (const void*)&fwd_thunk_51, (const void*)&fwd_thunk_52, (const void*)&fwd_thunk_53, (const void*)&fwd_thunk_54,
    (const void*)&fwd_thunk_55, (const void*)&fwd_thunk_56, (const void*)&fwd_thunk_57, (const void*)&fwd_thunk_58,
    (const void*)&fwd_thunk_59, (const void*)&fwd_thunk_60, (const void*)&fwd_thunk_61, (const void*)&fwd_thunk_62,
    (const void*)&fwd_thunk_63,
};

// ---- activation factory hook ---------------------------------------------------------------------
HRESULT hookGetActivationFactory(const wchar_t* name, const GUID* iid, void** out) {
    HRESULT hr = g_origGetActivation(name, iid, out);
    if (SUCCEEDED(hr) && out && *out && name && iid && wcscmp(name, L"Windows.Xbox.Input.Controller") == 0 &&
        std::memcmp(iid, &kIidStatics, sizeof(GUID)) == 0) {
        void* real = *out;
        EnterCriticalSection(&g_proxyCs);
        if (g_proxy.real != real) {
            comAddRef(real);
            void* old = g_proxy.real;
            g_proxy.real = real;
            if (old) comRelease(old);
        }
        LeaveCriticalSection(&g_proxyCs);
        comRelease(real);                      // the proxy holds its own reference
        InterlockedIncrement(&g_proxy.refs);
        *out = &g_proxy;
    }
    return hr;
}

// ---- per-frame hook -------------------------------------------------------------------------------
void hookControllerUpdate(void* mgr, float dt) {
    if (g_rescanPending.load() && mgr) {
        g_rescanPending = false;
        LOG("wheel: rescanning controllers");
        reinterpret_cast<void (*)(void*)>(g_base + kRescanRva)(mgr);
    }
    reinterpret_cast<void (*)(void*, float)>(g_base + kUpdateRva)(mgr, dt);
}

// ---- UpdateFFB hook (game thread, once per frame) --------------------------------------------------
// Captures the inputs and the driver's parameters, then runs the original. in[] is never modified.
extern "C" void hookFfbUpdate(uint8_t* slot, const float* in) {
    if (slot && in) {
        GameFfbInput g;
        g.rawTorque = in[0]; g.speed = in[1]; g.slip = in[2];
        const float* d = *reinterpret_cast<float* const*>(slot + 0x10c0);
        if (d) {
            g.gain = d[0];
            g.dynScale = d[0x28 / 4]; g.dynMax = d[0x2c / 4]; g.springParam = d[0x48 / 4];
            g.slip0 = d[0x54 / 4]; g.slip1 = d[0x58 / 4]; g.slipScale0 = d[0x5c / 4]; g.slipScale1 = d[0x60 / 4];
            g.speed0 = d[0x64 / 4]; g.speed1 = d[0x68 / 4]; g.speedScale0 = d[0x6c / 4]; g.speedScale1 = d[0x70 / 4];
            VirtualWheel_SetGameFfbInput(g);
        }
    }
    reinterpret_cast<void (*)(uint8_t*, const float*)>(g_base + kFfbUpdateRva)(slot, in);
}

// ---- diagnostics ----------------------------------------------------------------------------------
DWORD WINAPI diagThread(void*) {
    for (;;) {
        Sleep(2000);
        WheelInput in;
        bool have = WheelDevice_Read(in);
        char btn[40]; size_t n = 0;
        auto add = [&](bool b, const char* s) { if (b) n += snprintf(btn + n, sizeof(btn) - n, "%s", s); };
        add(in.a, "A"); add(in.b, "B"); add(in.x, "X"); add(in.y, "Y"); add(in.view, "V"); add(in.menu, "M");
        add(in.lb, "l"); add(in.rb, "r"); add(in.up, "^"); add(in.down, "v"); add(in.left, "<"); add(in.right, ">");
        if (n == 0) { btn[0] = '-'; btn[1] = 0; }
        char vars[1024];
        VirtualWheel_FormatVars(vars, sizeof(vars));
        LOG("wheel diag: steer %.3f thr %.2f brk %.2f clu %.2f btn %s gear %d | game rot %.0f | vars: %s",
            in.steering, in.throttle, in.brake, in.clutch, btn, in.gear, VirtualWheel_GameRotation(), vars);
        if (!have) continue;
        char pressed[256]; size_t pn = 0; pressed[0] = 0;
        for (int i = 0; i < 128 && pn < sizeof(pressed) - 1; ++i)
            if (in.rawButtons[i / 32] & (1u << (i % 32)))
                pn += snprintf(pressed + pn, sizeof(pressed) - pn, pn ? ",%d" : "%d", i);
        if (pn >= sizeof(pressed)) pn = sizeof(pressed) - 1;
        LOG("wheel diag raw: X %ld Y %ld Z %ld RX %ld RY %ld RZ %ld S0 %ld S1 %ld buttons %s",
            (long)in.raw[0], (long)in.raw[1], (long)in.raw[2], (long)in.raw[3], (long)in.raw[4],
            (long)in.raw[5], (long)in.raw[6], (long)in.raw[7], pn ? pressed : "none");
        FfbStatus fs;
        if (WheelDevice_FfbStatus(fs))
        {
            char game[96];
            if (fs.haveGame)
                snprintf(game, sizeof(game), " | game raw %.3f speed %.1f slip %.3f gain %.2f", fs.game.rawTorque,
                         fs.game.speed, fs.game.slip, fs.game.gain);
            else
                snprintf(game, sizeof(game), " | game input none");
            LOG("wheel diag ffb: %s F %.3f (torque %.3f spring %.3f damp %.3f rumble %.3f menu %.3f) angle %.3f vel %.2f out %ld slip %.3f%s",
                fs.stale ? "stale" : fs.race ? "race" : "menu", fs.terms.total, fs.terms.torque, fs.terms.spring,
                fs.terms.damping, fs.terms.rumble, fs.terms.menu, fs.angle, fs.velocity, fs.mag, fs.terms.slip, game);
        }
    }
}

} // namespace

void WheelHook_Install(uintptr_t base, const Config& cfg) {
    if (!cfg.wheel.enabled) { LOG("wheel: disabled in settings"); return; }
    g_base = base;
    g_wc = cfg.wheel;
    g_diag = cfg.diagnostics;
    InitializeCriticalSection(&g_proxyCs);
    VirtualWheel_Configure(cfg);
    VirtualWheel_SetGameBase(base);
    if (!WheelDevice_Start(cfg.wheel, cfg.diagnostics)) { LOG("wheel: no usable device; wheel hooks not installed"); return; }

    // IAT hook: wait until Azimuth has bound the slot (file value is an unbound RVA hint).
    void** slot = reinterpret_cast<void**>(base + kIatGetActivation);
    void* orig = nullptr;
    for (int i = 0; i < 15000; ++i) {
        orig = *slot;
        if (orig && reinterpret_cast<uintptr_t>(orig) != kIatUnboundHint) break;
        orig = nullptr;
        Sleep(1);
    }
    if (!orig) { LOG("wheel: activation import never bound by Azimuth; wheel hooks not installed"); return; }
    g_origGetActivation = reinterpret_cast<GetActivationFn>(orig);
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        LOG("wheel: VirtualProtect on the import slot failed %lu", GetLastError());
        return;
    }
    *slot = reinterpret_cast<void*>(&hookGetActivationFactory);
    VirtualProtect(slot, sizeof(void*), old, &old);
    LOG("wheel: activation hook installed");

    // Per-frame hook (controller rescan).
    const uint8_t* site = reinterpret_cast<const uint8_t*>(base + kFrameSiteBytes);
    if (std::memcmp(site, kFrameSiteExpected, sizeof(kFrameSiteExpected)) != 0) {
        LOG("wheel: frame hook site mismatch; rescan disabled");
    } else {
        void* mgr = *reinterpret_cast<void**>(base + kControllerMgrRva);
        g_rescanPending = cfg.wheel.forceRescan && mgr != nullptr;
        LOG("wheel: controller manager %p, rescan %s", mgr,
            g_rescanPending.load() ? "scheduled" : (cfg.wheel.forceRescan ? "not needed yet" : "off"));
        uint8_t* thunk = static_cast<uint8_t*>(allocNear(base + kFrameCallSite, 4096));
        if (!thunk) {
            LOG("wheel: could not allocate frame hook memory; rescan disabled");
            g_rescanPending = false;
        } else if (!patchCallRel32(base + kFrameCallSite, reinterpret_cast<void*>(&hookControllerUpdate), thunk)) {
            LOG("wheel: frame hook failed; rescan disabled");
            g_rescanPending = false;
        } else {
            DWORD o;
            VirtualProtect(thunk, 4096, PAGE_EXECUTE_READ, &o);
            LOG("wheel: frame hook installed (thunk %p)", (void*)thunk);
        }
    }

    // Game FFB input hook (UpdateFFB call site): the plugin sees what the car computes.
    if (cfg.wheel.ffbEnabled) {
        const uint8_t* fsite = reinterpret_cast<const uint8_t*>(base + kFfbSiteBytes);
        if (std::memcmp(fsite, kFfbSiteExpected, sizeof(kFfbSiteExpected)) != 0) {
            LOG("wheel ffb: FFB input hook site mismatch; using game variables only");
        } else {
            uint8_t* fthunk = static_cast<uint8_t*>(allocNear(base + kFfbCallSite, 4096));
            if (!fthunk || !patchCallRel32(base + kFfbCallSite, reinterpret_cast<void*>(&hookFfbUpdate), fthunk)) {
                LOG("wheel ffb: FFB input hook failed; using game variables only");
            } else {
                DWORD o;
                VirtualProtect(fthunk, 4096, PAGE_EXECUTE_READ, &o);
                LOG("wheel ffb: game FFB input hook installed");
            }
        }
    }

    if (cfg.diagnostics)
        if (HANDLE t = CreateThread(nullptr, 0, diagThread, nullptr, 0, nullptr)) CloseHandle(t);
}

} // namespace fh2
