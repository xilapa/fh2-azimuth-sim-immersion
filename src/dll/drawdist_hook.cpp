// SPDX-License-Identifier: MIT
// Pop-in killer (no game files are edited):
//  A. track settings: how far trees, scenery and props are drawn, the grass end and its detail level, all grown with their
//     multipliers, and the scenery multiplier applied per track;
//  C. scenery level of detail (buildings, props and everything else drawn per model): the render loop's calls of the level selection
//     also draw a finer level farther away when its resource is ready; the level the game draws itself is never hidden;
//  E. placed trees, fences and grass: their detail switches move out as their template library loads.
#include "drawdist_hook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <vector>
#include "patch_util.h"
#include "log.h"

namespace fh2 {
namespace {

// ---- A. track settings
constexpr uintptr_t kThunkRva        = 0x134af90;   // add rcx,0x8110 ; jmp Serialize
constexpr uintptr_t kThunkJmpRva     = 0x134af97;   // the E9 rel32
constexpr uintptr_t kSerializeRva    = 0x1144320;
constexpr uint8_t   kThunkBytes[12]  = {0x48, 0x81, 0xC1, 0x10, 0x81, 0x00, 0x00, 0xE9, 0x84, 0x93, 0xDF, 0xFF};

constexpr int kOffGrassEnd = 0x168, kOffGrassStart = 0x164, kOffWorld = 0x5e4, kOffProps = 0x32c;
constexpr float kGrassMax = 500.0f;                 // metres: GrassDetail never draws grass farther
constexpr float kFarMax = 3000.0f;                  // metres: the world objects and props are never drawn farther
constexpr size_t kTrackSettingsSpan = 0x600;
constexpr int kOffGrassLod = 0x358, kOffGrassLodFade = 0x35c;

// ---- C. every model has four detail levels, each a separate draw item (bits 50-53 of its flags say which levels it
// holds) and, for most models, a separately streamed resource. The game compares the squared distance to the camera
// with the model's four distances, draws the item of that level, and past the last one nothing.
constexpr uintptr_t kLevelSelectRva = 0x1139200, kLevelSelectSize = 0x98;
// The render loop's calls (shadow casters, and the main view's two paths); there r15 holds the model, rbx the renderer.
constexpr uintptr_t kLevelSelectCalls[3] = {0x1353e2e, 0x13545b3, 0x135464a};
constexpr uint64_t  kLevelBits = 0x3cULL << 48;
constexpr uintptr_t kNoResourceRva = 0x2ebd1cc;       // the id that means "no streamed resource for this level"
constexpr uintptr_t kLevelResourceVtable = 0x2ec20b8;  // a level's streamed resource

// ---- E. placed objects come from template libraries (instancetemplates\*.plib). Each object has 16 lists (8 views, 2
// each) of detail ranges, 0x30 bytes each: start, start minus fade-in, end, end plus fade-out (both filled in at load),
// their squares, the fade-in and the fade-out length. As a library loads, a pass applies the fades and squares the
// ranges; the hook runs at its start (r12: the library, whose name is an MSVC std::string at +0, objects at +0x28..+0x30).
constexpr uintptr_t kPlacedPassRva = 0x1867a9d;       // mov rbx,[r12+0x28] ; lea rsi,[r12+0x40]
constexpr uint8_t   kPlacedPassBytes[10] = {0x49, 0x8B, 0x5C, 0x24, 0x28, 0x49, 0x8D, 0x74, 0x24, 0x40};
constexpr size_t    kPlacedObjectSize = 0x2a8, kPlacedLists = 16, kPlacedListStride = 0x28, kRangeFloats = 12;
constexpr float     kNearDetailMax = 500.0f;          // metres: ranges from 0 ending here or nearer are near detail
constexpr size_t    kRangeType = 11;                  // the float slot holding a range's type
constexpr uint32_t  kRangeType3d = 3;                 // a 3D tree model (0 and 1: plain models and billboards)

uintptr_t      g_base = 0;
DrawDistConfig g_cfg;
std::atomic<bool> g_firstLoad{false};
std::atomic<float> g_lodMeasure{1.0f};       // multiplies the squared distance in the level selection: 1 / scale^2
float*         g_azimuthScale = nullptr;     // Azimuth's Scenery detail factor on the squared distance, when found
float          g_azimuthScale0 = 1.0f;       // its value as Azimuth set it
DWORD          g_tlsModel = TLS_OUT_OF_INDEXES, g_tlsRender = TLS_OUT_OF_INDEXES;   // set by the call stubs
// A track file without a key keeps the value of the last load, which may be ours: then the game's value is the one we replaced.
struct LastWrite {
    float gameValue = 0, wrote = -1.0f;
    float game(float v) { if (v != wrote) gameValue = v; return gameValue; }
};
LastWrite g_grassEnd, g_grassLod[2];             // the grass end, and its detail level end and fade
LastWrite g_worldEnd, g_propsEnd;                // how far the world objects (trees and scenery) and props are drawn

typedef uint64_t (*SerializeFn)(uint8_t*, void*, void*, void*);

template <class T> T& at(uint8_t* p, int off) { return *reinterpret_cast<T*>(p + off); }
void raise(uint8_t* ts, int off, float v) {
    float& f = at<float>(ts, off);
    if (v > f) f = v;
}

// ---------------------------------------------------------------------------
// A
// ---------------------------------------------------------------------------
// Azimuth's Scenery detail jumps from the level selection to its function, which loads its factor on the squared
// distance (movss xmm1, [rip+rel32]) 0x4e bytes in and multiplies by it (mulss xmm1, xmm0) at 0x5b; the function behind
// the entry it moves the other calls to reads the same factor. Null when that is not Azimuth 1.0.4's code.
float* findAzimuthScale() {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + kLevelSelectRva);
    if (!readable(p, 14) || p[0] != 0xFF || p[1] != 0x25 || p[2] || p[3] || p[4] || p[5]) return nullptr;
    uint8_t* fn;
    std::memcpy(&fn, p + 6, sizeof(fn));
    static const uint8_t kLoad[] = {0xF3, 0x0F, 0x10, 0x0D}, kMul[] = {0xF3, 0x0F, 0x59, 0xC8};
    if (!readable(fn + 0x4e, 0x11) || std::memcmp(fn + 0x4e, kLoad, 4) != 0 || std::memcmp(fn + 0x5b, kMul, 4) != 0)
        return nullptr;
    int32_t rel;
    std::memcpy(&rel, fn + 0x52, sizeof(rel));
    float* s = reinterpret_cast<float*>(fn + 0x56 + rel);
    return readable(s, sizeof(float)) && *s > 0.0f && *s <= 1.0f ? s : nullptr;
}

// Sets the scenery measure (1 / scale^2) the hooked level selection uses: the stronger of SceneryDetail and Azimuth's
// Scenery detail, whose calls the hook takes over.
void applyLodMeasure(float m) {
    const float eff = g_azimuthScale ? std::min(g_azimuthScale0, m) : m;
    g_lodMeasure.store(eff, std::memory_order_relaxed);
    LOG("drawdist: scenery level of detail distances x%.1f", 1.0f / std::sqrt(eff));
}

// Multiplies a draw distance by k (up to kFarMax, never below the game's), so detail moved out is not cut off there.
void growEnd(uint8_t* ts, int off, LastWrite& last, float k) {
    float& v = at<float>(ts, off);
    const float game = last.game(v);
    v = k > 1 ? std::max(game, std::min(game * k, kFarMax)) : game;
    last.wrote = v;
}

void applyTrackOverrides(uint8_t* ts) {
    if (!readable(ts, kTrackSettingsSpan)) return;
    if (g_cfg.scenery > 1) applyLodMeasure(1.0f / (g_cfg.scenery * g_cfg.scenery));
    const float world0 = at<float>(ts, kOffWorld), props0 = at<float>(ts, kOffProps);
    growEnd(ts, kOffWorld, g_worldEnd, std::max(g_cfg.scenery, g_cfg.trees));
    growEnd(ts, kOffProps, g_propsEnd, g_cfg.fences);
    const float grass0 = at<float>(ts, kOffGrassEnd);
    const float grassGame = g_grassEnd.game(grass0);
    const float grass = g_cfg.grass > 1 ? std::min(grassGame * g_cfg.grass, kGrassMax) : 0.0f;   // metres; 0 = the game's
    if (grass > 0) {
        raise(ts, kOffGrassEnd, grass);
        raise(ts, kOffGrassStart, grass * 0.75f);
    }
    g_grassEnd.wrote = at<float>(ts, kOffGrassEnd);
    const float k = grassGame > 0 ? at<float>(ts, kOffGrassEnd) / grassGame : 1.0f;
    const int lodOff[2] = {kOffGrassLod, kOffGrassLodFade};
    for (int i = 0; i < 2; ++i) {   // the grass detail level's end and fade scale with the grass end
        float& v = at<float>(ts, lodOff[i]);
        const float game = g_grassLod[i].game(v);
        v = grass > 0 ? std::max(game, std::min(game * k, grass)) : game;
        g_grassLod[i].wrote = v;
    }
    LOG("drawdist: track settings loaded (trees and scenery %.0f->%.0f m, props %.0f->%.0f m, grass %.0f->%.0f m)",
        world0, at<float>(ts, kOffWorld), props0, at<float>(ts, kOffProps), grass0, at<float>(ts, kOffGrassEnd));
}

// ---------------------------------------------------------------------------
// C
// ---------------------------------------------------------------------------
// Where the call at rva goes: into the level selection, whose body Azimuth's Scenery detail rewrites and whose calls
// it moves to a later entry. 0 if it is not such a call.
uintptr_t levelSelectTarget(uintptr_t rva) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + rva);
    if (!readable(p, 5) || p[0] != 0xE8) return 0;
    int32_t rel;
    std::memcpy(&rel, p + 1, sizeof(rel));
    const uintptr_t target = rva + 5 + rel;
    return target >= kLevelSelectRva && target < kLevelSelectRva + kLevelSelectSize ? target : 0;
}

