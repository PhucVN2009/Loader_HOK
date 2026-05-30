#pragma once
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <time.h>
#include <vector>
#include <utility>
#include <android/log.h>

bool maphack = false;

// Diagnostic toggle: when true, sync_oos_transform records candidate position
// sources for OOS actors. Surfaced live in the in-game menu's "Debug" tab, and
// also mirrored to logcat (tag "HOKMAP", capture with: adb logcat -s HOKMAP:D).
// Used to determine which field actually advances while an actor is out of sight.
static bool g_mapDebug = true;
#define HOKMAP_LOG(...) do { if (g_mapDebug) __android_log_print(ANDROID_LOG_DEBUG, "HOKMAP", __VA_ARGS__); } while (0)

// ── Live debug state shared with the menu (Debug tab) ───────────────────────
enum { SRC_INTERP = 0, SRC_HOK, SRC_UPDLOGIC, SRC_FRAMESYNC, SRC_COUNT };
static const char* kSrcName[SRC_COUNT] = { "Interp", "HOK_Interp", "UpdateLogic", "FrameSync" };
static uint64_t g_srcCount[SRC_COUNT] = { 0, 0, 0, 0 };

// Per-OOS-actor tracking. *Move accumulates total XZ path length seen on each
// source while the actor is out of sight — the source whose accumulator grows is
// the live position. cur = MoveComponent.curPosition, rem = remotePosition,
// lpos = ActorLinker.position.
struct DbgActor {
    uint32_t id = 0;
    float lpos[3] = {0,0,0}, cur[3] = {0,0,0}, rem[3] = {0,0,0};
    float lposMove = 0, curMove = 0, remMove = 0;
    float pLpos[3] = {0,0,0}, pCur[3] = {0,0,0}, pRem[3] = {0,0,0};
    bool  hasPrev = false;
    bool  hasMove = false;       // MoveComponent pointer was non-null
    uint32_t srcMask = 0;        // bitmask of hooks that drove this actor
    uint32_t samples = 0;
};
static std::unordered_map<uint32_t, DbgActor> g_dbg;
static std::mutex g_dbgMtx;

// =============================================================================
// Out-Of-Sight (OOS) actor tracking
//
// Root-cause analysis for frozen enemy positions:
//
//   When the server decides actor X is OOS for the local player it sends two
//   independent events:
//     A) NtfSetActorVisible(X, false, pos, ...)  →  actor.SetVisible(false,false)
//     B) OnActorLeaveView(X, seq)
//        └─ ActorManager::OnActorLeaveView  ← removes X from ALL iteration lists
//        └─ OnActorLeaveView_UnregisterEvt  ← unsubscribes HP/state event handlers
//
//   Our Layer-2 hook intercepts (A) and forces logicVis=true so the mesh stays
//   rendered.  But (B) still fires, removing X from ActorManager's HeroActors /
//   SoldierActors / ... lists.  Therefore ActorManager::Interpolation() never
//   iterates X → ActorLinker::Interpolation() is NEVER called → our Layer-4c
//   hook never fires → myTransform stays frozen.
//
// Fix strategy:
//   Layer 4c  Interpolation()     – per-render-frame Unity path (if in lists)
//   Layer 4d  HOK_OnInterpolation – SGW path, called for ALL actors always
//   Layer 7   skip OnActorLeaveView – keep actor in ActorManager render lists
//   Layer 5   OnActorCurHpChange  – force HP update even when "invisible"
//   Layer 6   skip UnregisterEvt  – keep HP/buff event handlers registered
// =============================================================================

struct OOSData {
    float    pos[3];
    float    fwd[3];    // normalised forward direction
    float    speed;     // derived from consecutive packets
    bool     isMoving;
    uint64_t lastNs;    // CLOCK_MONOTONIC ns
};

static std::unordered_map<uint32_t, OOSData> g_oosMap;
static std::unordered_map<uint32_t, void*>   g_actorPtrMap;
static std::unordered_set<uint32_t>          g_oosSet;
static std::mutex                            g_oosMtx;

