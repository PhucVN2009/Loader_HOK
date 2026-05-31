#pragma once
// =============================================================================
//  ESP overlay – drawn with ImGui every frame.  Adapted to THIS ImGui source:
//  every game function is resolved by NAME via Il2CppGetMethodOffset (so it
//  survives game updates); field offsets re-checked against the 11.3.1.10 dump.
//
//  Menu (struct ESP):
//     Enable        – master switch
//     Line          – line from TOP of screen down to the actor
//     Box           – 2D box
//     Cooldown      – enemy hero skill cooldowns (1/2/3)
//     HP            – HP bar
//     Map           – radar (corner) and/or overlay on the game minimap
//     VisibleCheck  – only draw enemies the game currently marks visible
//     PlayerInfo    – hero name above the box
//     Alert         – top banner when enemy heroes are visible
//     HeroImage     – real hero portrait (best-effort) else marker+name
//     Minions       – include enemy minions + jungle monsters
//     Ultimate      – "R" ready/▢cd indicator for the enemy ultimate
// =============================================================================
#include <cstdint>
#include <cmath>
#include <vector>
#include <map>
#include <string>
#include <mutex>
#include <cstdio>
#include <dlfcn.h>
#include "imgui.h"
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"
#include "Dobby/dobby.h"

// -----------------------------------------------------------------------------
// Settings
// -----------------------------------------------------------------------------
struct ESPSettings {
    bool Enable       = false;
    bool Line         = false;
    bool Box          = false;
    bool Cooldown     = false;
    bool HP           = false;
    bool Map          = false;
    bool VisibleCheck = false;
    bool PlayerInfo   = false;
    bool Alert        = false;
    bool HeroImage    = false;
    bool Minions      = false;
    bool Ultimate     = false;

    int  MapMode      = 0;       // 0 = corner radar, 1 = overlay on game minimap

    // radar
    float RadarSize   = 170.0f;  // px radius
    float RadarScale  = 2.2f;    // px per world unit

    // minimap-overlay calibration (px + world bounds; tweak on device)
    float MmX = 24.0f, MmY = 24.0f, MmSize = 260.0f;
    float WorldMin = 0.0f, WorldMax = 140.0f;
};
static ESPSettings ESP;

static bool g_antiBan = true;    // Bật Anti-Ban (gates AnoSDK report bypass)

// modmap.h reads this to also reveal native fog while ESP is on.
bool g_espActive = false;

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
#define ESP_ACTOR_HERO     0
#define ESP_ACTOR_MONSTER  1
#define ESP_ACTOR_ORGAN    2
#define ESP_CAMP_MID       0

#define ESP_OFF_VALUE_COMP   0x400   // ActorLinker.ValueComponent
#define ESP_OFF_SKILL_CTRL   0x38    // ActorLinker.SkillControl (SkillLinkerComponent)
#define ESP_OFF_HEROCFG_ID   0x50    // ResHeroCfgInfo.dwCfgID  (resHeroCfgInfo@0x48 + 0x08)
#define ESP_OFF_MOVE_CONTROL 0x468   // ActorLinker.MoveControl
#define ESP_OFF_NAME         0x4F8   // ActorLinker.name (System.String)
#define ESP_OFF_POSITION     0x50C   // ActorLinker.position (Vector3)
#define ESP_OFF_LOGIC_VIS    0x559   // ActorLinker._logicVisible
#define ESP_OFF_MOVE_CURPOS  0x28    // MoveComponent.curPosition
#define ESP_OFF_MOVE_REMOTE  0x34    // MoveComponent.remotePosition
#define ESP_OFF_VAL_HP       0x38    // ValueLinkerComponent.actorHp
#define ESP_OFF_VAL_HPTOTAL  0x3C    // ValueLinkerComponent.actorHpTotal
#define ESP_OFF_GPC_HOSTPID  0x58    // GamePlayerCenter.hostPlayerID
#define ESP_OFF_GPC_HOSTPLY  0x60    // GamePlayerCenter._hostPlayer
#define ESP_OFF_PLAYER_CAMP  0x7C    // Player.playerCamp