// The game's level selection (0x1139200), the same comparisons in the same order: returns 1 when this item is not drawn.
char gameSelect(float d2, uint64_t flags, int* level, uint8_t noFinest, float t0, float t1, float t2, float t3) {
    if (!(flags & kLevelBits)) return 0;
    int l;
    if (d2 >= t3) { *level = 4; return 1; }
    if (d2 >= t2) l = 3;
    else if (d2 >= t1) l = 2;
    else if (!(d2 >= t0) && !noFinest) l = 0;
    else l = 1;
    *level = l;
    return ((flags >> (50 + l)) & 1) ? 0 : 1;
}

// The pointer at p + off, or 0 when p is 0. Plain reads: these are the pointers the game itself follows for the same
// models (0x14135465e), and a check per read (VirtualQuery) would cost every drawn item several system calls a frame.
uintptr_t follow(uintptr_t p, uint32_t off) { return p ? *reinterpret_cast<const uintptr_t*>(p + off) : 0; }

// Whether the model's level j can be drawn: 1 its streamed resource is loaded and settled (or it has none), 0 not,
// -1 unknown (the model is not laid out as expected).
int levelReady(uintptr_t model, uintptr_t render, int j) {
    const uintptr_t desc = follow(follow(follow(model, 0xb0), 8), 8);
    if (!desc || !render) return -1;
    const uint32_t id = *reinterpret_cast<const uint32_t*>(desc + 0x6c + 4 * j);
    if (id == *reinterpret_cast<const uint32_t*>(g_base + kNoResourceRva)) return 1;
    const uintptr_t arr = follow(follow(render, 0x35d0), 0);
    if (!arr) return -1;
    const uintptr_t obj = *reinterpret_cast<const uint64_t*>(arr + 8 * uint64_t(id));
    if (!obj) return 0;
    typedef bool (*Test)(uintptr_t);
    const uint64_t* vt = *reinterpret_cast<uint64_t* const*>(obj);
    const bool changing = reinterpret_cast<Test>(vt[0x70 / 8])(obj);       // what the game itself asks
    if (reinterpret_cast<uintptr_t>(vt) != g_base + kLevelResourceVtable) { return changing ? 0 : 1; }
    const bool hasData = reinterpret_cast<Test>(vt[0x58 / 8])(obj), loaded = hasData && reinterpret_cast<Test>(vt[0x60 / 8])(obj);
    return loaded && !changing ? 1 : 0;
}