static void (*_TransformSetPosInj)(void* transform, float* v3) = nullptr;
static void (*_SetActorHp)(void* inst, int32_t curHp, int32_t totalHp) = nullptr;

// Debug accessors (defined here, after g_oosMtx, for use by the menu Debug tab).
static inline size_t maphack_oos_count() {
    std::lock_guard<std::mutex> lk(g_oosMtx);
    return g_oosSet.size();
}
static inline std::vector<DbgActor> maphack_dbg_snapshot() {
    std::lock_guard<std::mutex> lk(g_dbgMtx);
    std::vector<DbgActor> v; v.reserve(g_dbg.size());
    for (auto& p : g_dbg) v.push_back(p.second);
    return v;
}
static inline void maphack_dbg_clear() {
    { std::lock_guard<std::mutex> lk(g_dbgMtx); g_dbg.clear(); }
    for (int i = 0; i < SRC_COUNT; i++) g_srcCount[i] = 0;
}

static inline void actor_cache(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_actorPtrMap[id] = inst;
}
static inline void oos_insert(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oosSet.insert(id);
    g_actorPtrMap[id] = inst;
    HOKMAP_LOG("oos_insert id=%u  (oosSet size=%zu)", id, g_oosSet.size());
}
static inline void oos_remove(uint32_t id) {
    if (!id) return;
    { std::lock_guard<std::mutex> lk(g_oosMtx); g_oosSet.erase(id); }
    // drop debug tracking so re-entered actors start a fresh movement window
    std::lock_guard<std::mutex> lk(g_dbgMtx); g_dbg.erase(id);
}

// =============================================================================
// LAYER 1 – FogOfWar
// =============================================================================
static bool (*_FowIsEnable)() = nullptr;
static bool new_FowIsEnable() {
    if (maphack) return false;
    return _FowIsEnable ? _FowIsEnable() : false;
}
static bool (*_FowGetEnable)() = nullptr;
static bool new_FowGetEnable() {
    if (maphack) return false;
    return _FowGetEnable ? _FowGetEnable() : false;
}
static bool (*_FowGetEnableRender)() = nullptr;
static bool new_FowGetEnableRender() {
    if (maphack) return false;
    return _FowGetEnableRender ? _FowGetEnableRender() : false;
}

// =============================================================================
// LAYER 2 – SetVisible / ForceSetVisible
// =============================================================================
static void (*_ActorSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        actor_cache(inst);
        if (!logicVis) oos_insert(inst);
        else if (inst) oos_remove(*(uint32_t*)((uint64_t)inst + 0x4F4));
        logicVis = true; meshVis = true;
    }
    if (_ActorSetVisible) _ActorSetVisible(inst, logicVis, meshVis);
}
static void (*_ActorForceSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorForceSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        actor_cache(inst);
        if (!logicVis) oos_insert(inst);
        else if (inst) oos_remove(*(uint32_t*)((uint64_t)inst + 0x4F4));
        logicVis = true; meshVis = true;
    }
    if (_ActorForceSetVisible) _ActorForceSetVisible(inst, logicVis, meshVis);
}

// =============================================================================
// LAYER 3 – CheckVisible
// =============================================================================
static bool (*_CheckVisible)(void* attacker, void* target, int32_t flag) = nullptr;
static bool new_CheckVisible(void* attacker, void* target, int32_t flag) {
    if (maphack) return true;
    return _CheckVisible ? _CheckVisible(attacker, target, flag) : false;
}

