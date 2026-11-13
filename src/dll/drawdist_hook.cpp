// SPDX-License-Identifier: MIT
// Pop-in killer (no game files are edited):
//  A. track settings: how far trees, scenery and props are drawn, the grass end and its detail level, all grown with their
//     multipliers, and the scenery multiplier applied per track;
//  C. scenery level of detail (buildings, props and everything else drawn per model): the render loop's calls of the level selection
//     also draw a finer level farther away when its resource is ready; in the main view and as a shadow caster the
//     game's own coarser level is hidden, but only while the finer one is seen to be drawn there;
//  E. placed trees, fences and grass: their detail switches move out as their template library loads;
//  F. grass patches the game has loaded but does not list for its current zone are drawn as well;
//  G. the shadow layers keep their size instead of being resized metre by metre as the camera moves.
#include "drawdist_hook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <iterator>
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
// The render loop's calls (shadow casters, and the two paths of the other views); there r15 holds the model, rbx the
// renderer and rsi the view type (6 at the first call).
constexpr uintptr_t kLevelSelectCalls[3] = {0x1353e2e, 0x13545b3, 0x135464a};
constexpr uint64_t  kLevelBits = 0x3cULL << 48;
constexpr uintptr_t kNoResourceRva = 0x2ebd1cc;       // the id that means "no streamed resource for this level"
constexpr uintptr_t kLevelResourceVtable = 0x2ec20b8;  // a level's streamed resource
// The models of a frame are one list (the renderer points at its begin and end, 0x100 bytes per model). A placed model
// has an object in it for each of its levels, and only while the game wants that level near its own distance.
constexpr int       kOffModelList = 0x35a8;
constexpr uint32_t  kMainView = 18, kCasterView = 6;    // the view types of the main scene and of the shadow casters
constexpr size_t    kFamilies = 1 << 14, kFamilyProbes = 8;
// Family::at: the index in the list, whether the object drew that level at its last main view call, whether it cast
// it at its last shadow caster call, and whether two objects hold the level (then it is drawn in parts and one of
// them says nothing about the others).
constexpr uint32_t  kDrew = 0x80000000u, kShared = 0x40000000u, kCast = 0x20000000u, kIndex = kCast - 1;
// The renderer's values for the shadow casters: no caster past the reach (0x141353d8a, by the distance to the object's
// own box), and none that is too small seen from the eye (0x141353eb2: eye x y z, then at +0x10 a scale, at +0x14 a
// switch that is negative when the test is off, at +0x18 the size).
constexpr int       kOffCasterEye = 0x1880, kOffCasterReach = 0x29e8;

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

// ---- F. grass patches are streamed scene items. Each frame the game draws the items on its current zone's baked list,
// which holds the grass patches within the game's own grass distance only (about 200 m). Patches farther away are loaded
// (the neighbouring zones need them) but not listed, so grass shows up in whole blocks as the zone changes. The hook
// runs where a frame's view takes that list (rdx: the zone state, rsi: the view) and adds the loaded grass patches.
constexpr uintptr_t kViewListRva = 0x1336445;         // movsxd rax,[rdx+0x1b8] ; lea rcx,[rax*2+0x31]
constexpr uint8_t   kViewListBytes[15] = {0x48, 0x63, 0x82, 0xB8, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x0C, 0x45, 0x31, 0x00, 0x00, 0x00};
constexpr int       kOffZoneLists = 0x188, kOffZoneListIndex = 0x1b8;   // the two item lists, and which one is current
constexpr int       kOffViewItems = 0x35b0;           // the view's pointer to the world's streamed items
constexpr uintptr_t kStreamedItemVtable = 0x2f5a830, kGrassPatchVtable = 0x2f5a470;
constexpr int       kOffHandleResource = 0x28, kOffResourceState = 0x34, kOffResourceItem = 0x118;
constexpr uint8_t   kResourceLoaded = 4;              // both state bytes hold it once the item can be used
constexpr size_t    kItemsPerFrame = 4096;            // streamed items looked at per frame for newly loaded grass

