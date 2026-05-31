#pragma once
// =============================================================================
//  ESP overlay – drawn with ImGui background draw-list every frame.
//
//  Ported from the old Java/Canvas menu to fit THIS ImGui source:
//   * functions/offsets are resolved by NAME through Il2CppGetMethodOffset
//     (survives game updates – no hard-coded il2cpp offsets),
//   * field offsets re-checked against the 11.3.1.10 dump,
//   * everything is rendered with ImGui (no JNI / no PlayerInfo array).
//
//  Features (menu "Cài đặt ESP"):
//     Bật Anti-Ban     – gate the AnoSDK report-data bypass (default ON)
//     ESP: Đường kẻ    – line from bottom-center to the actor
//     ESP: Hộp         – 2D box
//     ESP: Thanh máu    – HP bar above the box
//     ESP: Khoảng cách  – distance (m) from local hero
//     ESP: Lính         – include enemy minions
//     ESP: Quái rừng    – include neutral jungle monsters
// =============================================================================
#include <cstdint>
#include <cmath>
#include <vector>
#include <mutex>
#include <cstdio>
#include "imgui.h"
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"
#include "Dobby/dobby.h"

// -----------------------------------------------------------------------------
// Menu toggles (read by DrawMenu in Main.cpp)
// -----------------------------------------------------------------------------
static bool g_antiBan     = true;   // 1_CheckBox_True_Bật Anti-Ban
static bool g_espLine     = false;  // 2_CheckBox_False_ESP: Đường kẻ
static bool g_espBox      = false;  // 3_CheckBox_False_ESP: Hộp
static bool g_espHealth   = false;  // 4_CheckBox_False_ESP: Thanh máu
static bool g_espDistance = false;  // 5_CheckBox_False_ESP: Khoảng cách
static bool g_espMinion   = false;  // 7_CheckBox_False_ESP: Lính (Minion)
static bool g_espMonster  = false;  // 8_CheckBox_False_ESP: Quái rừng (Monster)

// Master flag: any drawing feature on (used by Main.cpp + native fow hook)
static inline bool EspIsActive() {
    return g_espLine || g_espBox || g_espHealth || g_espDistance ||
           g_espMinion || g_espMonster;
}
// modmap.h reads this to also reveal native fog while ESP is on.
bool g_espActive = false;

// -----------------------------------------------------------------------------
// Actor type / camp constants (CoreDef.ActorTypeDef, CSProtocol.COM_PLAYERCAMP)
// -----------------------------------------------------------------------------
#define ESP_ACTOR_HERO     0
#define ESP_ACTOR_MONSTER  1
#define ESP_ACTOR_ORGAN    2
#define ESP_CAMP_MID       0

// -----------------------------------------------------------------------------
// Field offsets (verified against com.levelinfinite.sgameGlobal.midaspay_11.3.1.10.cs)
// -----------------------------------------------------------------------------
#define ESP_OFF_VALUE_COMP        0x400   // ActorLinker.ValueComponent
#define ESP_OFF_MOVE_CONTROL      0x468   // ActorLinker.MoveControl
#define ESP_OFF_POSITION          0x50C   // ActorLinker.position (Vector3)
#define ESP_OFF_LOGIC_VISIBLE     0x559   // ActorLinker._logicVisible
#define ESP_OFF_MOVE_CURPOS       0x28    // MoveComponent.curPosition
#define ESP_OFF_MOVE_REMOTEPOS    0x34    // MoveComponent.remotePosition
#define ESP_OFF_VAL_HP            0x38    // ValueLinkerComponent.actorHp
#define ESP_OFF_VAL_HPTOTAL       0x3C    // ValueLinkerComponent.actorHpTotal
#define ESP_OFF_GPC_HOSTPID       0x58    // GamePlayerCenter.hostPlayerID
#define ESP_OFF_GPC_HOSTPLAYER    0x60    // GamePlayerCenter._hostPlayer
#define ESP_OFF_PLAYER_CAMP       0x7C    // Player.playerCamp

#define ESP_MAX_CACHE 1024

struct EspVec3 { float x, y, z; };

// -----------------------------------------------------------------------------
// Resolved function pointers
// -----------------------------------------------------------------------------
static void*    (*esp_CamGetMain)(void* method)                      = nullptr;
static void     (*esp_W2S)(void* cam, EspVec3* pos, int eye, EspVec3* out) = nullptr;
static int      (*esp_GetCamp)(void* inst)                           = nullptr;
static int      (*esp_GetType)(void* inst)                           = nullptr;
static uint32_t (*esp_GetPid)(void* inst)                            = nullptr;
static void*    (*esp_GpcManager)(void* method)                      = nullptr;