// =============================================================================
// LAYER 4a – NtfActorMovementData: cache pos/fwd/speed
// =============================================================================
static void (*_NtfActorMovementData)(void* dataPtr) = nullptr;
static void new_NtfActorMovementData(void* dataPtr) {
    if (_NtfActorMovementData) _NtfActorMovementData(dataPtr);
    if (!maphack || !dataPtr) return;

    uint32_t actorID = *(uint32_t*)((uint64_t)dataPtr + 0x08);
    if (!actorID) return;
    float*   pos    = (float*)  ((uint64_t)dataPtr + 0x18);
    int32_t* fwdInt = (int32_t*)((uint64_t)dataPtr + 0x0C);

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    std::lock_guard<std::mutex> lk(g_oosMtx);
    OOSData& d = g_oosMap[actorID];
    if (d.lastNs > 0) {
        float dx = pos[0]-d.pos[0], dz = pos[2]-d.pos[2];
        float dist = sqrtf(dx*dx + dz*dz);
        float dt   = (nowNs - d.lastNs) / 1e9f;
        if (dt > 0.001f && dt < 2.0f) {
            float m = dist / dt;
            if (m < 20.0f) d.speed = m;
        }
        d.isMoving = (dist > 0.05f);
    }
    d.pos[0] = pos[0]; d.pos[1] = pos[1]; d.pos[2] = pos[2];
    float fx = fwdInt[0]/1000.0f, fy = fwdInt[1]/1000.0f, fz = fwdInt[2]/1000.0f;
    float mag = sqrtf(fx*fx + fy*fy + fz*fz);
    if (mag > 0.001f) { fx/=mag; fy/=mag; fz/=mag; }
    d.fwd[0]=fx; d.fwd[1]=fy; d.fwd[2]=fz;
    d.lastNs = nowNs;
}

// =============================================================================
// LAYER 4b – NtfActorMoveState
// =============================================================================
static void (*_NtfActorMoveState)(uint32_t actorID, bool isMoving) = nullptr;
static void new_NtfActorMoveState(uint32_t actorID, bool isMoving) {
    if (_NtfActorMoveState) _NtfActorMoveState(actorID, isMoving);
    if (!maphack) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    auto it = g_oosMap.find(actorID);
    if (it != g_oosMap.end()) it->second.isMoving = isMoving;
}

// =============================================================================
// LAYER 4 root-cause fix – live logic position from MoveComponent
//
//   ActorLinker.MoveControl    @ 0x468  → Assets.Scripts.GameLogic.MoveComponent*
//   MoveComponent.curPosition  @ 0x028  → UnityEngine.Vector3 (x@0x28/y@0x2C/z@0x30)
//
// Why enemies froze when out-of-sight (the bug being fixed here):
//   HOK is a deterministic frame-sync (lockstep) game: ActorLinker.UpdateLogic()
//   steps EVERY actor's MoveComponent every logic frame, so MoveComponent
//   .curPosition keeps advancing for heroes / monsters / soldiers even while the
//   local player can't see them.
//
//   ActorLinker.position (0x50C) is a DIFFERENT field: it is written from server
//   movement packets via ActorLinker.UpdatePosition(SGW.DisplayInfoData), and the
//   server stops sending NtfActorMovementData for out-of-sight actors.  The old
//   code synced myTransform from that packet field (or dead-reckoned from the last
//   packet for ≤3 s), so once the packets stopped the actor animated in place but
//   never moved — exactly the reported symptom.
//
//   Fix: for OOS actors prefer the always-live curPosition; fall back to the packet
//   position / dead-reckoning only when MoveComponent is unavailable.
// =============================================================================
static inline bool read_logic_pos(void* inst, float out[3]) {
    void* moveCtrl = *(void**)((uint64_t)inst + 0x468); // ActorLinker.MoveControl
    if (!moveCtrl) return false;
    float* cp = (float*)((uint64_t)moveCtrl + 0x28);    // MoveComponent.curPosition
    if (cp[0] != cp[0] || cp[2] != cp[2]) return false; // reject NaN
    if (cp[0] == 0.0f && cp[1] == 0.0f && cp[2] == 0.0f) return false; // uninitialised
    out[0] = cp[0]; out[1] = cp[1]; out[2] = cp[2];
    return true;
}