// Distances scaled by SceneryDetail pick a finer level farther away: the finest at or past that pick whose resource is
// ready is drawn as well. The level the game picks itself is never hidden: the game hands an item to these calls only
// near its own distances, so a finer level may never arrive, and some models are far stand-ins for others.
// Drawing both is safe: all items of a model hold the same levels (527 of 527 models at the festival), so a model is
// never drawn twice; only far stand-ins overlap their near models, as the game itself draws them.
char selectLevel(float distSq, const uint64_t* flags, int* level, float, uint8_t noFinest, float t0, float t1, float t2, float t3) {
    const uint64_t f = *flags;
    const float m = g_lodMeasure.load(std::memory_order_relaxed);
    if (!(m < 1.0f) || !(f & kLevelBits)) return gameSelect(distSq, f, level, noFinest, t0, t1, t2, t3);
    const char gameCulled = gameSelect(distSq, f, level, noFinest, t0, t1, t2, t3);
    const int gameLevel = *level;
    gameSelect(distSq * m, f, level, noFinest, t0, t1, t2, t3);
    const int pick = *level;                                        // 4 or more: past the last (scaled) distance
    const uintptr_t model = __readgsqword(0x1480 + 8 * g_tlsModel), render = __readgsqword(0x1480 + 8 * g_tlsRender);
    int target = -1;
    for (int j = pick; j <= std::min(3, gameLevel) && target < 0; ++j) {    // never coarser than the game's own level
        const int r = levelReady(model, render, j);
        if (r < 0) break;
        if (r) target = j;
    }
    if (target >= 0 && ((f >> (50 + target)) & 1)) { *level = target; return 0; }
    *level = gameLevel;                                             // otherwise the game's own choice: never hide what it draws
    return gameCulled;
}

