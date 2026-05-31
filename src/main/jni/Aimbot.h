#pragma once
// =============================================================================
//  Aimbot – adapted from the old Java/hardcoded-offset menu to fit THIS ImGui
//  source.  Every il2cpp function is resolved by NAME via Il2CppGetMethodOffset
//  (survives game updates); field offsets re-verified against the 11.3.1.10 dump.
//
//  Menu (★ AIMBOT ★):
//     Aimbot (Bật/Tắt)        – master switch
//     Aim Distance (10-150)   – max target range
//     Aim Smooth (1-50)       – smoothing / bullet-speed factor
//     Aim Skill 1 / 2 / 3     – which skill slots the aim applies to
//     Aimbot Debug Overlay    – on-screen status text
//
//  Mechanism: hook SkillControlIndicator.GetUseSkillDirection() and return a
//  direction pointing at the selected enemy.  CSkillButtonManager.Update() is
//  hooked as a fallback to learn the active skill slot.  Actors are tracked via
//  ActorLinker.HOK_OnSpawnActor / HOK_OnLateUpdate.
// =============================================================================
#include <cstdint>
#include <cmath>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdio>
#include "imgui.h"
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"
#include "Dobby/dobby.h"

// -----------------------------------------------------------------------------
// Settings (bound to the menu)
// -----------------------------------------------------------------------------
static bool  m_aimEnabled  = true;    // 10_CheckBox_True_Aimbot (Bật/Tắt)
static int   m_aimDistance = 150;     // 11_InputValue_150_Aim Distance (10-150)
static int   m_aimSmooth   = 50;      // 12_InputValue_50_Aim Smooth (1-50)
static bool  m_aimSkill1   = true;    // 13_CheckBox_True_Aim Skill 1
static bool  m_aimSkill2   = true;    // 14_CheckBox_True_Aim Skill 2
static bool  m_aimSkill3   = true;    // 15_CheckBox_True_Aim Skill 3
static bool  m_aimSkill4   = true;    // Aim Skill 4 (heroes with a 4th skill)
static bool  m_aimDebug    = false;   // 16_CheckBox_False_Aimbot Debug Overlay
static int   m_aimType     = 2;       // 0=Lowest HP, 1=Lowest HP%, 2=Closest
static bool  m_aimDrawRange = true;   // draw the aim-range circle
static bool  m_aimDrawLine  = true;   // draw a line to the current target

static bool  g_antiBan     = true;    // 1_CheckBox_True_Bật Anti-Ban

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
#define AIM_ACTOR_HERO    0
#define AIM_CAMP_MID      0

// Field offsets (verified against com...11.3.1.10.cs)
#define AIM_OFF_VALUE_COMP    0x400   // ActorLinker.ValueComponent
#define AIM_OFF_MOVE_CONTROL  0x468   // ActorLinker.MoveControl
#define AIM_OFF_POSITION      0x50C   // ActorLinker.position (Vector3)
#define AIM_OFF_MOVE_CURPOS   0x28    // MoveComponent.curPosition
#define AIM_OFF_MOVE_REMOTE   0x34    // MoveComponent.remotePosition
#define AIM_OFF_MOVE_FORWARD  0x40    // MoveComponent.moveForward (Vector3)
#define AIM_OFF_VAL_HP        0x38    // ValueLinkerComponent.actorHp
#define AIM_OFF_VAL_HPTOTAL   0x3C    // ValueLinkerComponent.actorHpTotal
#define AIM_OFF_GPC_HOSTPID   0x58    // GamePlayerCenter.hostPlayerID
#define AIM_OFF_GPC_HOSTPLY   0x60    // GamePlayerCenter._hostPlayer
#define AIM_OFF_PLAYER_CAMP   0x7C    // Player.playerCamp
#define AIM_OFF_PLAYER_CAPT   0x1F8   // Player.Captain (PoolObjHandle<ActorLinker>)
#define AIM_OFF_POOLHANDLE_OBJ 0x08   // PoolObjHandle._handleObj (object ptr) within the handle

// SkillControlIndicator.skillSlot (SkillSlotLinker*) @ 0x58, SlotType @ 0x30
#define AIM_OFF_SCI_SKILLSLOT 0x58
#define AIM_OFF_SLOT_TYPE     0x30
// CSkillButtonManager.m_usingSlot (SkillSlotType) @ 0x170
#define AIM_OFF_BTNMGR_SLOT   0x170