static inline void sync_oos_transform(void* inst, int src) {
    if (!maphack || !inst) return;
    uint32_t objID = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!objID) return;

    if (!g_oosMtx.try_lock()) return;
    bool isOOS = g_oosSet.count(objID) > 0;
    bool haveCache = false; OOSData d{};
    if (isOOS) {
        auto it = g_oosMap.find(objID);
        if (it != g_oosMap.end() && it->second.lastNs > 0) { d = it->second; haveCache = true; }
    }
    g_oosMtx.unlock();
    if (!isOOS) return;

    void* myTransform = *(void**)((uint64_t)inst + 0x740);
    if (!myTransform || !_TransformSetPosInj) return;

    float* lpos = (float*)((uint64_t)inst + 0x50C); // ActorLinker.position (packet/render)

    // ── Diagnostic: record candidate position sources for the menu Debug tab ──
    if (g_mapDebug) {
        if (src >= 0 && src < SRC_COUNT) g_srcCount[src]++;
        void* mc   = *(void**)((uint64_t)inst + 0x468);            // MoveControl
        float* cur = mc ? (float*)((uint64_t)mc + 0x28) : nullptr; // curPosition
        float* rem = mc ? (float*)((uint64_t)mc + 0x34) : nullptr; // remotePosition

        std::lock_guard<std::mutex> lk(g_dbgMtx);
        DbgActor& a = g_dbg[objID];
        a.id = objID;
        a.hasMove = (mc != nullptr);
        a.srcMask |= (src >= 0 && src < SRC_COUNT) ? (1u << src) : 0u;
        a.samples++;
        auto accum = [&](float* prev, const float* now, float& mv) {
            if (a.hasPrev) { float dx = now[0]-prev[0], dz = now[2]-prev[2]; mv += sqrtf(dx*dx + dz*dz); }
            prev[0]=now[0]; prev[1]=now[1]; prev[2]=now[2];
        };
        accum(a.pLpos, lpos, a.lposMove);
        a.lpos[0]=lpos[0]; a.lpos[1]=lpos[1]; a.lpos[2]=lpos[2];
        if (cur) { accum(a.pCur, cur, a.curMove); a.cur[0]=cur[0]; a.cur[1]=cur[1]; a.cur[2]=cur[2]; }
        if (rem) { accum(a.pRem, rem, a.remMove); a.rem[0]=rem[0]; a.rem[1]=rem[1]; a.rem[2]=rem[2]; }
        a.hasPrev = true;
    }

    float writePos[3];

    if (read_logic_pos(inst, writePos)) {
        // PRIORITY 1 – live frame-sync logic position (advances even while OOS).
        // Logic Y is sometimes 0 (height ignored by the planar sim) → keep render Y.
        if (writePos[1] == 0.0f && lpos[1] != 0.0f) writePos[1] = lpos[1];
    } else if (haveCache) {
        float ddx = lpos[0]-d.pos[0], ddz = lpos[2]-d.pos[2];
        if ((ddx*ddx + ddz*ddz) > 1.0f) {
            // PRIORITY 2 – server packet position advanced past the cache.
            writePos[0]=lpos[0]; writePos[1]=lpos[1]; writePos[2]=lpos[2];
        } else {
            // PRIORITY 3 – dead-reckon from the last cached movement packet.
            writePos[0]=d.pos[0]; writePos[1]=d.pos[1]; writePos[2]=d.pos[2];
            if (d.isMoving && d.speed > 0.05f) {
                float fm = sqrtf(d.fwd[0]*d.fwd[0]+d.fwd[1]*d.fwd[1]+d.fwd[2]*d.fwd[2]);
                if (fm > 0.5f && fm < 2.0f) {
                    struct timespec ts2; clock_gettime(CLOCK_MONOTONIC, &ts2);
                    float dt = ((uint64_t)ts2.tv_sec*1000000000ULL+(uint64_t)ts2.tv_nsec - d.lastNs) / 1e9f;
                    if (dt > 0.0f && dt < 3.0f) {
                        writePos[0] += (d.fwd[0]/fm)*d.speed*dt;
                        writePos[1] += (d.fwd[1]/fm)*d.speed*dt;
                        writePos[2] += (d.fwd[2]/fm)*d.speed*dt;
                    }
                }
            }
        }
    } else {
        // PRIORITY 4 – nothing better than the (possibly frozen) render position.
        writePos[0]=lpos[0]; writePos[1]=lpos[1]; writePos[2]=lpos[2];
    }

    // Mirror the live position back into ActorLinker.position (0x50C) so that the
    // minimap blip (MinimapComponentNew) and any get_Position() callers move too,
    // not just the world mesh.  Safe for OOS actors: the packet path overwrites
    // this field again the moment the actor re-enters the local player's vision.
    lpos[0]=writePos[0]; lpos[1]=writePos[1]; lpos[2]=writePos[2];

    _TransformSetPosInj(myTransform, writePos);
}