void installLodHook() {
    for (uintptr_t rva : kLevelSelectCalls) {
        if (!levelSelectTarget(rva)) {
            LOG("drawdist: level selection call mismatch; unchanged");
            return;
        }
    }
    // The stubs keep the caller's r15 (model) and rbx (renderer) in two TLS slots, read back with gs:[0x1480 + 8 * index].
    g_tlsModel = TlsAlloc();
    g_tlsRender = TlsAlloc();
    if (g_tlsModel >= 64 || g_tlsRender >= 64) { LOG("drawdist: no TLS slots for the level selection; unchanged"); return; }
    const uint32_t slotModel = 0x1480 + 8 * g_tlsModel, slotRender = 0x1480 + 8 * g_tlsRender;
    uint8_t prefix[18] = {0x65, 0x4C, 0x89, 0x3C, 0x25, 0, 0, 0, 0,     // mov gs:[slotModel], r15
                          0x65, 0x48, 0x89, 0x1C, 0x25, 0, 0, 0, 0};    // mov gs:[slotRender], rbx
    std::memcpy(prefix + 5, &slotModel, 4);
    std::memcpy(prefix + 14, &slotRender, 4);
    constexpr size_t kPerCall = 32;      // 18 bytes of prefix and patchCallRel32's 12
    uint8_t* block = static_cast<uint8_t*>(allocNear(g_base + kLevelSelectCalls[0], kPerCall * 3));
    if (!block) { LOG("drawdist: could not allocate level selection memory; unchanged"); return; }
    int patched = 0, azimuth = 0;
    for (int i = 0; i < 3; ++i) {
        if (levelSelectTarget(kLevelSelectCalls[i]) != kLevelSelectRva) ++azimuth;   // taken from Azimuth's Scenery detail
        if (patchCallRel32(g_base + kLevelSelectCalls[i], reinterpret_cast<void*>(&selectLevel), block + kPerCall * i, false, prefix, sizeof prefix)) ++patched;
    }
    LOG("drawdist: level selection hooked at %d of 3 render calls (%d taken over from Azimuth's Scenery detail); finer levels farther away once loaded, never hiding the game's own", patched, azimuth);
    g_azimuthScale = findAzimuthScale();
    if (g_azimuthScale) {
        g_azimuthScale0 = *g_azimuthScale;
        g_lodMeasure.store(g_azimuthScale0, std::memory_order_relaxed);   // Azimuth's setting alone, until SceneryDetail is applied
        LOG("drawdist: Azimuth's Scenery detail is on (x%.1f); SceneryDetail raises it where it is weaker", 1.0f / std::sqrt(g_azimuthScale0));
    }
}