// -----------------------------------------------------------------------------
// Actor cache – populated by the spawn hook, kept fresh by the late-update hook
// -----------------------------------------------------------------------------
struct EspCachedActor {
    uintptr_t inst;
    int       camp;
    int       type;
    int       lastSeen;   // frame counter
};
static std::vector<EspCachedActor> g_espCache;
static std::mutex                  g_espMutex;
static int                         g_espFrame = 0;

// -----------------------------------------------------------------------------
// Safe memory helpers (kept lightweight, like modmap.h)
// -----------------------------------------------------------------------------
static inline bool espValidPtr(uintptr_t a) {
    return a > 0x10000 && a < 0x7fffffffffffULL;
}
template<typename T>
static inline T espRead(uintptr_t a, T def) {
    if (!espValidPtr(a)) return def;
    return *(T*)a;
}

// -----------------------------------------------------------------------------
// HOOK: ActorLinker.HOK_OnSpawnActor() – cache every new actor
// -----------------------------------------------------------------------------
static void (*orig_EspSpawn)(void* inst) = nullptr;
static void hook_EspSpawn(void* inst) {
    if (orig_EspSpawn) orig_EspSpawn(inst);
    if (!inst) return;

    uintptr_t a = (uintptr_t)inst;
    int camp = esp_GetCamp ? esp_GetCamp(inst) : -1;
    int type = esp_GetType ? esp_GetType(inst) : -1;

    std::lock_guard<std::mutex> lk(g_espMutex);
    for (auto& c : g_espCache) if (c.inst == a) { c.camp = camp; c.type = type; return; }
    if (g_espCache.size() >= ESP_MAX_CACHE) return;
    g_espCache.push_back({a, camp, type, g_espFrame});
}

// -----------------------------------------------------------------------------
// HOOK: ActorLinker.HOK_OnLateUpdate(int) – mark liveness for every active actor
// -----------------------------------------------------------------------------
static void (*orig_EspLate)(void* inst, int delta) = nullptr;
static void hook_EspLate(void* inst, int delta) {
    if (inst) {
        uintptr_t a = (uintptr_t)inst;
        std::lock_guard<std::mutex> lk(g_espMutex);
        bool found = false;
        for (auto& c : g_espCache) { if (c.inst == a) { c.lastSeen = g_espFrame; found = true; break; } }
        if (!found && g_espCache.size() < ESP_MAX_CACHE) {
            int camp = esp_GetCamp ? esp_GetCamp(inst) : -1;
            int type = esp_GetType ? esp_GetType(inst) : -1;
            g_espCache.push_back({a, camp, type, g_espFrame});
        }
    }
    if (orig_EspLate) orig_EspLate(inst, delta);
}

