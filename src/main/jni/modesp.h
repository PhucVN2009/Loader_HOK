#pragma once
// =============================================================================
// ESP – enemy boxes / lines / health, drawn with ImGui.
// Ported from the hok4 source but every il2cpp method is resolved BY NAME
// (auto-update) and field offsets are verified against this game's dump.
//
// Data is collected from ActorLinker.HOK_OnLateUpdate(int) which the engine
// calls every frame for every active actor (heroes/minions/monsters), so we get
// fresh camp/type/playerId/HP/position without enumerating manager lists.
// Field offsets (verified vs dumpgamenew.cs):
//   ActorLinker.ObjID           0x4F4
//   ActorLinker.position        0x50C  (Vector3)
//   ActorLinker.ValueComponent  0x400  → ValueLinkerComponent
//   ValueLinkerComponent.actorHp 0x38 / actorHpTotal 0x3C
//   ActorLinker.MoveControl     0x468  → MoveComponent.curPosition 0x28 / moveForward 0x40
//   GamePlayerCenter.hostPlayerID 0x58
// =============================================================================
#include <cstdint>
#include <map>
#include <mutex>
#include <cmath>
#include <vector>

// ── ESP toggles (read by the renderer) ──
static bool g_espOn      = false;
static bool g_espBox     = true;
static bool g_espLine    = true;
static bool g_espHealth  = true;
static bool g_espName    = false;   // names need extra string reads; off by default
static bool g_espAlly    = false;   // also draw teammates
static bool g_espDebug   = false;   // diagnostic overlay
static float g_espMaxDist = 200.0f;

// aimbot diagnostics (written by modaim.h, read by the debug overlay here)
static int  g_aimDbgSlot  = -1;   // last skill slot seen by the aimbot hook
static int  g_aimDbgFound = -1;   // 1 = target found, 0 = none, -1 = hook not hit

// ── Actor type / camp enums (from dump) ──
enum { ACTOR_TYPE_HERO = 0 };       // CoreDef.ActorTypeDef: 0 = hero

struct EspActor {
    uint32_t objId = 0, pid = 0;
    int      camp = 0, type = 0;
    int      hp = 0, maxHp = 0;
    float    pos[3]  = {0,0,0};
    float    prev[3] = {0,0,0};
    float    fwd[3]  = {0,0,0};      // movement direction (normalised)
    float    speed   = 0;            // world units / second (approx)
    bool     hasPrev = false;
    uint32_t stamp = 0;             // frame counter for stale cleanup
};

static std::map<uint32_t, EspActor> g_espActors;
static std::mutex g_espMtx;
static uint32_t   g_espFrame   = 0;
static uint32_t   g_hostPid    = 0;
static int        g_hostCamp   = -1;
static bool       g_espCollect = false;  // set by Main: g_espOn || m_aimEnabled

// ── il2cpp accessors / camera (resolved by name from Main.cpp) ──
static int      (*_esp_getObjCamp)(void*)  = nullptr;   // ActorLinker.get_objCamp
static int      (*_esp_getObjType)(void*)  = nullptr;   // ActorLinker.get_objType
static uint32_t (*_esp_getPlayerId)(void*) = nullptr;   // ActorLinker.get_playerId
static void*    (*_esp_getGPC)()           = nullptr;   // GamePlayerCenter.GetPlayerCenterManager
static void*    (*_esp_getMainCamera)()    = nullptr;   // Camera.get_main
static void     (*_esp_world2screen)(void* cam, float* world, int eye, float* outScreen) = nullptr; // WorldToScreenPoint_Injected

// ── Per-frame actor collection hook: ActorLinker.HOK_OnLateUpdate(int) ──
static void (*_esp_HOKLateUpdate)(void* inst, int delta) = nullptr;
static void new_esp_HOKLateUpdate(void* inst, int delta) {
    if (_esp_HOKLateUpdate) _esp_HOKLateUpdate(inst, delta);
    if (!g_espCollect || !inst) return;

    uint32_t objId = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!objId) return;

    int      camp = _esp_getObjCamp  ? _esp_getObjCamp(inst)  : 0;
    int      type = _esp_getObjType  ? _esp_getObjType(inst)  : 0;
    uint32_t pid  = _esp_getPlayerId ? _esp_getPlayerId(inst) : 0;

    void* vc = *(void**)((uint64_t)inst + 0x400);
    int hp = 0, maxHp = 0;
    if (vc) { hp = *(int*)((uint64_t)vc + 0x38); maxHp = *(int*)((uint64_t)vc + 0x3C); }

    float* p = (float*)((uint64_t)inst + 0x50C);

    // refresh host player id occasionally
    if (_esp_getGPC && (g_espFrame % 60 == 0 || g_hostPid == 0)) {
        void* gpc = _esp_getGPC();
        if (gpc) g_hostPid = *(uint32_t*)((uint64_t)gpc + 0x58);
    }

    std::lock_guard<std::mutex> lk(g_espMtx);
    EspActor& a = g_espActors[objId];
    a.objId = objId; a.pid = pid; a.camp = camp; a.type = type;
    a.hp = hp; a.maxHp = maxHp;
    if (a.hasPrev) {
        float dx = p[0]-a.pos[0], dz = p[2]-a.pos[2];
        float d  = sqrtf(dx*dx + dz*dz);
        if (d > 0.001f) { a.fwd[0]=dx/d; a.fwd[1]=0; a.fwd[2]=dz/d; }
        a.speed = d * 60.0f;                         // per-frame dist → ~units/sec
    }
    a.prev[0]=a.pos[0]; a.prev[1]=a.pos[1]; a.prev[2]=a.pos[2];
    a.pos[0] = p[0]; a.pos[1] = p[1]; a.pos[2] = p[2];
    a.hasPrev = true;
    a.stamp = g_espFrame;
    if (pid && pid == g_hostPid) g_hostCamp = camp;  // identify local camp
}