// Which multiplier reaches a placed-object library: 0 for the libraries the plugin leaves alone.
float libraryScale(const char* name, uint64_t len) {
    struct Lib { const char* name; float DrawDistConfig::*k; };
    static const Lib kLibs[] = {{"TREEANDBUSHTEMPLATE", &DrawDistConfig::trees}, {"BARRIERTEMPLATE", &DrawDistConfig::fences},
                                {"SMASHABLETEMPLATE", &DrawDistConfig::fences}, {"GRASSTEMPLATE", &DrawDistConfig::grass},
                                {"CROPS", &DrawDistConfig::grass}};
    for (const Lib& l : kLibs)
        if (len == std::strlen(l.name) && std::memcmp(name, l.name, len) == 0) return g_cfg.*l.k;
    return 0;
}

float* listBegin(uint8_t* obj, size_t j) { return *reinterpret_cast<float* const*>(obj + kPlacedListStride * j); }
float* listEnd(uint8_t* obj, size_t j) { return *reinterpret_cast<float* const*>(obj + kPlacedListStride * j + 8); }

// Moves the detail switches of a library's objects out by its multiplier k. In each list, an object whose ranges follow
// on one another from 0 and end by kNearDetailMax is near detail: its switches are multiplied by k. Where it ends, a
// coarser object may take over (a range of another object starting there, like a fence's stand-in): that object's start
// moves with it, and its end caps the move; the near object's inner switches move in proportion.
// A 3D tree is different: it is drawn as a model, and only while the game has its mesh loaded (within its streaming
// distance, which nothing here changes). So its billboard keeps the game's start and stays behind the tree: where the
// tree cannot be drawn, the billboard still is, and nothing disappears.
// Ranges that end farther out (billboards, far stand-ins) keep the game's values. Fades keep their length; the pass
// after this one subtracts and adds them.
void scaleLibrary(uint8_t* lib) {
    const uint64_t len = *reinterpret_cast<const uint64_t*>(lib + 0x10), cap = *reinterpret_cast<const uint64_t*>(lib + 0x18);
    if (len > 64) return;
    const char* name = cap < 16 ? reinterpret_cast<const char*>(lib) : *reinterpret_cast<const char* const*>(lib);
    const float k = libraryScale(name, len);
    if (!(k > 1.0f)) return;
    uint8_t* const first = *reinterpret_cast<uint8_t* const*>(lib + 0x28);
    uint8_t* const last = *reinterpret_cast<uint8_t* const*>(lib + 0x30);
    bool ok = first && last >= first && (last - first) % kPlacedObjectSize == 0;
    for (uint8_t* obj = first; ok && obj < last; obj += kPlacedObjectSize)
        for (size_t j = 0; j < kPlacedLists && ok; ++j) {
            const float* b = listBegin(obj, j);
            const float* e = listEnd(obj, j);
            ok = e >= b && (e - b) % kRangeFloats == 0 && (b != nullptr || e == nullptr);
        }
    if (!ok) {
        LOG("drawdist: %.*s layout mismatch; unchanged", (int)len, name);
        return;
    }
    struct Move { float* b; float* c; float end, to; std::vector<float*> takeOver; };
    int moved = 0;
    for (size_t j = 0; j < kPlacedLists; ++j) {
        std::vector<Move> moves;               // found on the game's values first, applied after
        for (uint8_t* obj = first; obj < last; obj += kPlacedObjectSize) {
            float* const b = listBegin(obj, j);
            float* const e = listEnd(obj, j);
            if (b == e || b[0] != 0.0f) continue;
            float* c = b;                     // the last range of the chain from 0
            while (c + kRangeFloats < e && c[kRangeFloats] == c[2]) c += kRangeFloats;
            const float end = c[2];
            if (!(end > 0.0f) || end > kNearDetailMax) continue;
            Move m{b, c, end, end * k, {}};
            const bool tree3d = *reinterpret_cast<const uint32_t*>(b + kRangeType) == kRangeType3d;
            for (uint8_t* o = first; !tree3d && o < last; o += kPlacedObjectSize) {
                float* const ob = listBegin(o, j);
                float* const oe = listEnd(o, j);
                if (ob == oe || ob[0] == 0.0f) continue;      // only objects that start farther out take over
                for (float* r = ob; r < oe; r += kRangeFloats)
                    if (r[0] == end) { m.takeOver.push_back(r); m.to = std::min(m.to, r[2]); }
            }
            if (m.to > end) moves.push_back(m);
        }
        for (Move& m : moves) {
            const float s = m.to / m.end;
            for (float* r = m.b; r <= m.c; r += kRangeFloats) {
                r[0] = r[1] = r[0] == m.end ? m.to : r[0] * s;
                r[2] = r[3] = r[2] == m.end ? m.to : r[2] * s;
            }
            for (float* r : m.takeOver) r[0] = r[1] = m.to;
        }
        moved += static_cast<int>(moves.size());
    }
    LOG("drawdist: %.*s: %d detail switches moved x%.1f", (int)len, name, moved, k);
}