// -----------------------------------------------------------------------------
// Install – called from hack_injec() in Main.cpp
// -----------------------------------------------------------------------------
static inline void EspInstallHook() {
    // Camera
    esp_CamGetMain = (void* (*)(void*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "get_main", 0);
    esp_W2S = (void (*)(void*, EspVec3*, int, EspVec3*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "WorldToScreenPoint_Injected", 3);

    // ActorLinker property getters
    esp_GetCamp = (int (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_objCamp", 0);
    esp_GetType = (int (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_objType", 0);
    esp_GetPid = (uint32_t (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_playerId", 0);

    // GamePlayerCenter singleton accessor (host camp / host id)
    esp_GpcManager = (void* (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "GamePlayerCenter", "GetPlayerCenterManager", 0);

    // Hooks
    void* spawn = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "HOK_OnSpawnActor", 0);
    if (spawn) DobbyHook(spawn, (void*)hook_EspSpawn, (void**)&orig_EspSpawn);

    void* late = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "HOK_OnLateUpdate", 1);
    if (late) DobbyHook(late, (void*)hook_EspLate, (void**)&orig_EspLate);
}

// -----------------------------------------------------------------------------
// Host camp / host player id (read fresh from GamePlayerCenter each frame)
// -----------------------------------------------------------------------------
static inline void EspGetHost(int& hostCamp, uint32_t& hostPid) {
    hostCamp = -1; hostPid = 0;
    if (!esp_GpcManager) return;
    void* gpc = esp_GpcManager(nullptr);
    if (!gpc) return;
    uintptr_t g = (uintptr_t)gpc;
    hostPid = espRead<uint32_t>(g + ESP_OFF_GPC_HOSTPID, 0);
    uintptr_t hostPlayer = espRead<uintptr_t>(g + ESP_OFF_GPC_HOSTPLAYER, 0);
    if (espValidPtr(hostPlayer))
        hostCamp = espRead<int>(hostPlayer + ESP_OFF_PLAYER_CAMP, -1);
}

// -----------------------------------------------------------------------------
// Read an actor world position (curPosition → remotePosition → position field)
// -----------------------------------------------------------------------------
static inline bool EspReadPos(uintptr_t actor, EspVec3& pos) {
    uintptr_t mc = espRead<uintptr_t>(actor + ESP_OFF_MOVE_CONTROL, 0);
    if (espValidPtr(mc)) {
        pos.x = espRead<float>(mc + ESP_OFF_MOVE_CURPOS,     0.0f);
        pos.y = espRead<float>(mc + ESP_OFF_MOVE_CURPOS + 4, 0.0f);
        pos.z = espRead<float>(mc + ESP_OFF_MOVE_CURPOS + 8, 0.0f);
        if (pos.x != 0.0f || pos.z != 0.0f) return true;
        pos.x = espRead<float>(mc + ESP_OFF_MOVE_REMOTEPOS,     0.0f);
        pos.y = espRead<float>(mc + ESP_OFF_MOVE_REMOTEPOS + 4, 0.0f);
        pos.z = espRead<float>(mc + ESP_OFF_MOVE_REMOTEPOS + 8, 0.0f);
        if (pos.x != 0.0f || pos.z != 0.0f) return true;
    }
    pos.x = espRead<float>(actor + ESP_OFF_POSITION,     0.0f);
    pos.y = espRead<float>(actor + ESP_OFF_POSITION + 4, 0.0f);
    pos.z = espRead<float>(actor + ESP_OFF_POSITION + 8, 0.0f);
    return (pos.x != 0.0f || pos.z != 0.0f);
}

// World → screen (top-left origin pixels). Returns false if behind camera.
static inline bool EspW2S(void* cam, const EspVec3& world, float W, float H,
                          float& sx, float& sy, float& depth) {
    EspVec3 in = world, out = {0,0,0};
    esp_W2S(cam, &in, 2 /*Mono*/, &out);
    depth = out.z;
    if (out.z <= 0.0f) return false;          // behind camera
    sx = out.x;
    sy = H - out.y;                           // Unity screen origin is bottom-left
    if (sx < -W || sx > 2*W || sy < -H || sy > 2*H) return false;
    return true;
}

// -----------------------------------------------------------------------------
// MAIN DRAW – called from the eglSwapBuffers render loop in Main.cpp
// -----------------------------------------------------------------------------
static inline void DrawESP(int screenW, int screenH) {
    g_espActive = EspIsActive();
    if (!g_espActive)                return;
    if (!esp_CamGetMain || !esp_W2S) return;

    void* cam = esp_CamGetMain(nullptr);
    if (!cam) return;

    const float W = (float)screenW, H = (float)screenH;
    if (W <= 0 || H <= 0) return;

    int hostCamp; uint32_t hostPid;
    EspGetHost(hostCamp, hostPid);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 lineFrom(W * 0.5f, H);   // bottom-center

    g_espFrame++;

    // Snapshot the cache under the lock (prune stale entries too), then draw
    // without holding it so the game-thread hooks never stall on the render.
    std::vector<EspCachedActor> actors;
    {
        std::lock_guard<std::mutex> lk(g_espMutex);
        for (auto it = g_espCache.begin(); it != g_espCache.end();) {
            if (!espValidPtr(it->inst) || (g_espFrame - it->lastSeen) > 300)
                it = g_espCache.erase(it);
            else ++it;
        }
        actors = g_espCache;
    }

    // pass 1: find local hero world position (for distance feature)
    EspVec3 hostPos = {0,0,0}; bool haveHost = false;
    if (g_espDistance && hostPid) {
        for (auto& c : actors) {
            if (c.type != ESP_ACTOR_HERO || !esp_GetPid) continue;
            if (esp_GetPid((void*)c.inst) == hostPid) {
                if (EspReadPos(c.inst, hostPos)) haveHost = true;
                break;
            }
        }
    }

    // pass 2: draw
    for (auto& c : actors) {
        uintptr_t actor = c.inst;
        if (!espValidPtr(actor)) continue;

        int type = c.type;
        int camp = c.camp;

        bool isHero    = (type == ESP_ACTOR_HERO);
        bool isMonster = (type == ESP_ACTOR_MONSTER);
        if (!isHero && !isMonster) continue;        // skip organ/eye/bullet/...

        bool isLocal       = (isHero && hostPid && esp_GetPid && esp_GetPid((void*)actor) == hostPid);
        bool isTeammate    = (isHero && camp == hostCamp && !isLocal);
        bool isEnemyHero   = (isHero && camp != hostCamp);
        bool isFriendMinion= (isMonster && camp == hostCamp);
        bool isEnemyMinion = (isMonster && camp != hostCamp && camp != ESP_CAMP_MID);
        bool isJungle      = (isMonster && camp == ESP_CAMP_MID);

        if (isLocal)        continue;               // never draw self
        if (isFriendMinion) continue;               // skip allied minions
        if (isMonster) {
            if (isEnemyMinion && !g_espMinion)  continue;
            if (isJungle      && !g_espMonster) continue;
        }

        EspVec3 pos;
        if (!EspReadPos(actor, pos)) continue;

        float headH = isHero ? 2.7f : 1.5f;

        float fx, fy, fdepth, hx, hy, hdepth;
        if (!EspW2S(cam, pos, W, H, fx, fy, fdepth)) continue;                 // feet
        EspVec3 head = {pos.x, pos.y + headH, pos.z};
        if (!EspW2S(cam, head, W, H, hx, hy, hdepth)) { hx = fx; hy = fy - 60.0f; }

        float boxH = fabsf(fy - hy);
        if (boxH < 4.0f || boxH > H) continue;
        float boxW = boxH * 0.55f;

        // colour by allegiance
        ImU32 col;
        if (isEnemyHero || isEnemyMinion) col = IM_COL32(255, 60, 60, 255);   // enemy = red
        else if (isTeammate)              col = IM_COL32(60, 200, 255, 255);  // ally  = blue
        else if (isJungle)                col = IM_COL32(255, 200, 0, 255);   // jungle = yellow
        else                              col = IM_COL32(200, 200, 200, 255);

        float left   = fx - boxW * 0.5f;
        float right  = fx + boxW * 0.5f;
        float top    = hy;
        float bottom = fy;

        // ESP: Đường kẻ
        if (g_espLine)
            dl->AddLine(lineFrom, ImVec2(fx, fy), col, 1.5f);

        // ESP: Hộp
        if (g_espBox) {
            dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), col, 0.0f, 0, 2.0f);
        }

        // ESP: Thanh máu
        if (g_espHealth) {
            int   hp      = 0, hpMax = 1;
            uintptr_t vc = espRead<uintptr_t>(actor + ESP_OFF_VALUE_COMP, 0);
            if (espValidPtr(vc)) {
                hp    = espRead<int>(vc + ESP_OFF_VAL_HP, 0);
                hpMax = espRead<int>(vc + ESP_OFF_VAL_HPTOTAL, 1);
            }
            if (hpMax > 0 && hp >= 0) {
                float ratio = (float)hp / (float)hpMax;
                if (ratio < 0.0f) ratio = 0.0f;
                if (ratio > 1.0f) ratio = 1.0f;
                float barX = left - 6.0f;
                float barTop = top, barBot = bottom;
                ImU32 bg = IM_COL32(0, 0, 0, 180);
                dl->AddRectFilled(ImVec2(barX - 2, barTop - 1), ImVec2(barX + 2, barBot + 1), bg);
                ImU32 hpCol = IM_COL32((int)(255 * (1 - ratio)), (int)(255 * ratio), 40, 255);
                float fillTop = barBot - (barBot - barTop) * ratio;
                dl->AddRectFilled(ImVec2(barX - 1, fillTop), ImVec2(barX + 1, barBot), hpCol);
            }
        }

        // ESP: Khoảng cách
        if (g_espDistance) {
            float dist = fdepth;   // fallback: camera depth
            if (haveHost) {
                float dx = pos.x - hostPos.x, dz = pos.z - hostPos.z;
                dist = sqrtf(dx * dx + dz * dz);
            }
            char buf[24];
            snprintf(buf, sizeof(buf), "%.0fm", dist);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(fx - ts.x * 0.5f, fy + 2.0f), col, buf);
        }
    }
}