// ---- G. shadows are drawn in three layers by distance. Each frame the game fits a sphere around a layer's part of the
// view and rounds its radius up to a whole metre (0x141155953..0x14115598d); the layer's shadow map is stretched over
// that sphere. The radius follows the camera (the view widens with speed), so the map is resized metre by metre and
// every shadow in it jumps. With Azimuth's Shadow distance the middle layer reaches 130 m instead of 15 and that shows
// as a slow flicker on the trees. The hook runs right after the rounding (rsi: the layer's view data).
constexpr uintptr_t kShadowSphereRva = 0x1155995;     // vmovaps xmm4,xmm12 ; mov ecx,8
constexpr uint8_t   kShadowSphereBytes[10] = {0xC4, 0xC1, 0x78, 0x28, 0xE4, 0xB9, 0x08, 0x00, 0x00, 0x00};
constexpr int       kOffSphereRadius = 0x1bc;
constexpr float     kRadiusRoom = 1.03f;              // a new radius is this much larger than asked, so the next change is far off
constexpr float     kRadiusShrink = 0.88f;            // the radius in use is given up when the game asks for less than this part of it

uintptr_t      g_base = 0;
DrawDistConfig g_cfg;
std::atomic<bool> g_firstLoad{false};
std::atomic<float> g_lodMeasure{1.0f};       // multiplies the squared distance in the level selection: 1 / scale^2
float*         g_azimuthScale = nullptr;     // Azimuth's Scenery detail factor on the squared distance, when found
float          g_azimuthScale0 = 1.0f;       // its value as Azimuth set it
DWORD          g_tlsModel = TLS_OUT_OF_INDEXES, g_tlsRender = TLS_OUT_OF_INDEXES, g_tlsView = TLS_OUT_OF_INDEXES;   // set by the call stubs
// The level objects of one placed model (same placement, level resources and other flags): where the object holding
// each level was in the frame's list at its last main view call. Written by the render threads without a lock: a
// wrong value only costs a frame, because nothing is hidden before the object it points at is checked in the list.
struct Family { std::atomic<uint64_t> key; std::atomic<uint32_t> at[4]; };
Family         g_families[kFamilies];
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
int levelReady(uintptr_t desc, uintptr_t render, int j) {
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

uint64_t mix(uint64_t h, uint64_t v) {
    h = (h ^ v) * 0x9E3779B97F4A7C15ULL;
    return h ^ (h >> 32);
}

// The family of this model's level objects; a new one takes a free slot. Null when there is no room.
Family* family(uintptr_t model, uintptr_t desc, uint64_t flags) {
    uint64_t place, ids[2];
    uint32_t placeZ;
    std::memcpy(&place, reinterpret_cast<const void*>(model + 0x24), 8);
    std::memcpy(&placeZ, reinterpret_cast<const void*>(model + 0x2c), 4);
    std::memcpy(ids, reinterpret_cast<const void*>(desc + 0x6c), 16);
    const uint64_t key = mix(mix(mix(mix(mix(0, place), placeZ), ids[0]), ids[1]), flags & ~kLevelBits) | 1;
    size_t i = (key >> 40) & (kFamilies - 1);
    for (size_t n = 0; n < kFamilyProbes; ++n, i = (i + 1) & (kFamilies - 1)) {
        uint64_t k = g_families[i].key.load(std::memory_order_relaxed);
        if (k == key) return &g_families[i];
        if (k) continue;
        if (g_families[i].key.compare_exchange_strong(k, key) || k == key) return &g_families[i];
    }
    // shortcut: no free slot near this key empties the whole table, and for a frame nothing is hidden; give the slots
    // an age if that ever shows.
    for (Family& fam : g_families) {
        for (auto& a : fam.at) a.store(0, std::memory_order_relaxed);
        fam.key.store(0, std::memory_order_relaxed);
    }
    return nullptr;
}

// Whether the list's entry at index is another object of this model's family that holds the level and gets through
// the render loop's checks as this one did: same other flags and the bits of +0xc4 it tests for the main view (0x40
// is for the mirror and differs within a family), and its own block (+0xb0) neither gone, retired (+0xc1) nor
// switched off (bit 39 at +0x10).
bool sibling(uintptr_t model, uintptr_t desc, uint64_t f, uint32_t index, int level, uintptr_t begin, uintptr_t end) {
    const uintptr_t s = begin + (uintptr_t(index) << 8);
    if (s >= end || s == model) return false;
    const uint8_t* a = reinterpret_cast<const uint8_t*>(s);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(model);
    const uint64_t sf = *reinterpret_cast<const uint64_t*>(s + 0xb8);
    if (((sf ^ f) & ~kLevelBits) || !((sf >> (50 + level)) & 1)) return false;
    if (std::memcmp(a + 0x24, b + 0x24, 12) != 0 || std::memcmp(a + 0xf0, b + 0xf0, 8) != 0 || ((a[0xc4] ^ b[0xc4]) & 0x1d)) return false;
    const uintptr_t block = follow(s, 0xb0);
    if (!block || *reinterpret_cast<const uint8_t*>(block + 0xc1) || ((*reinterpret_cast<const uint64_t*>(block + 0x10) >> 39) & 1)) return false;
    const uintptr_t sdesc = follow(follow(block, 8), 8);
    return sdesc && std::memcmp(reinterpret_cast<const void*>(sdesc + 0x6c), reinterpret_cast<const void*>(desc + 0x6c), 16) == 0;
}

// Distances scaled by SceneryDetail pick a finer level farther away: the finest at or past that pick whose resource is
// ready is drawn. Each level of a placed model is its own object, and the game lists an object only near its own
// distances, so the object of a ready level may not be there: the object of the game's own level is drawn too.
// Whether the finer object s casts a shadow when the game's own, at distSq, got as far as the level selection. The
// game drops a caster past the reach before it selects a level, by the distance to the object's own box, and the boxes
// of a family differ by up to a metre or two: near the reach both cast. After the selection it drops objects that are
// too small; this is that test, with the game's own arithmetic (0x141353ebc).
bool castsToo(uintptr_t render, uintptr_t s, float distSq) {
    const float reach = *reinterpret_cast<const float*>(render + kOffCasterReach);
    if (!(distSq < 0.9f * reach * reach)) return false;
    const float* eye = reinterpret_cast<const float*>(render + kOffCasterEye);
    if (0.0f > eye[5]) return true;
    const float* ball = reinterpret_cast<const float*>(s + 0xc8);          // radius term, centre x y z
    const float dx = ball[1] - eye[0], dy = ball[2] - eye[1], dz = ball[3] - eye[2];
    return !((dx * dx + dy * dy + dz * dz) * eye[4] > eye[6] * ball[0]);
}

// Two levels on the same spot flicker, and the coarser one's shadow falls on the finer one, so in the main view and as
// a shadow caster the game's own is hidden while the finer one is in the frame's list and drew (or cast) at its last
// call there. The other views (reflections) keep both.
char selectLevel(float distSq, const uint64_t* flags, int* level, float, uint8_t noFinest, float t0, float t1, float t2, float t3) {
    const uint64_t f = *flags;
    const float m = g_lodMeasure.load(std::memory_order_relaxed);
    if (!(m < 1.0f) || !(f & kLevelBits)) return gameSelect(distSq, f, level, noFinest, t0, t1, t2, t3);
    const char gameCulled = gameSelect(distSq, f, level, noFinest, t0, t1, t2, t3);
    const int gameLevel = *level;
    gameSelect(distSq * m, f, level, noFinest, t0, t1, t2, t3);
    const int pick = *level;                                        // 4 or more: past the last (scaled) distance
    const uintptr_t model = __readgsqword(0x1480 + 8 * g_tlsModel), render = __readgsqword(0x1480 + 8 * g_tlsRender);
    const uintptr_t desc = follow(follow(follow(model, 0xb0), 8), 8);
    int target = -1;
    for (int j = pick; j <= std::min(3, gameLevel) && target < 0; ++j) {    // never coarser than the game's own level
        const int r = levelReady(desc, render, j);
        if (r < 0) break;
        if (r) target = j;
    }
    const bool finer = target >= 0 && ((f >> (50 + target)) & 1);  // this item holds the level to draw
    char culled = finer ? 0 : gameCulled;                           // otherwise the game's own choice
    *level = finer ? target : gameLevel;
    const uint32_t view = uint32_t(__readgsqword(0x1480 + 8 * g_tlsView));
    if (!desc || !render || (view != kMainView && view != kCasterView)) return culled;
    const uint32_t did = view == kMainView ? kDrew : kCast;         // each of the two views has its own record
    const uintptr_t list = follow(render, kOffModelList), begin = follow(list, 0), end = follow(list, 8);
    if (model < begin || model >= end || ((model - begin) & 0xff) || ((model - begin) >> 8) > kIndex) return culled;
    const uint32_t me = uint32_t((model - begin) >> 8);
    Family* fam = family(model, desc, f);
    if (!fam) return culled;
    if (!culled && !finer && target >= 0) {                         // the game's level, with a finer one ready: who draws it?
        const uint32_t a = fam->at[target].load(std::memory_order_relaxed);
        if ((a & (did | kShared)) == did && sibling(model, desc, f, a & kIndex, target, begin, end)
            && (view == kMainView || castsToo(render, begin + (uintptr_t(a & kIndex) << 8), distSq))) culled = 1;
    }
    for (int b = 0; b < 4; ++b) {                                   // what this object did, for the others of its family
        if (!((f >> (50 + b)) & 1)) continue;
        const uint32_t old = fam->at[b].load(std::memory_order_relaxed);
        uint32_t v = me | (old & ~(kIndex | did)) | (!culled && *level == b ? did : 0);
        if (!(old & kShared) && (old & kIndex) != me && sibling(model, desc, f, old & kIndex, b, begin, end)) v |= kShared;
        if (v != old) fam->at[b].store(v, std::memory_order_relaxed);
    }
    return culled;
}

void installLodHook() {
    for (uintptr_t rva : kLevelSelectCalls) {
        if (!levelSelectTarget(rva)) {
            LOG("drawdist: level selection call mismatch; unchanged");
            return;
        }
    }
    // The stubs keep the caller's r15 (model), rbx (renderer) and rsi (view type) in three TLS slots, read back with
    // gs:[0x1480 + 8 * index].
    g_tlsModel = TlsAlloc();
    g_tlsRender = TlsAlloc();
    g_tlsView = TlsAlloc();
    if (g_tlsModel >= 64 || g_tlsRender >= 64 || g_tlsView >= 64) { LOG("drawdist: no TLS slots for the level selection; unchanged"); return; }
    const uint32_t slotModel = 0x1480 + 8 * g_tlsModel, slotRender = 0x1480 + 8 * g_tlsRender, slotView = 0x1480 + 8 * g_tlsView;
    uint8_t prefix[27] = {0x65, 0x4C, 0x89, 0x3C, 0x25, 0, 0, 0, 0,     // mov gs:[slotModel], r15
                          0x65, 0x48, 0x89, 0x1C, 0x25, 0, 0, 0, 0,     // mov gs:[slotRender], rbx
                          0x65, 0x48, 0x89, 0x34, 0x25, 0, 0, 0, 0};    // mov gs:[slotView], rsi
    std::memcpy(prefix + 5, &slotModel, 4);
    std::memcpy(prefix + 14, &slotRender, 4);
    std::memcpy(prefix + 23, &slotView, 4);
    constexpr size_t kPerCall = 40;      // 27 bytes of prefix and patchCallRel32's 12
    uint8_t* block = static_cast<uint8_t*>(allocNear(g_base + kLevelSelectCalls[0], kPerCall * 3));
    if (!block) { LOG("drawdist: could not allocate level selection memory; unchanged"); return; }
    int patched = 0, azimuth = 0;
    for (int i = 0; i < 3; ++i) {
        if (levelSelectTarget(kLevelSelectCalls[i]) != kLevelSelectRva) ++azimuth;   // taken from Azimuth's Scenery detail
        if (patchCallRel32(g_base + kLevelSelectCalls[i], reinterpret_cast<void*>(&selectLevel), block + kPerCall * i, false, prefix, sizeof prefix)) ++patched;
    }
    LOG("drawdist: level selection hooked at %d of 3 render calls (%d taken over from Azimuth's Scenery detail); finer levels farther away once loaded; in the main view and as a shadow caster the game's coarser one hides while the finer one is drawn", patched, azimuth);
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

// ---------------------------------------------------------------------------
// G
// ---------------------------------------------------------------------------
// The radius to use when the game asks for `want` and `held` is in use: the same while the asked one fits in it and is
// not much smaller (a smaller sphere would only sharpen the shadows a little); otherwise the asked one with some room.
float steadyRadius(float held, float want) {
    if (want <= held && want >= held * kRadiusShrink) return held;
    return std::ceil(want * kRadiusRoom);
}

struct HeldRadius { std::atomic<uintptr_t> layer; std::atomic<float> radius; };
HeldRadius g_heldRadius[4];                  // the layers, by the address of their view data
std::atomic<unsigned> g_heldNext{0};

void steadyShadowSphere(uint8_t* layer) {
    float& radius = at<float>(layer, kOffSphereRadius);
    if (!(radius > 0.0f)) return;
    const uintptr_t me = reinterpret_cast<uintptr_t>(layer);
    HeldRadius* mine = nullptr;
    for (HeldRadius& h : g_heldRadius) {
        uintptr_t owner = h.layer.load(std::memory_order_relaxed);
        if (!owner) h.layer.compare_exchange_strong(owner, me);      // when another thread won, owner is that layer
        if (!owner || owner == me) { mine = &h; break; }
    }
    if (!mine) {                             // other layers hold every slot (a new session's): take the oldest
        mine = &g_heldRadius[g_heldNext.fetch_add(1, std::memory_order_relaxed) % std::size(g_heldRadius)];
        mine->radius.store(0.0f, std::memory_order_relaxed);
        mine->layer.store(me, std::memory_order_relaxed);
    }
    radius = steadyRadius(mine->radius.load(std::memory_order_relaxed), radius);
    mine->radius.store(radius, std::memory_order_relaxed);
}

// Replaces the n bytes at rva (whole instructions that use neither rip nor rsp; n is 5 plus a multiple of 5) by a call
// to a stub. The stub keeps the registers the game's code may still read (the volatile ones and xmm0-5), loads fn's
// arguments with `args`, calls fn, then runs the replaced instructions. rsp must be 16-aligned at the site: after the
// call and 7 pushes it is again, and stays so through the 0x80 bytes reserved (shadow space and the saved xmm registers).
bool installStubCall(uintptr_t rva, const uint8_t* orig, size_t n, const uint8_t* args, size_t argLen, void* fn, const char* what) {
    uint8_t* const site = reinterpret_cast<uint8_t*>(g_base + rva);
    if (!readable(site, n) || std::memcmp(site, orig, n) != 0) {
        LOG("drawdist: %s mismatch; unchanged", what);
        return false;
    }
    static const uint8_t save[] = {
        0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,          // push rax, rcx, rdx, r8, r9, r10, r11
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,                                // sub rsp, 0x80
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,  // movdqu [rsp+0x20], xmm0 ; [rsp+0x30], xmm1
        0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,  // xmm2 ; xmm3
        0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70}; // xmm4 ; xmm5
    static const uint8_t restore[] = {
        0xFF, 0xD0,                                                              // call rax
        0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,  // movdqu xmm0, [rsp+0x20] ; xmm1
        0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,  // xmm2 ; xmm3
        0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,  // xmm4 ; xmm5
        0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,                                // add rsp, 0x80
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58};         // pop r11, r10, r9, r8, rdx, rcx, rax
    const uint64_t target = reinterpret_cast<uint64_t>(fn);
    const uint8_t* const targetBytes = reinterpret_cast<const uint8_t*>(&target);
    std::vector<uint8_t> code(std::begin(save), std::end(save));
    code.insert(code.end(), args, args + argLen);
    code.insert(code.end(), {0x48, 0xB8});                                       // mov rax, imm64 (fn)
    code.insert(code.end(), targetBytes, targetBytes + 8);
    code.insert(code.end(), std::begin(restore), std::end(restore));
    code.insert(code.end(), orig, orig + n);
    code.push_back(0xC3);                                                        // ret
    uint8_t* const stub = static_cast<uint8_t*>(allocNear(g_base + rva, code.size()));
    if (!stub) { LOG("drawdist: could not allocate memory for %s; unchanged", what); return false; }
    std::memcpy(stub, code.data(), code.size());
    FlushInstructionCache(GetCurrentProcess(), stub, code.size());
    std::vector<uint8_t> call(n);
    call[0] = 0xE8;                                                              // call stub ; 5-byte nops
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(stub) - static_cast<intptr_t>(g_base + rva + 5));
    std::memcpy(&call[1], &rel, 4);
    static const uint8_t nop5[5] = {0x0F, 0x1F, 0x44, 0x00, 0x00};
    for (size_t i = 5; i + 5 <= n; i += 5) std::memcpy(&call[i], nop5, 5);
    DWORD prot;
    if (!VirtualProtect(site, n, PAGE_EXECUTE_READWRITE, &prot)) {
        LOG("drawdist: %s VirtualProtect failed %lu; unchanged", what, GetLastError());
        return false;
    }
    Suspended sus;
    sus.suspendOthers();
    std::memcpy(site, call.data(), n);
    FlushInstructionCache(GetCurrentProcess(), site, n);
    sus.resumeAll();
    VirtualProtect(site, n, prot, &prot);
    return true;
}

void installPlacementHook() {
    static const uint8_t args[] = {0x4C, 0x89, 0xE1};                            // mov rcx, r12
    if (installStubCall(kPlacedPassRva, kPlacedPassBytes, sizeof kPlacedPassBytes, args, sizeof args, reinterpret_cast<void*>(&scaleLibrary), "placed objects pass"))
        LOG("drawdist: placed trees x%.1f, fences x%.1f, grass x%.1f, applied as their libraries load", g_cfg.trees, g_cfg.fences, g_cfg.grass);
}

// ---------------------------------------------------------------------------
// F
// ---------------------------------------------------------------------------
struct ItemList { uint32_t* begin; uint32_t* end; uint32_t* capacity; };   // an MSVC std::vector<uint32_t> of item numbers
struct ItemEntry { uint8_t* handle; void* count; };                        // a shared pointer; null while the item is not loaded
struct ItemArray { ItemEntry* begin; ItemEntry* end; };

// Only the thread that prepares the frame runs this hook, so its state needs no lock.
ItemEntry* g_items = nullptr;            // the world's streamed items the state below belongs to
size_t g_itemCount = 0, g_nextItem = 0;  // how many there are, and where the search for new grass goes on
std::vector<uint32_t> g_grassItems;      // the loaded grass patches found so far

// True when this streamed item is a loaded grass patch. The pointers are the game's own, read as the game reads them.
bool loadedGrass(const ItemEntry& e) {
    if (!e.handle) return false;
    uint8_t* const res = at<uint8_t*>(e.handle, kOffHandleResource);
    if (!res || at<uintptr_t>(res, 0) != g_base + kStreamedItemVtable) return false;
    if (at<uint8_t>(res, kOffResourceState) != kResourceLoaded || at<uint8_t>(res, kOffResourceState + 1) != kResourceLoaded) return false;
    uint8_t* const item = at<uint8_t*>(res, kOffResourceItem);
    return item && at<uintptr_t>(item, 0) == g_base + kGrassPatchVtable;
}

// Adds the loaded grass patches to the view's current item list. The game reserves that list for every item of the
// world, so adding never reallocates; the game's range checks then draw each patch as far as GrassDetail says.
extern "C" void showLoadedGrass(uint8_t* zone, uint8_t* view) {
    if (!readable(zone + kOffZoneLists, kOffZoneListIndex + 4 - kOffZoneLists) || !readable(view + kOffViewItems, 8)) return;
    const uint32_t which = at<uint32_t>(zone, kOffZoneListIndex);
    ItemArray* const items = at<ItemArray*>(view, kOffViewItems);
    if (which > 1 || !readable(items, sizeof *items) || !items->begin || items->end <= items->begin) return;
    const size_t count = static_cast<size_t>(items->end - items->begin);
    ItemList& list = *reinterpret_cast<ItemList*>(zone + kOffZoneLists + sizeof(ItemList) * which);
    if (count > (1u << 24) || !readable(items->begin, count * sizeof(ItemEntry))) return;
    if (!list.begin || list.end < list.begin || list.capacity < list.end ||
        !readable(list.begin, static_cast<size_t>(list.capacity - list.begin) * sizeof(uint32_t))) return;
    if (items->begin != g_items || count != g_itemCount) {       // another world: start over
        g_items = items->begin;
        g_itemCount = count;
        g_nextItem = 0;
        g_grassItems.clear();
    }
    const auto show = [&list](uint32_t i) {
        if (list.end == list.capacity || std::find(list.begin, list.end, i) != list.end) return;
        *list.end = i;
        __atomic_store_n(&list.end, list.end + 1, __ATOMIC_RELEASE);   // the item first, then the new end
    };
    // The patches found before come first, so a new zone's list is complete in its first frame.
    g_grassItems.erase(std::remove_if(g_grassItems.begin(), g_grassItems.end(),
                                      [](uint32_t i) { return !loadedGrass(g_items[i]); }), g_grassItems.end());
    for (uint32_t i : g_grassItems) show(i);
    for (size_t n = std::min(kItemsPerFrame, count); n > 0; --n) {
        const uint32_t i = static_cast<uint32_t>(g_nextItem);
        g_nextItem = (g_nextItem + 1) % count;
        if (loadedGrass(g_items[i]) && std::find(g_grassItems.begin(), g_grassItems.end(), i) == g_grassItems.end()) {
            g_grassItems.push_back(i);
            show(i);
        }
    }
}

void installShadowHook() {
    static const uint8_t args[] = {0x48, 0x89, 0xF1};                            // mov rcx, rsi
    if (installStubCall(kShadowSphereRva, kShadowSphereBytes, sizeof kShadowSphereBytes, args, sizeof args, reinterpret_cast<void*>(&steadyShadowSphere), "shadow layer size"))
        LOG("drawdist: the shadow layers keep their size (the game resizes them metre by metre as the camera moves)");
}

void installGrassHook() {
    static const uint8_t args[] = {0x48, 0x89, 0xD1, 0x48, 0x89, 0xF2};          // mov rcx, rdx ; mov rdx, rsi
    if (installStubCall(kViewListRva, kViewListBytes, sizeof kViewListBytes, args, sizeof args, reinterpret_cast<void*>(&showLoadedGrass), "view item list"))
        LOG("drawdist: loaded grass patches are added to each frame's item list");
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
    if (d.grass > 1) installGrassHook();
    if (d.steadyShadows) installShadowHook();
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

// Test hook: runs the hooked level selection with this model and renderer and the view type set with
// FH2SI_TestSetView (what the call stubs pass in TLS) and the measure m, then puts the plugin's state back as it was.
static uint32_t g_testView = 0;
extern "C" __declspec(dllexport) void FH2SI_TestSetView(uint32_t view) { g_testView = view; }
extern "C" __declspec(dllexport) char FH2SI_TestSelect(void* model, void* render, float m, float distSq, uint64_t flags,
                                                     int* level, uint8_t noFinest, float t0, float t1, float t2, float t3) {
    if (fh2::g_tlsModel >= 64 || fh2::g_tlsRender >= 64 || fh2::g_tlsView >= 64) return -1;
    const float keep = fh2::g_lodMeasure.load();
    fh2::g_lodMeasure.store(m);
    TlsSetValue(fh2::g_tlsModel, model);
    TlsSetValue(fh2::g_tlsRender, render);
    TlsSetValue(fh2::g_tlsView, reinterpret_cast<void*>(uintptr_t(g_testView)));
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

// Test hook: steadies the radius of a shadow layer's sphere, as the hook after the game's rounding does.
extern "C" __declspec(dllexport) void FH2SI_TestSteadyShadow(uint8_t* layer) { fh2::steadyShadowSphere(layer); }

// Test hook: adds the loaded grass patches to the zone state's current item list, as the frame hook does.
extern "C" __declspec(dllexport) void FH2SI_TestShowGrass(uint8_t* zone, uint8_t* view) { fh2::showLoadedGrass(zone, view); }