// SkillLinkerComponent.skillSlotLinkerArray (SkillSlotLinker[]) @ 0x28
#define ESP_OFF_SKILL_ARRAY  0x28
// il2cpp reference-array: length @ 0x18, element[0] @ 0x20 (8 bytes each)
#define ESP_ARR_LEN          0x18
#define ESP_ARR_DATA         0x20
// SkillSlotLinker fields
#define ESP_OFF_SLOT_TYPE    0x30    // CoreDef.SkillSlotType SlotType
#define ESP_OFF_SLOT_CURCD   0x5C    // Int32 CurSkillCD  (remaining, ms)
#define ESP_OFF_SLOT_CDMAX   0x60    // Int32 CurSkillCDMax
#define ESP_OFF_SLOT_READY   0x74    // Boolean IsCDReady

// il2cpp System.String layout (64-bit)
#define ESP_STR_LEN   0x10
#define ESP_STR_CHARS 0x14

#define ESP_MAX_CACHE 1024

struct EspVec3 { float x, y, z; };

static inline bool EspIsActive() { return ESP.Enable; }

// -----------------------------------------------------------------------------
// Resolved function pointers
// -----------------------------------------------------------------------------
static void*    (*esp_CamGetMain)(void*)                              = nullptr;
static void     (*esp_W2S)(void*, EspVec3*, int, EspVec3*)            = nullptr;
static int      (*esp_GetCamp)(void*)                                 = nullptr;
static int      (*esp_GetType)(void*)                                 = nullptr;
static uint32_t (*esp_GetPid)(void*)                                  = nullptr;
static void*    (*esp_GpcManager)(void*)                              = nullptr;
// hero image pipeline (best-effort): GetBuiltinUISprite(path) -> Sprite -> Texture -> GL handle
static void*    (*esp_GetBuiltinSprite)(void*, void*)                 = nullptr;  // CFileManager.GetBuiltinUISprite(string)
static void*    (*esp_SpriteTex)(void*)                               = nullptr;  // Sprite.get_texture
static void*    (*esp_TexNativePtr)(void*)                            = nullptr;  // Texture.GetNativeTexturePtr
static void*    (*esp_StringNew)(const char*)                         = nullptr;  // il2cpp_string_new

// -----------------------------------------------------------------------------
// Actor cache
// -----------------------------------------------------------------------------
struct EspCachedActor { uintptr_t inst; int camp; int type; int lastSeen; };
static std::vector<EspCachedActor> g_espCache;
static std::mutex                  g_espMutex;
static int                         g_espFrame = 0;

// heroId -> ImGui texture id (0 = tried & failed/none)
static std::map<uint32_t, ImTextureID> g_heroTex;

// -----------------------------------------------------------------------------
// Safe memory helpers
// -----------------------------------------------------------------------------
static inline bool espValidPtr(uintptr_t a) { return a > 0x10000 && a < 0x7fffffffffffULL; }
template<typename T> static inline T espRead(uintptr_t a, T def) {
    if (!espValidPtr(a)) return def;
    return *(T*)a;
}
static inline std::string espStr(uintptr_t s) {
    if (!espValidPtr(s)) return "";
    int len = espRead<int>(s + ESP_STR_LEN, 0);
    if (len <= 0 || len > 64) return "";
    std::string out; out.reserve(len);
    for (int i = 0; i < len; ++i) {
        uint16_t c = espRead<uint16_t>(s + ESP_STR_CHARS + i * 2, 0);
        out += (c >= 32 && c < 127) ? (char)c : '?';
    }
    return out;
}

// -----------------------------------------------------------------------------
// Hooks: cache spawned actors / keep them live
// -----------------------------------------------------------------------------
static void (*orig_EspSpawn)(void*) = nullptr;
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
static void (*orig_EspLate)(void*, int) = nullptr;
static void hook_EspLate(void* inst, int delta) {
    if (inst) {
        uintptr_t a = (uintptr_t)inst;
        std::lock_guard<std::mutex> lk(g_espMutex);
        bool found = false;
        for (auto& c : g_espCache) if (c.inst == a) { c.lastSeen = g_espFrame; found = true; break; }
        if (!found && g_espCache.size() < ESP_MAX_CACHE) {
            int camp = esp_GetCamp ? esp_GetCamp(inst) : -1;
            int type = esp_GetType ? esp_GetType(inst) : -1;
            g_espCache.push_back({a, camp, type, g_espFrame});
        }
    }
    if (orig_EspLate) orig_EspLate(inst, delta);
}