#define AIM_MAX_CACHE 1024

struct AimVec3 { float x, y, z; };

// -----------------------------------------------------------------------------
// Resolved function pointers
// -----------------------------------------------------------------------------
static int      (*aim_GetCamp)(void*)                       = nullptr;
static int      (*aim_GetType)(void*)                       = nullptr;
static uint32_t (*aim_GetPid)(void*)                        = nullptr;
static void*    (*aim_GpcManager)(void*)                    = nullptr;
static bool     (*aim_IsHostView)(void*)                    = nullptr;  // ActorHelper.IsHostPlayerView
static void*    (*aim_CamGetMain)(void*)                    = nullptr;  // Camera.get_main
static void     (*aim_W2S)(void*, AimVec3*, int, AimVec3*)  = nullptr;  // Camera.WorldToScreenPoint_Injected

// -----------------------------------------------------------------------------
// Actor cache
// -----------------------------------------------------------------------------
struct AimCachedActor { uintptr_t inst; int camp; int type; int lastSeen; };
static std::vector<AimCachedActor> g_aimCache;
static std::mutex                  g_aimMutex;
static std::atomic<int>            g_aimFrame{0};

// Host actor (set by the late-update hook when playerId matches host)
static std::atomic<uintptr_t>      g_aimHostActor{0};
static std::atomic<uint32_t>       g_aimHostPid{0};
static int                         g_aimHostCamp = -1;

// Current target + skill slot (for debug overlay / drawing)
static std::atomic<uintptr_t>      g_aimTarget{0};
static std::atomic<int>            g_aimSlot{0};
static float                       g_aimTargetDist = 0.0f;
static int                         g_aimTargetHp = 0, g_aimTargetHpMax = 0;
static AimVec3                     g_aimTargetPos = {0,0,0};   // last target world pos (for line)
static std::atomic<bool>           g_aimHasTarget{false};

// -----------------------------------------------------------------------------
// Safe memory helpers
// -----------------------------------------------------------------------------
static inline bool aimValidPtr(uintptr_t a) { return a > 0x10000 && a < 0x7fffffffffffULL; }
template<typename T> static inline T aimRead(uintptr_t a, T def) {
    if (!aimValidPtr(a)) return def;
    return *(T*)a;
}

static inline float aimMag(AimVec3 v)  { return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); }
static inline AimVec3 aimNorm(AimVec3 v){ float m = aimMag(v); if (m < 1e-4f) return {0,0,0}; return {v.x/m, v.y/m, v.z/m}; }
static inline float aimDist(AimVec3 a, AimVec3 b){ AimVec3 d{a.x-b.x,a.y-b.y,a.z-b.z}; return aimMag(d); }

static inline bool aimReadPos(uintptr_t actor, AimVec3& pos) {
    uintptr_t mc = aimRead<uintptr_t>(actor + AIM_OFF_MOVE_CONTROL, 0);
    if (aimValidPtr(mc)) {
        pos.x = aimRead<float>(mc + AIM_OFF_MOVE_CURPOS,     0.0f);
        pos.y = aimRead<float>(mc + AIM_OFF_MOVE_CURPOS + 4, 0.0f);
        pos.z = aimRead<float>(mc + AIM_OFF_MOVE_CURPOS + 8, 0.0f);
        if (pos.x != 0.0f || pos.z != 0.0f) return true;
        pos.x = aimRead<float>(mc + AIM_OFF_MOVE_REMOTE,     0.0f);
        pos.y = aimRead<float>(mc + AIM_OFF_MOVE_REMOTE + 4, 0.0f);
        pos.z = aimRead<float>(mc + AIM_OFF_MOVE_REMOTE + 8, 0.0f);
        if (pos.x != 0.0f || pos.z != 0.0f) return true;
    }
    pos.x = aimRead<float>(actor + AIM_OFF_POSITION,     0.0f);
    pos.y = aimRead<float>(actor + AIM_OFF_POSITION + 4, 0.0f);
    pos.z = aimRead<float>(actor + AIM_OFF_POSITION + 8, 0.0f);
    return (pos.x != 0.0f || pos.z != 0.0f);
}