void installPlacementHook() {
    uint8_t* const site = reinterpret_cast<uint8_t*>(g_base + kPlacedPassRva);
    if (!readable(site, sizeof kPlacedPassBytes) || std::memcmp(site, kPlacedPassBytes, sizeof kPlacedPassBytes) != 0) {
        LOG("drawdist: placed objects pass mismatch; unchanged");
        return;
    }
    // The stub keeps the registers the pass may still read (the volatile ones and xmm0-5), calls scaleLibrary(r12),
    // then runs the two instructions its call replaced. rsp is 16-aligned at the call site: after the call and 7 pushes
    // it is again, and stays so through the 0x80 bytes reserved (shadow space and the saved xmm registers).
    static const uint8_t pre[] = {
        0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,          // push rax, rcx, rdx, r8, r9, r10, r11
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,                                // sub rsp, 0x80
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,  // movdqu [rsp+0x20], xmm0 ; [rsp+0x30], xmm1
        0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,  // xmm2 ; xmm3
        0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,  // xmm4 ; xmm5
        0x4C, 0x89, 0xE1,                                                        // mov rcx, r12
        0x48, 0xB8};                                                             // mov rax, imm64 (scaleLibrary)
    static const uint8_t post[] = {
        0xFF, 0xD0,                                                              // call rax
        0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,  // movdqu xmm0, [rsp+0x20] ; xmm1
        0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,  // xmm2 ; xmm3
        0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,  // xmm4 ; xmm5
        0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,                                // add rsp, 0x80
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58,          // pop r11, r10, r9, r8, rdx, rcx, rax
        0x49, 0x8B, 0x5C, 0x24, 0x28, 0x49, 0x8D, 0x74, 0x24, 0x40,              // mov rbx, [r12+0x28] ; lea rsi, [r12+0x40]
        0xC3};                                                                   // ret
    uint8_t* const stub = static_cast<uint8_t*>(allocNear(g_base + kPlacedPassRva, sizeof pre + 8 + sizeof post));
    if (!stub) { LOG("drawdist: could not allocate placed objects memory; unchanged"); return; }
    const uint64_t fn = reinterpret_cast<uint64_t>(&scaleLibrary);
    std::memcpy(stub, pre, sizeof pre);
    std::memcpy(stub + sizeof pre, &fn, 8);
    std::memcpy(stub + sizeof pre + 8, post, sizeof post);
    FlushInstructionCache(GetCurrentProcess(), stub, sizeof pre + 8 + sizeof post);
    uint8_t call[10] = {0xE8, 0, 0, 0, 0, 0x0F, 0x1F, 0x44, 0x00, 0x00};     // call stub ; 5-byte nop
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(stub) - static_cast<intptr_t>(g_base + kPlacedPassRva + 5));
    std::memcpy(call + 1, &rel, 4);
    DWORD prot;
    if (!VirtualProtect(site, sizeof call, PAGE_EXECUTE_READWRITE, &prot)) {
        LOG("drawdist: placed objects pass VirtualProtect failed %lu; unchanged", GetLastError());
        return;
    }
    Suspended sus;
    sus.suspendOthers();
    std::memcpy(site, call, sizeof call);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof call);
    sus.resumeAll();
    VirtualProtect(site, sizeof call, prot, &prot);
    LOG("drawdist: placed trees x%.1f, fences x%.1f, grass x%.1f, applied as their libraries load", g_cfg.trees, g_cfg.fences, g_cfg.grass);
}