// -----------------------------------------------------------------------------
// Install
// -----------------------------------------------------------------------------
static inline void EspInstallHook() {
    esp_CamGetMain = (void* (*)(void*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "get_main", 0);
    esp_W2S = (void (*)(void*, EspVec3*, int, EspVec3*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "WorldToScreenPoint_Injected", 3);

    esp_GetCamp = (int (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_objCamp", 0);
    esp_GetType = (int (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_objType", 0);
    esp_GetPid = (uint32_t (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_playerId", 0);
    esp_GpcManager = (void* (*)(void*))Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "GamePlayerCenter", "GetPlayerCenterManager", 0);

    // hero image pipeline (optional)
    esp_GetBuiltinSprite = (void* (*)(void*, void*))Il2CppGetMethodOffset(
        "Scripts.System.dll", "Assets.Scripts.UI", "CFileManager", "GetBuiltinUISprite", 1);
    esp_SpriteTex = (void* (*)(void*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Sprite", "get_texture", 0);
    esp_TexNativePtr = (void* (*)(void*))Il2CppGetMethodOffset(
        "UnityEngine.CoreModule.dll", "UnityEngine", "Texture", "GetNativeTexturePtr", 0);
    {
        void* h = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (h) { esp_StringNew = (void* (*)(const char*))dlsym(h, "il2cpp_string_new"); dlclose(h); }
    }

    void* spawn = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "HOK_OnSpawnActor", 0);
    if (spawn) DobbyHook(spawn, (void*)hook_EspSpawn, (void**)&orig_EspSpawn);
    void* late = Il2CppGetMethodOffset(
        "Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "HOK_OnLateUpdate", 1);
    if (late) DobbyHook(late, (void*)hook_EspLate, (void**)&orig_EspLate);
}

// -----------------------------------------------------------------------------
// Host info
// -----------------------------------------------------------------------------
static inline void EspGetHost(int& hostCamp, uint32_t& hostPid) {
    hostCamp = -1; hostPid = 0;
    if (!esp_GpcManager) return;
    void* gpc = esp_GpcManager(nullptr);
    if (!gpc) return;
    uintptr_t g = (uintptr_t)gpc;
    hostPid = espRead<uint32_t>(g + ESP_OFF_GPC_HOSTPID, 0);
    uintptr_t hp = espRead<uintptr_t>(g + ESP_OFF_GPC_HOSTPLY, 0);
    if (espValidPtr(hp)) hostCamp = espRead<int>(hp + ESP_OFF_PLAYER_CAMP, -1);
}

static inline bool EspReadPos(uintptr_t actor, EspVec3& pos) {
    uintptr_t mc = espRead<uintptr_t>(actor + ESP_OFF_MOVE_CONTROL, 0);
    if (espValidPtr(mc)) {
        pos.x = espRead<float>(mc + ESP_OFF_MOVE_CURPOS,     0.0f);
        pos.y = espRead<float>(mc + ESP_OFF_MOVE_CURPOS + 4, 0.0f);
        pos.z = espRead<float>(mc + ESP_OFF_MOVE_CURPOS + 8, 0.0f);
        if (pos.x != 0.0f || pos.z != 0.0f) return true;
        pos.x = espRead<float>(mc + ESP_OFF_MOVE_REMOTE,     0.0f);
        pos.y = espRead<float>(mc + ESP_OFF_MOVE_REMOTE + 4, 0.0f);
        pos.z = espRead<float>(mc + ESP_OFF_MOVE_REMOTE + 8, 0.0f);
        if (pos.x != 0.0f || pos.z != 0.0f) return true;
    }
    pos.x = espRead<float>(actor + ESP_OFF_POSITION,     0.0f);
    pos.y = espRead<float>(actor + ESP_OFF_POSITION + 4, 0.0f);
    pos.z = espRead<float>(actor + ESP_OFF_POSITION + 8, 0.0f);
    return (pos.x != 0.0f || pos.z != 0.0f);
}

static inline bool EspW2S(void* cam, const EspVec3& w, float W, float H,
                          float& sx, float& sy, float& depth) {
    EspVec3 in = w, out = {0,0,0};
    esp_W2S(cam, &in, 2, &out);
    depth = out.z;
    if (out.z <= 0.0f) return false;
    sx = out.x; sy = H - out.y;
    if (sx < -W || sx > 2*W || sy < -H || sy > 2*H) return false;
    return true;
}

// Remaining cooldown (ms) for a given SkillSlotType. -1 = not found.
// Walks SkillLinkerComponent.skillSlotLinkerArray and matches SlotType.
static inline int EspSkillCD(uintptr_t actor, int slotType) {
    uintptr_t sk = espRead<uintptr_t>(actor + ESP_OFF_SKILL_CTRL, 0);
    if (!espValidPtr(sk)) return -1;
    uintptr_t arr = espRead<uintptr_t>(sk + ESP_OFF_SKILL_ARRAY, 0);
    if (!espValidPtr(arr)) return -1;
    int len = (int)espRead<int64_t>(arr + ESP_ARR_LEN, 0);
    if (len <= 0 || len > 16) return -1;
    for (int i = 0; i < len; ++i) {
        uintptr_t slot = espRead<uintptr_t>(arr + ESP_ARR_DATA + i * 8, 0);
        if (!espValidPtr(slot)) continue;
        if (espRead<int>(slot + ESP_OFF_SLOT_TYPE, -1) != slotType) continue;
        if (espRead<bool>(slot + ESP_OFF_SLOT_READY, false)) return 0;
        int ms = espRead<int>(slot + ESP_OFF_SLOT_CURCD, 0);
        return ms < 0 ? 0 : ms;
    }
    return -1;
}

// Resource paths to try for a hero head/portrait sprite, %u = heroId.
// HoK builds vary; first non-null wins. Add/adjust on device if needed.
static const char* kEspHeroIconPaths[] = {
    "UI/UIIcon/HeroIcon/%u",
    "UGUI/Image/HeroIcon/%u",
    "Image/HeroHead/%u",
    "DataSource/Outpost/HeroIcon/%u",
};

// best-effort hero portrait -> ImGui texture id (cached per heroId, tried once).
// Uses CFileManager.GetBuiltinUISprite(string) which is null-safe: a wrong path
// just returns null (no crash) and we fall back to a coloured marker.
static inline ImTextureID EspHeroTexture(uint32_t heroId) {
    if (!heroId) return (ImTextureID)0;
    auto it = g_heroTex.find(heroId);
    if (it != g_heroTex.end()) return it->second;

    ImTextureID tex = (ImTextureID)0;
    if (esp_GetBuiltinSprite && esp_SpriteTex && esp_TexNativePtr && esp_StringNew) {
        for (const char* fmt : kEspHeroIconPaths) {
            char path[96];
            snprintf(path, sizeof(path), fmt, heroId);
            void* pStr = esp_StringNew(path);
            if (!pStr) continue;
            void* sprite = esp_GetBuiltinSprite(pStr, nullptr);
            if (!sprite) continue;
            void* t = esp_SpriteTex(sprite);
            if (!t) continue;
            void* gl = esp_TexNativePtr(t);
            if (gl) { tex = (ImTextureID)gl; break; }
        }
    }
    g_heroTex[heroId] = tex;   // cache result (even 0) so we try only once
    return tex;
}

// -----------------------------------------------------------------------------
// MAIN DRAW
// -----------------------------------------------------------------------------
static inline void DrawESP(int screenW, int screenH) {
    g_espActive = ESP.Enable;
    if (!ESP.Enable)               return;
    if (!esp_CamGetMain || !esp_W2S) return;
    void* cam = esp_CamGetMain(nullptr);
    if (!cam) return;

    const float W = (float)screenW, H = (float)screenH;
    if (W <= 0 || H <= 0) return;

    int hostCamp; uint32_t hostPid;
    EspGetHost(hostCamp, hostPid);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 lineTop(W * 0.5f, 0.0f);     // line now comes from the TOP

    g_espFrame++;

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

    // local hero world position (for distance / radar / alert)
    EspVec3 hostPos = {0,0,0}; bool haveHost = false;
    if (hostPid && esp_GetPid) {
        for (auto& c : actors) {
            if (c.type != ESP_ACTOR_HERO) continue;
            if (esp_GetPid((void*)c.inst) == hostPid) { haveHost = EspReadPos(c.inst, hostPos); break; }
        }
    }

    // radar frame (corner)
    ImVec2 radarC(W - ESP.RadarSize - 40.0f, ESP.RadarSize + 40.0f);
    if (ESP.Map && ESP.MapMode == 0) {
        dl->AddCircleFilled(radarC, ESP.RadarSize, IM_COL32(0, 0, 0, 110), 48);
        dl->AddCircle(radarC, ESP.RadarSize, IM_COL32(255, 255, 255, 90), 48, 1.5f);
        dl->AddCircleFilled(radarC, 4.0f, IM_COL32(255, 255, 255, 255)); // self
    }
    // minimap-overlay frame
    if (ESP.Map && ESP.MapMode == 1) {
        dl->AddRect(ImVec2(ESP.MmX, ESP.MmY), ImVec2(ESP.MmX + ESP.MmSize, ESP.MmY + ESP.MmSize),
                    IM_COL32(255, 255, 255, 70), 0, 0, 1.5f);
    }

    int enemyVisible = 0;

    for (auto& c : actors) {
        uintptr_t actor = c.inst;
        if (!espValidPtr(actor)) continue;

        int type = c.type, camp = c.camp;
        bool isHero = (type == ESP_ACTOR_HERO), isMonster = (type == ESP_ACTOR_MONSTER);
        if (!isHero && !isMonster) continue;

        bool isLocal     = (isHero && hostPid && esp_GetPid && esp_GetPid((void*)actor) == hostPid);
        bool isTeammate  = (isHero && camp == hostCamp && !isLocal);
        bool isEnemyHero = (isHero && camp != hostCamp);
        bool isFriendCreep = (isMonster && camp == hostCamp);
        bool isEnemyCreep  = (isMonster && camp != hostCamp && camp != ESP_CAMP_MID);

        if (isLocal)       continue;
        if (isFriendCreep) continue;
        if (isMonster && !ESP.Minions) continue;

        EspVec3 pos;
        if (!EspReadPos(actor, pos)) continue;

        bool gameVisible = espRead<bool>(actor + ESP_OFF_LOGIC_VIS, false);
        bool isEnemy = isEnemyHero || isEnemyCreep;

        // colour by allegiance / visibility
        ImU32 col;
        if (isEnemy)        col = gameVisible ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 130, 60, 255);
        else if (isTeammate)col = IM_COL32(60, 160, 255, 255);
        else                col = IM_COL32(255, 210, 0, 255); // jungle

        // ---- radar / minimap (drawn even when off-screen) ----
        if (ESP.Map && haveHost) {
            float dx = pos.x - hostPos.x, dz = pos.z - hostPos.z;
            if (ESP.MapMode == 0) {
                float px = radarC.x + dx * ESP.RadarScale;
                float py = radarC.y - dz * ESP.RadarScale;
                float ox = px - radarC.x, oy = py - radarC.y;
                float d = sqrtf(ox*ox + oy*oy);
                if (d > ESP.RadarSize) { float s = ESP.RadarSize / d; px = radarC.x + ox*s; py = radarC.y + oy*s; }
                dl->AddCircleFilled(ImVec2(px, py), 4.0f, col);
            } else {
                float u = (pos.x - ESP.WorldMin) / (ESP.WorldMax - ESP.WorldMin);
                float v = (pos.z - ESP.WorldMin) / (ESP.WorldMax - ESP.WorldMin);
                if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
                    float px = ESP.MmX + u * ESP.MmSize;
                    float py = ESP.MmY + (1.0f - v) * ESP.MmSize;
                    dl->AddCircleFilled(ImVec2(px, py), 4.0f, col);
                }
            }
        }

        // VisibleCheck: skip on-screen ESP for non-visible enemies
        if (ESP.VisibleCheck && isEnemy && !gameVisible) continue;
        if (isEnemyHero && gameVisible) enemyVisible++;

        // ---- world to screen ----
        float fx, fy, fdepth, hx, hy, hdepth;
        if (!EspW2S(cam, pos, W, H, fx, fy, fdepth)) continue;
        float headH = isHero ? 2.7f : 1.4f;
        EspVec3 head = {pos.x, pos.y + headH, pos.z};
        if (!EspW2S(cam, head, W, H, hx, hy, hdepth)) { hx = fx; hy = fy - 60.0f; }

        float boxH = fabsf(fy - hy);
        if (boxH < 4.0f || boxH > H) continue;
        float boxW = boxH * 0.55f;
        float left = fx - boxW * 0.5f, right = fx + boxW * 0.5f, top = hy, bottom = fy;

        if (ESP.Line) dl->AddLine(lineTop, ImVec2(fx, fy), col, 1.5f);
        if (ESP.Box)  dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), col, 0.0f, 0, 2.0f);

        if (ESP.HP) {
            int hp = 0, hpMax = 1;
            uintptr_t vc = espRead<uintptr_t>(actor + ESP_OFF_VALUE_COMP, 0);
            if (espValidPtr(vc)) {
                hp = espRead<int>(vc + ESP_OFF_VAL_HP, 0);
                hpMax = espRead<int>(vc + ESP_OFF_VAL_HPTOTAL, 1);
            }
            if (hpMax > 0 && hp >= 0) {
                float r = (float)hp / (float)hpMax;
                if (r < 0.0f) r = 0.0f;
                if (r > 1.0f) r = 1.0f;
                float bx = left - 6.0f;
                dl->AddRectFilled(ImVec2(bx - 2, top - 1), ImVec2(bx + 2, bottom + 1), IM_COL32(0,0,0,180));
                ImU32 hc = IM_COL32((int)(255*(1-r)), (int)(255*r), 40, 255);
                dl->AddRectFilled(ImVec2(bx - 1, bottom - (bottom-top)*r), ImVec2(bx + 1, bottom), hc);
            }
        }

        // hero-only extras
        if (isHero) {
            float textY = top - 4.0f;

            if (ESP.HeroImage) {
                uint32_t heroId = espRead<uint32_t>(actor + ESP_OFF_HEROCFG_ID, 0);
                ImTextureID tx = EspHeroTexture(heroId);
                if (tx) {
                    float s = boxH * 0.6f; if (s < 24) s = 24; if (s > 96) s = 96;
                    dl->AddImage(tx, ImVec2(fx - s*0.5f, top - s - 4.0f), ImVec2(fx + s*0.5f, top - 4.0f));
                    textY = top - s - 6.0f;
                } else {
                    dl->AddCircleFilled(ImVec2(fx, top - 10.0f), 6.0f, col); // marker fallback
                }
            }

            if (ESP.PlayerInfo) {
                uintptr_t nameStr = espRead<uintptr_t>(actor + ESP_OFF_NAME, 0);
                std::string nm = espStr(nameStr);
                if (nm.empty()) {
                    uint32_t hid = espRead<uint32_t>(actor + ESP_OFF_HEROCFG_ID, 0);
                    if (hid) { char b[24]; snprintf(b, sizeof(b), "Hero %u", hid); nm = b; }
                }
                if (!nm.empty()) {
                    ImVec2 ts = ImGui::CalcTextSize(nm.c_str());
                    dl->AddText(ImVec2(fx - ts.x*0.5f, textY - ts.y), col, nm.c_str());
                }
            }

            if (ESP.Cooldown || ESP.Ultimate) {
                char cd[48] = {0};
                if (ESP.Cooldown) {
                    int c1 = EspSkillCD(actor, 1), c2 = EspSkillCD(actor, 2), c3 = EspSkillCD(actor, 3);
                    snprintf(cd, sizeof(cd), "1:%.0f 2:%.0f 3:%.0f",
                             c1 > 0 ? c1/1000.0f : 0.0f, c2 > 0 ? c2/1000.0f : 0.0f, c3 > 0 ? c3/1000.0f : 0.0f);
                } else {
                    int c3 = EspSkillCD(actor, 3);
                    if (c3 <= 0) snprintf(cd, sizeof(cd), "R");
                    else         snprintf(cd, sizeof(cd), "R %.0f", c3/1000.0f);
                }
                ImU32 cc = (ESP.Ultimate && !ESP.Cooldown && EspSkillCD(actor,3) <= 0)
                           ? IM_COL32(60,255,80,255) : IM_COL32(255,255,255,255);
                dl->AddText(ImVec2(left, bottom + 2.0f), cc, cd);
            }
        }
    }

    // ---- Alert banner ----
    if (ESP.Alert && enemyVisible > 0) {
        char b[48]; snprintf(b, sizeof(b), "! %d ENEMY VISIBLE", enemyVisible);
        ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddRectFilled(ImVec2(W*0.5f - ts.x*0.5f - 8, 8), ImVec2(W*0.5f + ts.x*0.5f + 8, 12 + ts.y),
                          IM_COL32(180, 0, 0, 160));
        dl->AddText(ImVec2(W*0.5f - ts.x*0.5f, 10), IM_COL32(255,255,255,255), b);
    }
}