static inline bool aimReadForward(uintptr_t actor, AimVec3& fwd) {
    uintptr_t mc = aimRead<uintptr_t>(actor + AIM_OFF_MOVE_CONTROL, 0);
    if (!aimValidPtr(mc)) { fwd = {0,0,0}; return false; }
    fwd.x = aimRead<float>(mc + AIM_OFF_MOVE_FORWARD,     0.0f);
    fwd.y = aimRead<float>(mc + AIM_OFF_MOVE_FORWARD + 4, 0.0f);
    fwd.z = aimRead<float>(mc + AIM_OFF_MOVE_FORWARD + 8, 0.0f);
    return (fwd.x != 0.0f || fwd.z != 0.0f);
}

// -----------------------------------------------------------------------------
// Host helpers
// -----------------------------------------------------------------------------
static inline void AimUpdateHost() {
    if (!aim_GpcManager) return;
    void* gpc = aim_GpcManager(nullptr);
    if (!gpc) return;
    uintptr_t g = (uintptr_t)gpc;
    uint32_t pid = aimRead<uint32_t>(g + AIM_OFF_GPC_HOSTPID, 0);
    if (pid) g_aimHostPid.store(pid);
    uintptr_t hp = aimRead<uintptr_t>(g + AIM_OFF_GPC_HOSTPLY, 0);
    if (aimValidPtr(hp)) {
        g_aimHostCamp = aimRead<int>(hp + AIM_OFF_PLAYER_CAMP, -1);
        // Player.Captain is a PoolObjHandle<ActorLinker>; the object pointer
        // lives at +0x08 of the handle (offset +0x00 is _handleSeq).
        uintptr_t cap = aimRead<uintptr_t>(hp + AIM_OFF_PLAYER_CAPT + AIM_OFF_POOLHANDLE_OBJ, 0);
        if (aimValidPtr(cap)) g_aimHostActor.store(cap);
    }
}

// -----------------------------------------------------------------------------
// Hooks: actor cache + host detection
// -----------------------------------------------------------------------------
static void (*orig_AimSpawn)(void*) = nullptr;
static void hook_AimSpawn(void* inst) {
    if (orig_AimSpawn) orig_AimSpawn(inst);
    if (!inst) return;
    uintptr_t a = (uintptr_t)inst;
    int camp = aim_GetCamp ? aim_GetCamp(inst) : -1;
    int type = aim_GetType ? aim_GetType(inst) : -1;
    std::lock_guard<std::mutex> lk(g_aimMutex);
    for (auto& c : g_aimCache) if (c.inst == a) { c.camp = camp; c.type = type; return; }
    if (g_aimCache.size() >= AIM_MAX_CACHE) return;
    g_aimCache.push_back({a, camp, type, g_aimFrame.load()});
}

static void (*orig_AimLate)(void*, int) = nullptr;
static void hook_AimLate(void* inst, int delta) {
    if (inst) {
        uintptr_t a = (uintptr_t)inst;
        int frame = g_aimFrame.load();
        {
            std::lock_guard<std::mutex> lk(g_aimMutex);
            bool found = false;
            for (auto& c : g_aimCache) if (c.inst == a) { c.lastSeen = frame; found = true; break; }
            if (!found && g_aimCache.size() < AIM_MAX_CACHE) {
                int camp = aim_GetCamp ? aim_GetCamp(inst) : -1;
                int type = aim_GetType ? aim_GetType(inst) : -1;
                g_aimCache.push_back({a, camp, type, frame});
            }
        }
        // host detection: prefer the game's own check, fall back to playerId
        bool isHost = false;
        if (aim_IsHostView) isHost = aim_IsHostView(inst);
        if (!isHost) {
            uint32_t hostPid = g_aimHostPid.load();
            if (hostPid && aim_GetPid) {
                uint32_t pid = aim_GetPid(inst);
                isHost = (pid && pid == hostPid);
            }
        }
        if (isHost) g_aimHostActor.store(a);
    }
    if (orig_AimLate) orig_AimLate(inst, delta);
}

