#pragma once
#include <cstdint>
#include <cstdio>
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"

// =============================================================================
// Show skill cooldowns on the hero name (in-match), HOK version.
//
//   ActorLinker.SkillControl  @ 0x38   → SkillLinkerComponent
//   ActorLinker.HudControl    @ 0x428  → HudComponent3D
//   SkillLinkerComponent.skillSlotLinkerArray @ 0x28  (SkillSlotLinker[])
//   SkillSlotLinker.CurSkillCD @ 0x5C  (Int32, ms)
//   HudComponent3D.ChangeName(String)  → sets the displayed name
//   ActorLinker.get_objType()  → 0 = hero
//
// All methods resolved BY NAME (auto-update). Hooked on ActorLinker.HOK_OnLateUpdate
// which runs per actor per frame; throttled so we don't spam string allocations.
// =============================================================================

bool g_showCd = false;
static uint32_t g_cdTick = 0;   // bumped from the render loop

static void (*_cd_HudChangeName)(void* hud, void* nameStr) = nullptr;
static int  (*_cd_getObjType)(void* actor) = nullptr;

static void (*_cd_origLateUpdate)(void* inst, int delta) = nullptr;
static void cd_LateUpdate(void* inst, int delta) {
    if (_cd_origLateUpdate) _cd_origLateUpdate(inst, delta);
    if (!g_showCd || !inst || !_cd_HudChangeName) return;
    if ((g_cdTick & 7) != 0) return;                       // ~every 8 frames
    if (_cd_getObjType && _cd_getObjType(inst) != 0) return; // heroes only (0)

    void* skill = *(void**)((uint64_t)inst + 0x38);        // SkillControl
    void* hud   = *(void**)((uint64_t)inst + 0x428);       // HudControl
    if (!skill || !hud) return;

    void* arr = *(void**)((uint64_t)skill + 0x28);         // skillSlotLinkerArray
    if (!arr) return;
    int32_t len = *(int32_t*)((uint64_t)arr + 0x18);       // il2cpp array length
    if (len <= 0) return;
    void** data = (void**)((uint64_t)arr + 0x20);          // element pointers

    int cd[4] = {0, 0, 0, 0};
    int n = len < 4 ? len : 4;
    for (int i = 0; i < n; i++) {
        void* slot = data[i];
        if (!slot) continue;
        int v = *(int32_t*)((uint64_t)slot + 0x5C);        // CurSkillCD (ms)
        cd[i] = (v > 0) ? (v + 999) / 1000 : 0;            // round up to seconds
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "[%d][%d][%d][%d]", cd[0], cd[1], cd[2], cd[3]);
    void* s = (void*)String::Create(buf);
    if (s) _cd_HudChangeName(hud, s);
}