// =============================================================================
// LAYER 4c – Interpolation(): Unity render-loop path
// Called by ActorManager::Interpolation() for actors still in its lists.
// After Layer 7 skips OnActorLeaveView, OOS actors remain in those lists.
// =============================================================================
static void (*_Interpolation)(void* inst) = nullptr;
static void new_Interpolation(void* inst) {
    if (_Interpolation) _Interpolation(inst);
    sync_oos_transform(inst, SRC_INTERP);
}

// =============================================================================
// LAYER 4d – HOK_OnInterpolation(): SGW engine path
// Called by the SGW/HOKExtend system for EVERY actor regardless of ActorManager
// list membership.  This fires even before Layer 7's fix is confirmed working.
// =============================================================================
static void (*_HOKOnInterpolation)(void* inst) = nullptr;
static void new_HOKOnInterpolation(void* inst) {
    if (_HOKOnInterpolation) _HOKOnInterpolation(inst);
    sync_oos_transform(inst, SRC_HOK);
}

// =============================================================================
// LAYER 4e – ActorLinker::UpdateLogic(int delta): frame-sync logic path
// Runs once per logic frame for EVERY actor (the deterministic lockstep step that
// produces MoveComponent.curPosition).  Guarantees position sync happens even if
// the render-side Interpolation()/HOK_OnInterpolation() are culled for OOS actors.
// Because sync_oos_transform also mirrors the live position into ActorLinker
// .position (0x50C), the game's own Interpolation() then smoothly follows it.
// =============================================================================
static void (*_ActorUpdateLogic)(void* inst, int32_t delta) = nullptr;
static void new_ActorUpdateLogic(void* inst, int32_t delta) {
    if (_ActorUpdateLogic) _ActorUpdateLogic(inst, delta);
    sync_oos_transform(inst, SRC_UPDLOGIC);
}

// =============================================================================
// LAYER 4f – FrameSynchr::UpdateFrame(): self-driven per-logic-frame sweep
//
// Reliability backstop.  In a frame-sync game UpdateFrame() runs every logic
// frame on the logic thread; from here we iterate our cached OOS actors and call
// sync ourselves.  This works even if the game never calls Interpolation()/
// UpdateLogic() on OOS actors (e.g. they were dropped from ActorManager's update
// lists), which is the most likely reason earlier per-actor hooks did nothing.
// Runs on the same thread that frees actors, and each pointer is re-validated by
// its ObjID before use, so it is safe against stale entries.
// =============================================================================
static void (*_FrameUpdate)(void* inst) = nullptr;
static void drive_oos_sync() {
    if (!maphack) return;
    std::vector<std::pair<uint32_t,void*>> snapshot;
    {
        std::lock_guard<std::mutex> lk(g_oosMtx);
        snapshot.reserve(g_oosSet.size());
        for (uint32_t id : g_oosSet) {
            auto it = g_actorPtrMap.find(id);
            if (it != g_actorPtrMap.end() && it->second) snapshot.emplace_back(id, it->second);
        }
    }
    for (auto& pr : snapshot) {
        void* inst = pr.second;
        if (*(uint32_t*)((uint64_t)inst + 0x4F4) != pr.first) continue; // stale/reused
        sync_oos_transform(inst, SRC_FRAMESYNC);
    }
}
static void new_FrameUpdate(void* inst) {
    if (_FrameUpdate) _FrameUpdate(inst);
    drive_oos_sync();
}