// -----------------------------------------------------------------------------
// Target selection – closest enemy hero within range
// -----------------------------------------------------------------------------
static inline uintptr_t AimSelectTarget(AimVec3 myPos, int myCamp, AimVec3& outPos) {
    std::vector<AimCachedActor> snap;
    {
        std::lock_guard<std::mutex> lk(g_aimMutex);
        snap = g_aimCache;
    }
    uintptr_t best = 0; float bestScore = 1e30f; AimVec3 bestPos{0,0,0};
    int bestHp = 0, bestHpMax = 0; float bestDist = 0.0f;
    float maxRange = (float)m_aimDistance;

    for (auto& c : snap) {
        uintptr_t actor = c.inst;
        if (!aimValidPtr(actor)) continue;
        if (c.type != AIM_ACTOR_HERO) continue;                 // only heroes
        if (c.camp == myCamp || c.camp == AIM_CAMP_MID) continue; // skip ally/neutral

        AimVec3 ePos;
        if (!aimReadPos(actor, ePos)) continue;

        float d = aimDist(myPos, ePos);
        if (d > maxRange || d < 0.5f) continue;

        // skip dead enemies
        int hp = 0, hpMax = 1;
        uintptr_t vc = aimRead<uintptr_t>(actor + AIM_OFF_VALUE_COMP, 0);
        if (aimValidPtr(vc)) {
            hp = aimRead<int>(vc + AIM_OFF_VAL_HP, 0);
            hpMax = aimRead<int>(vc + AIM_OFF_VAL_HPTOTAL, 1);
        }
        if (hp <= 0) continue;

        // score by aim type (lower = better)
        float score;
        switch (m_aimType) {
            case 0:  score = (float)hp; break;                        // lowest HP
            case 1:  score = (float)hp / (float)(hpMax > 0 ? hpMax : 1); break; // lowest HP%
            default: score = d; break;                                // closest
        }

        if (score < bestScore) {
            bestScore = score; best = actor; bestPos = ePos;
            bestHp = hp; bestHpMax = hpMax; bestDist = d;
        }
    }

    if (best) {
        outPos = bestPos;
        g_aimTargetDist = bestDist;
        g_aimTargetHp = bestHp; g_aimTargetHpMax = bestHpMax;
    }
    return best;
}

// Predictive aim direction (lead the target by its movement)
static inline AimVec3 AimCalcDir(AimVec3 myPos, AimVec3 enemyPos, AimVec3 enemyFwd,
                                 float enemySpeed, float bulletSpeed) {
    AimVec3 to{enemyPos.x-myPos.x, enemyPos.y-myPos.y, enemyPos.z-myPos.z};
    float dist = aimMag(to);
    float bs = (bulletSpeed < 1e-3f) ? 10.0f : bulletSpeed;
    float t = dist / bs;
    AimVec3 future{
        enemyPos.x + enemyFwd.x * enemySpeed * t,
        enemyPos.y + enemyFwd.y * enemySpeed * t,
        enemyPos.z + enemyFwd.z * enemySpeed * t
    };
    return aimNorm(AimVec3{future.x-myPos.x, future.y-myPos.y, future.z-myPos.z});
}

// -----------------------------------------------------------------------------
// HOOK: SkillControlIndicator.GetUseSkillDirection() -> Vector3
//   Returns an overridden direction toward the chosen enemy when aimbot is on.
// -----------------------------------------------------------------------------
static AimVec3 (*orig_GetUseSkillDir)(void*) = nullptr;
static AimVec3 hook_GetUseSkillDir(void* instance) {
    AimVec3 orig = orig_GetUseSkillDir ? orig_GetUseSkillDir(instance) : AimVec3{0,0,0};
    if (!instance || !m_aimEnabled) return orig;

    // active skill slot from the indicator: +0x58 -> SkillSlotLinker, +0x30 -> SlotType
    // SlotType enum: 1=skill1, 2=skill2, 3=skill3, 4=EX3 / 5=skill4 -> map to "4"
    int rawSlot = 0;
    uintptr_t ssl = aimRead<uintptr_t>((uintptr_t)instance + AIM_OFF_SCI_SKILLSLOT, 0);
    if (aimValidPtr(ssl)) rawSlot = aimRead<int>(ssl + AIM_OFF_SLOT_TYPE, 0);
    int slot = rawSlot;
    if (rawSlot == 4 || rawSlot == 5) slot = 4;     // 4th-skill heroes
    if (slot >= 1 && slot <= 4) g_aimSlot.store(slot);

    bool slotAllowed = (slot == 0)
                    || (slot == 1 && m_aimSkill1)
                    || (slot == 2 && m_aimSkill2)
                    || (slot == 3 && m_aimSkill3)
                    || (slot == 4 && m_aimSkill4);
    if (!slotAllowed) return orig;

    uintptr_t host = g_aimHostActor.load();
    if (!host) return orig;

    AimVec3 myPos;
    if (!aimReadPos(host, myPos)) return orig;

    AimVec3 ePos;
    uintptr_t target = AimSelectTarget(myPos, g_aimHostCamp, ePos);
    g_aimTarget.store(target);
    if (!target) { g_aimHasTarget.store(false); return orig; }
    g_aimTargetPos = ePos;
    g_aimHasTarget.store(true);

    AimVec3 eFwd; aimReadForward(target, eFwd);
    // smooth 1..50 -> larger smooth = slower/heavier lead (lower bullet speed)
    float smooth = (float)(m_aimSmooth < 1 ? 1 : m_aimSmooth);
    float bulletSpeed = (float)m_aimDistance / smooth;
    float enemySpeed  = aimMag(eFwd) > 0.01f ? 6.0f : 0.0f;   // rough lead factor
    return AimCalcDir(myPos, ePos, aimNorm(eFwd), enemySpeed, bulletSpeed);
}