void logAzimuthScenery() {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + kLevelSelectRva);
    if (g_cfg.scenery > 1 && !g_azimuthScale && p[0] == 0xFF && p[1] == 0x25)
        LOG("drawdist: Azimuth's Scenery detail is on but its code is not the known one; only SceneryDetail sets the scenery detail distances");
}

// ---------------------------------------------------------------------------
// the hook on the track settings deserializer
// ---------------------------------------------------------------------------
extern "C" uint64_t hookTrackSettings(uint8_t* ts, void* ar, void* r8, void* r9) {
    uint64_t r = reinterpret_cast<SerializeFn>(g_base + kSerializeRva)(ts, ar, r8, r9);
    if (!g_firstLoad.exchange(true)) logAzimuthScenery();
    applyTrackOverrides(ts);
    return r;
}

bool installTrackHook() {
    const uint8_t* site = reinterpret_cast<const uint8_t*>(g_base + kThunkRva);
    if (!readable(site, sizeof(kThunkBytes)) || std::memcmp(site, kThunkBytes, sizeof(kThunkBytes)) != 0) {
        LOG("drawdist: track settings thunk mismatch; unchanged");
        return false;
    }
    void* mem = allocNear(g_base + kThunkJmpRva, 64);
    if (!mem) { LOG("drawdist: could not allocate thunk memory; unchanged"); return false; }
    // patchCallRel32 only rewrites the rel32 behind the opcode byte, so it serves the E9 jump as well.
    if (!patchCallRel32(g_base + kThunkJmpRva, reinterpret_cast<void*>(&hookTrackSettings), mem)) return false;
    LOG("drawdist: track settings hook installed");
    return true;
}

} // namespace