// project a world point → ImGui screen coords; returns false if behind camera
static inline bool esp_project(void* cam, const float* world, float scrH, ImVec2& out) {
    float sp[3] = {0,0,0};
    _esp_world2screen(cam, (float*)world, 0, sp);
    if (sp[2] <= 0.05f) return false;            // behind camera
    out.x = sp[0];
    out.y = scrH - sp[1];                        // Unity y is from bottom → flip
    return true;
}

// ── Draw – called every frame inside the ImGui frame ──
static inline void DrawESP() {
    if (!g_espOn || !_esp_getMainCamera || !_esp_world2screen) return;
    void* cam = _esp_getMainCamera();
    if (!cam) return;

    ImGuiIO& io = ImGui::GetIO();
    float scrW = io.DisplaySize.x, scrH = io.DisplaySize.y;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 bottomMid(scrW * 0.5f, scrH);          // line origin (player area)

    std::vector<EspActor> snap;
    {
        std::lock_guard<std::mutex> lk(g_espMtx);
        // drop stale entries (>120 frames unseen)
        for (auto it = g_espActors.begin(); it != g_espActors.end(); ) {
            if (g_espFrame - it->second.stamp > 120) it = g_espActors.erase(it);
            else { snap.push_back(it->second); ++it; }
        }
    }

    int nEnemy = 0;
    for (auto& a : snap) if (a.type==ACTOR_TYPE_HERO && g_hostCamp>=0 && a.camp!=g_hostCamp && a.hp>0) nEnemy++;

    if (g_espDebug) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "ESP DBG  scr=%.0fx%.0f  actors=%zu  hostPid=%u hostCamp=%d enemies=%d\n"
            "aim: slot=%d found=%d  (S1/2/3=%d)",
            scrW, scrH, snap.size(), g_hostPid, g_hostCamp, nEnemy,
            g_aimDbgSlot, g_aimDbgFound, 0);
        dl->AddText(ImVec2(scrW*0.35f, 8), IM_COL32(255,255,0,255), buf);
    }

    for (auto& a : snap) {
        if (a.type != ACTOR_TYPE_HERO) continue;             // heroes only
        bool enemy = (g_hostCamp >= 0) && (a.camp != g_hostCamp);
        if (!enemy && !g_espAlly) continue;
        if (a.pid && a.pid == g_hostPid) continue;           // skip self
        if (a.maxHp <= 0 || a.hp <= 0) continue;             // dead/invalid

        // feet + head for a vertical box
        float feet[3] = { a.pos[0], a.pos[1],        a.pos[2] };
        float head[3] = { a.pos[0], a.pos[1] + 2.2f, a.pos[2] };
        ImVec2 pf, ph;
        if (!esp_project(cam, feet, scrH, pf)) continue;
        if (!esp_project(cam, head, scrH, ph)) continue;

        if (g_espDebug) {
            // mark the raw feet projection so we can see if it lands on the actor
            dl->AddCircleFilled(pf, 6.0f, IM_COL32(255,0,255,255));
            char db[96]; snprintf(db, sizeof(db), "(%.0f,%.0f) w(%.0f,%.0f,%.0f)",
                pf.x, pf.y, a.pos[0], a.pos[1], a.pos[2]);
            dl->AddText(ImVec2(pf.x+8, pf.y), IM_COL32(255,255,255,255), db);
        }

        if (pf.x < -100 || pf.x > scrW + 100) continue;

        float h = pf.y - ph.y; if (h < 6) h = 6;
        float w = h * 0.5f;
        ImU32 col = enemy ? IM_COL32(255, 60, 60, 255) : IM_COL32(60, 180, 255, 255);

        if (g_espLine)
            dl->AddLine(bottomMid, ImVec2(pf.x, pf.y), col, 1.5f);

        if (g_espBox) {
            ImVec2 a0(pf.x - w*0.5f, ph.y), a1(pf.x + w*0.5f, pf.y);
            dl->AddRect(a0, a1, col, 0, 0, 1.5f);
        }

        if (g_espHealth) {
            float hpFrac = (float)a.hp / (float)a.maxHp; if (hpFrac < 0) hpFrac = 0; if (hpFrac > 1) hpFrac = 1;
            float bx = pf.x - w*0.5f - 5;
            ImVec2 b0(bx - 3, ph.y), b1(bx, pf.y);
            dl->AddRectFilled(b0, b1, IM_COL32(0,0,0,180));
            ImU32 hpc = IM_COL32((int)(255*(1-hpFrac)), (int)(255*hpFrac), 0, 255);
            dl->AddRectFilled(ImVec2(b0.x, b1.y - h*hpFrac), b1, hpc);
        }
    }
}