// -----------------------------------------------------------------------------
// HOOK: CSkillButtonManager.Update() – fallback slot tracking
// -----------------------------------------------------------------------------
static void (*orig_BtnMgrUpdate)(void*) = nullptr;
static void hook_BtnMgrUpdate(void* instance) {
    if (instance && g_aimSlot.load() == 0) {
        int slot = aimRead<int>((uintptr_t)instance + AIM_OFF_BTNMGR_SLOT, 0);
        if (slot >= 1 && slot <= 3) g_aimSlot.store(slot);
    }
    if (orig_BtnMgrUpdate) orig_BtnMgrUpdate(instance);
}

// -----------------------------------------------------------------------------
// Install (called from hack_injec in Main.cpp)
// -----------------------------------------------------------------------------
static inline void AimbotInstallHook() {
    aim_GetCamp = (int (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_objCamp", 0);
    aim_GetType = (int (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_objType", 0);
    aim_GetPid = (uint32_t (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_playerId", 0);
    aim_GpcManager = (void* (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "GamePlayerCenter", "GetPlayerCenterManager", 0);
    aim_IsHostView = (bool (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorHelper", "IsHostPlayerView", 1);
    aim_CamGetMain = (void* (*)(void*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "get_main", 0);
    aim_W2S = (void (*)(void*, AimVec3*, int, AimVec3*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "WorldToScreenPoint_Injected", 3);

    void* spawn = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "HOK_OnSpawnActor", 0);
    if (spawn) DobbyHook(spawn, (void*)hook_AimSpawn, (void**)&orig_AimSpawn);

    void* late = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "HOK_OnLateUpdate", 1);
    if (late) DobbyHook(late, (void*)hook_AimLate, (void**)&orig_AimLate);

    void* getDir = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "SkillControlIndicator", "GetUseSkillDirection", 0);
    if (getDir) DobbyHook(getDir, (void*)hook_GetUseSkillDir, (void**)&orig_GetUseSkillDir);

    void* btnUpd = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameSystem", "CSkillButtonManager", "Update", 0);
    if (btnUpd) DobbyHook(btnUpd, (void*)hook_BtnMgrUpdate, (void**)&orig_BtnMgrUpdate);
}

// -----------------------------------------------------------------------------
// Per-frame upkeep (called from the render loop): refresh host + prune cache
// -----------------------------------------------------------------------------
static inline void AimbotTick() {
    g_aimFrame++;
    AimUpdateHost();
    int frame = g_aimFrame.load();
    std::lock_guard<std::mutex> lk(g_aimMutex);
    for (auto it = g_aimCache.begin(); it != g_aimCache.end();) {
        if (!aimValidPtr(it->inst) || (frame - it->lastSeen) > 300)
            it = g_aimCache.erase(it);
        else ++it;
    }
}

// World -> screen (returns false if behind camera). Unity screen origin is
// bottom-left, so flip Y to ImGui's top-left.
static inline bool AimW2S(void* cam, const AimVec3& w, float W, float H, ImVec2& out) {
    if (!aim_W2S) return false;
    AimVec3 in = w, sp = {0,0,0};
    aim_W2S(cam, &in, 2 /*Mono*/, &sp);
    if (sp.z <= 0.0f) return false;
    out = ImVec2(sp.x, H - sp.y);
    return true;
}

// -----------------------------------------------------------------------------
// Drawing: aim-range circle + line to target + (optional) debug text.
// Called every frame from the render loop.
// -----------------------------------------------------------------------------
static inline void DrawAimbot(int screenW, int screenH) {
    if (!m_aimEnabled && !m_aimDebug) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = (float)screenW, H = (float)screenH;

    void* cam = aim_CamGetMain ? aim_CamGetMain(nullptr) : nullptr;
    uintptr_t host = g_aimHostActor.load();

    AimVec3 myPos; bool haveMy = (cam && host && aimReadPos(host, myPos));

    // ---- aim-range circle ON THE GROUND around the host hero ----
    // Sample points of a world-space circle at the hero's feet (constant Y) and
    // project each one; drawing a flat 2D screen circle would bend up into the
    // sky because of the camera tilt.
    if (m_aimEnabled && m_aimDrawRange && haveMy) {
        const int   N = 48;
        const float R = (float)m_aimDistance;
        ImVec2 pts[N];
        bool   ok[N];
        for (int i = 0; i < N; ++i) {
            float ang = (float)i / (float)N * 6.2831853f;
            AimVec3 p = { myPos.x + R * cosf(ang), myPos.y, myPos.z + R * sinf(ang) };
            ok[i] = AimW2S(cam, p, W, H, pts[i]);
        }
        for (int i = 0; i < N; ++i) {
            int j = (i + 1) % N;
            if (ok[i] && ok[j])
                dl->AddLine(pts[i], pts[j], IM_COL32(0, 220, 255, 170), 2.0f);
        }
    }

    // ---- line from host to the current target (live position each frame) ----
    // Re-select + re-read the target every frame so the line tracks the enemy
    // as it moves instead of freezing at the last skill-press position.
    if (m_aimEnabled && m_aimDrawLine && haveMy) {
        AimVec3 ePos;
        uintptr_t tgt = AimSelectTarget(myPos, g_aimHostCamp, ePos);
        if (tgt) {
            ImVec2 a, b;
            if (AimW2S(cam, myPos, W, H, a) && AimW2S(cam, ePos, W, H, b)) {
                dl->AddLine(a, b, IM_COL32(255, 40, 40, 220), 2.0f);
                dl->AddCircleFilled(b, 6.0f, IM_COL32(255, 40, 40, 220));
            }
        }
    }

    // ---- debug overlay text ----
    if (m_aimDebug) {
        char buf[256];
        uintptr_t tgt = g_aimTarget.load();
        int cache; { std::lock_guard<std::mutex> lk(g_aimMutex); cache = (int)g_aimCache.size(); }
        const char* typeName = (m_aimType == 0) ? "LowHP" : (m_aimType == 1) ? "LowHP%" : "Closest";
        if (!m_aimEnabled) {
            snprintf(buf, sizeof(buf), "Aimbot: OFF | S1=%d S2=%d S3=%d S4=%d",
                     (int)m_aimSkill1, (int)m_aimSkill2, (int)m_aimSkill3, (int)m_aimSkill4);
        } else if (!tgt) {
            snprintf(buf, sizeof(buf),
                     "Aimbot: ON | type=%s slot=%d | dist=%d smooth=%d | cache=%d | host=%s | NO TARGET",
                     typeName, g_aimSlot.load(), m_aimDistance, m_aimSmooth, cache, host ? "OK" : "FAIL");
        } else {
            snprintf(buf, sizeof(buf),
                     "Aimbot: TARGET | type=%s slot=%d | dist=%.1f | hp=%d/%d | cache=%d",
                     typeName, g_aimSlot.load(), g_aimTargetDist, g_aimTargetHp, g_aimTargetHpMax, cache);
        }
        dl->AddRectFilled(ImVec2(8, 8), ImVec2(8 + ImGui::CalcTextSize(buf).x + 8, 30),
                          IM_COL32(0, 0, 0, 150));
        dl->AddText(ImVec2(12, 11), IM_COL32(0, 255, 130, 255), buf);
    }
}
