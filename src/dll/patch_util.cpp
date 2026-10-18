#include "patch_util.h"
#include <tlhelp32.h>
#include <cstring>
#include "log.h"

namespace fh2 {

void* allocNear(uintptr_t base, size_t size) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t range = 0x70000000;
    uintptr_t lo = base > range ? base - range : gran;
    for (uintptr_t a = (base - gran) & ~(gran - 1); a > lo; a -= gran) {
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(a), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return p;
    }
    for (uintptr_t a = (base + 0x4000000) & ~(gran - 1); a < base + range; a += gran) {
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(a), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return p;
    }
    return nullptr;
}

void Suspended::suspendOthers() {
    handles.reserve(4096);
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    std::vector<DWORD> ids; ids.reserve(4096);
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ids.push_back(te.th32ThreadID);
        te.dwSize = sizeof(te);
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    // From here on no heap allocation until resume (a suspended thread may hold the heap lock).
    for (DWORD id : ids) {
        if (handles.size() == handles.capacity()) break;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, id);
        if (!h) continue;
        if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); continue; }
        handles.push_back(h);
    }
}

void Suspended::resumeAll() {
    for (HANDLE h : handles) { ResumeThread(h); CloseHandle(h); }
    handles.clear();
}

bool patchCallRel32(uintptr_t callSite, void* target, void* thunkMem) {
    uint8_t* t = static_cast<uint8_t*>(thunkMem);
    intptr_t rel = reinterpret_cast<intptr_t>(t) - static_cast<intptr_t>(callSite + 5);
    if (rel > INT32_MAX || rel < INT32_MIN) { LOG("patch: thunk out of rel32 range"); return false; }
    int32_t rel32 = static_cast<int32_t>(rel);

    uint8_t* p = t;
    *p++ = 0x48; *p++ = 0xB8;                         // mov rax, imm64
    uint64_t a = reinterpret_cast<uint64_t>(target); std::memcpy(p, &a, 8); p += 8;
    *p++ = 0xFF; *p++ = 0xE0;                         // jmp rax
    FlushInstructionCache(GetCurrentProcess(), t, 12);

    // Make the call site writable once, outside the suspended window.
    uint8_t* site = reinterpret_cast<uint8_t*>(callSite);
    DWORD prot;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &prot)) {
        LOG("patch: VirtualProtect failed %lu", GetLastError());
        return false;
    }
    Suspended sus;
    sus.suspendOthers();
    std::memcpy(site + 1, &rel32, 4);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    sus.resumeAll();
    VirtualProtect(site, 5, prot, &prot);
    return true;
}

} // namespace fh2