// =============================================================================
// LAYER 5 – HP sync for OOS actors
// SGC::OnActorCurHpChange(uint32 objID, int32 curHp, int32 totalHp) is fired
// by the local SGW simulation for every HP change.  The C# handler checks
// _logicVisible and skips OOS actors; we bypass via cached ActorLinker*.
// =============================================================================
static void (*_OnActorCurHpChange)(uint32_t objID, int32_t curHp, int32_t totalHp) = nullptr;
static void new_OnActorCurHpChange(uint32_t objID, int32_t curHp, int32_t totalHp) {
    if (_OnActorCurHpChange) _OnActorCurHpChange(objID, curHp, totalHp);
    if (!maphack) return;
    if (!g_oosMtx.try_lock()) return;
    bool isOOS = g_oosSet.count(objID) > 0;
    void* actor = nullptr;
    if (isOOS) {
        auto it2 = g_actorPtrMap.find(objID);
        if (it2 != g_actorPtrMap.end()) actor = it2->second;
    }
    g_oosMtx.unlock();
    if (!isOOS || !actor || !_SetActorHp) return;
    void* vc = *(void**)((uint64_t)actor + 0x400); // ValueLinkerComponent*
    if (vc) _SetActorHp(vc, curHp, totalHp);
}

// =============================================================================
// LAYER 6 – Keep HP/buff callbacks alive (skip UnregisterEvt)
// =============================================================================
static void (*_OnActorLeaveViewUnregEvt)(uint32_t actorID) = nullptr;
static void new_OnActorLeaveViewUnregEvt(uint32_t actorID) {
    if (!maphack)
        if (_OnActorLeaveViewUnregEvt) _OnActorLeaveViewUnregEvt(actorID);
    // skipped when maphack: keeps all C# event subscriptions alive
}

// =============================================================================
// LAYER 7 – Skip ActorManager::OnActorLeaveView (instance method)
//
// DIAGNOSIS (confirmed by training-camp observation):
//   SGC::OnActorLeaveView does TWO things in sequence:
//     1. actor.SetVisible(false, false)        ← Layer-2 hook intercepts ✓
//     2. ActorManager.OnActorLeaveView(id,seq) ← removes actor from HeroActors/etc.
//
//   Previous Layer 7 skipped SGC::OnActorLeaveView entirely which also skipped
//   step 1 → actor.SetVisible(false) never called → g_oosSet never populated →
//   sync_oos_transform() returned immediately for every actor → no fix at all.
//
// Correct fix: let SGC::OnActorLeaveView run normally (so SetVisible(false)
// fires → Layer 2 catches it → g_oosSet populated), but skip only the inner
// ActorManager::OnActorLeaveView call so the actor stays in HeroActors/etc.
// render lists → ActorManager::Interpolation() still iterates it every frame →
// Layer 4c fires → sync_oos_transform() runs → position updated.
//
// ActorManager::OnActorLeaveView is an INSTANCE method, so native signature is:
//   void fn(void* actorMgrInst, uint32_t actorID, uint32_t objSeq)
// =============================================================================
static void (*_ActorMgrLeaveView)(void* inst, uint32_t actorID, uint32_t objSeq) = nullptr;
static void new_ActorMgrLeaveView(void* inst, uint32_t actorID, uint32_t objSeq) {
    if (maphack) return; // skip — actor stays in ActorManager render/logic lists
    if (_ActorMgrLeaveView) _ActorMgrLeaveView(inst, actorID, objSeq);
}