void DrawDist_Install(uintptr_t base, const Config& cfg) {
    g_base = base;
    g_cfg = cfg.drawDist;
    const DrawDistConfig& d = g_cfg;
    if (d.scenery > 1 || d.trees > 1 || d.fences > 1 || d.grass > 1) installTrackHook();
    // Also with Azimuth's Scenery detail alone: its own fallback draws the coarse level over the fine one.
    if (d.scenery > 1 || findAzimuthScale()) installLodHook();
    if (d.trees > 1 || d.fences > 1 || d.grass > 1) installPlacementHook();   // Azimuth's Scenery detail does not reach them
}

} // namespace fh2

// Test hooks for tests/harness.cpp: the original deserializer needs a real archive, so the overrides
// are exercised directly.
extern "C" __declspec(dllexport) void FH2SI_TestApplyTrackOverrides(uint8_t* ts) { fh2::applyTrackOverrides(ts); }

// Test hook: finds Azimuth's factor (the harness fakes Azimuth's jump), applies the measure m and reports Azimuth's factor
// (left as it was) and the hook's measure afterwards; then puts the plugin's state back as it was.
extern "C" __declspec(dllexport) bool FH2SI_TestAzimuthLod(float m, float* azimuthAfter, float* wrapMeasure) {
    float* const keep = fh2::g_azimuthScale;
    const float keep0 = fh2::g_azimuthScale0, keepMeasure = fh2::g_lodMeasure.load();
    fh2::g_azimuthScale = fh2::findAzimuthScale();
    const bool found = fh2::g_azimuthScale != nullptr;
    if (found) fh2::g_azimuthScale0 = *fh2::g_azimuthScale;
    fh2::applyLodMeasure(m);
    *azimuthAfter = found ? *fh2::g_azimuthScale : 0.0f;
    *wrapMeasure = fh2::g_lodMeasure.load();
    fh2::g_azimuthScale = keep;
    fh2::g_azimuthScale0 = keep0;
    fh2::g_lodMeasure.store(keepMeasure);
    return found;
}

// Test hook: applies the track settings to ts with this GrassDetail (the others as configured), then restores it.
extern "C" __declspec(dllexport) void FH2SI_TestTrackLines(uint8_t* ts, float grass) {
    const fh2::DrawDistConfig keep = fh2::g_cfg;
    fh2::g_cfg.grass = grass;
    fh2::applyTrackOverrides(ts);
    fh2::g_cfg = keep;
}

// Test hook: runs the hooked level selection with this model and renderer (what the call stubs pass in TLS) and the
// measure m, then puts the plugin's state back as it was.
extern "C" __declspec(dllexport) char FH2SI_TestSelect(void* model, void* render, float m, float distSq, uint64_t flags,
                                                     int* level, uint8_t noFinest, float t0, float t1, float t2, float t3) {
    if (fh2::g_tlsModel >= 64 || fh2::g_tlsRender >= 64) return -1;
    const float keep = fh2::g_lodMeasure.load();
    fh2::g_lodMeasure.store(m);
    TlsSetValue(fh2::g_tlsModel, model);
    TlsSetValue(fh2::g_tlsRender, render);
    const char culled = fh2::selectLevel(distSq, &flags, level, 0.0f, noFinest, t0, t1, t2, t3);
    fh2::g_lodMeasure.store(keep);
    return culled;
}

// Test hook: moves a library's detail switches with these multipliers, then restores them.
extern "C" __declspec(dllexport) void FH2SI_TestScaleLibrary(uint8_t* lib, float trees, float fences, float grass) {
    const fh2::DrawDistConfig keep = fh2::g_cfg;
    fh2::g_cfg.trees = trees;
    fh2::g_cfg.fences = fences;
    fh2::g_cfg.grass = grass;
    fh2::scaleLibrary(lib);
    fh2::g_cfg = keep;
}
