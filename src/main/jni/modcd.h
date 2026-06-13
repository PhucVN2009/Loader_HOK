#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"

// =============================================================================
// Show each hero's skill cooldowns in a draggable/resizable ImGui overlay tab.
// (Rewriting the in-game name via ChangeName/SetActorHudName did not display, so
//  we draw our own panel instead.)
//
//   ActorLinker.SkillControl @0x38  → SkillLinkerComponent
//   ActorLinker.name         @0x4F8 → System.String (display name)
//   SkillLinkerComponent.GetSkillSlot(SkillSlotType) → SkillSlotLinker
//   SkillSlotLinker.CurSkillCD @0x5C (ms)
//   ActorLinker.get_objType() == 0 → hero ; get_objCamp() → camp
// =============================================================================

bool  g_showCd  = false;
float g_cdScale = 1.2f;
static uint32_t g_cdTick = 0;   // bumped from the render loop

struct CdEntry {
    uint32_t id = 0;
    int      camp = 0;
    char     name[40] = {0};
    int      cd[6] = {0,0,0,0,0,0};
    int      ncd = 0;
    uint32_t stamp = 0;
};
static std::map<uint32_t, CdEntry> g_cdMap;
static std::mutex g_cdMtx;

static int  (*_cd_getObjType)(void* actor)  = nullptr;
static int  (*_cd_getObjCamp)(void* actor)  = nullptr;
static void*(*_cd_GetSkillSlot)(void* skillCtrl, int slotType) = nullptr;

static void (*_cd_origLateUpdate)(void* inst, int delta) = nullptr;
static void cd_LateUpdate(void* inst, int delta) {
    if (_cd_origLateUpdate) _cd_origLateUpdate(inst, delta);
    if (!g_showCd || !inst || !_cd_GetSkillSlot) return;
    if ((g_cdTick & 7) != 0) return;                          // ~every 8 frames
    if (_cd_getObjType && _cd_getObjType(inst) != 0) return;  // heroes only (0)

    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;
    void* skill = *(void**)((uint64_t)inst + 0x38);
    if (!skill) return;

    int cd[6]; int n = 0;
    for (int t = 1; t <= 6 && n < 6; t++) {
        void* slot = _cd_GetSkillSlot(skill, t);
        if (!slot) continue;
        int cur = *(int32_t*)((uint64_t)slot + 0x5C);         // CurSkillCD (ms)
        cd[n++] = (cur > 0) ? (cur + 999) / 1000 : 0;         // round up to seconds
    }
    if (n == 0) return;

    int camp = _cd_getObjCamp ? _cd_getObjCamp(inst) : 0;

    std::lock_guard<std::mutex> lk(g_cdMtx);
    CdEntry& e = g_cdMap[id];
    e.id = id; e.camp = camp; e.ncd = n;
    for (int i = 0; i < n; i++) e.cd[i] = cd[i];
    if (e.name[0] == 0) {                                     // read name once
        void* nameStr = *(void**)((uint64_t)inst + 0x4F8);
        if (nameStr) {
            const char* nm = ((String*)nameStr)->CString();
            if (nm) { strncpy(e.name, nm, sizeof(e.name)-1); e.name[sizeof(e.name)-1]=0; }
        }
    }
    e.stamp = g_cdTick;
}

// Draggable + resizable overlay window listing every hero's cooldowns.
static void DrawCdOverlay() {
    if (!g_showCd) return;
    ImGui::SetNextWindowSize(ImVec2(300, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 90), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("CD Ky Nang", &g_showCd)) {
        ImGui::SetWindowFontScale(g_cdScale);
        ImGui::SliderFloat("Co chu", &g_cdScale, 0.6f, 3.0f, "%.1f");
        ImGui::Separator();

        std::vector<CdEntry> snap;
        {
            std::lock_guard<std::mutex> lk(g_cdMtx);
            for (auto it = g_cdMap.begin(); it != g_cdMap.end(); ) {
                if (g_cdTick - it->second.stamp > 240) it = g_cdMap.erase(it);
                else { snap.push_back(it->second); ++it; }
            }
        }
        if (snap.empty()) ImGui::Text("...");
        for (auto& e : snap) {
            char line[96]; int p = 0;
            for (int i = 0; i < e.ncd; i++) p += snprintf(line+p, sizeof(line)-p, "[%d]", e.cd[i]);
            ImColor col = (e.camp == 2) ? ImColor(255, 90, 90) : ImColor(90, 180, 255);
            ImGui::TextColored(col, "%s %s", e.name[0] ? e.name : "?", line);
        }
    }
    ImGui::End();
}
