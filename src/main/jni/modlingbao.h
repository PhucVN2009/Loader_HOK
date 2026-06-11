#pragma once
#include <cstdint>
#include <cstring>

// ─── Chế Độ Thử Thách Linh Bảo ───────────────────────────────────────────────
//
// Hook 1 – Max skill/equip level + fight = 9999999 trước khi vào trận
//   CLingBaoBattleSys::ReqStartLingBaoLevel(LBMGStartGameData startParam)
//   @ Scripts.System.dll  |  namespace: Assets.Scripts.GameSystem
//
// Hook 2 – Tốc độ trận: áp dụng khi BattleStart
//   LingBaoFightForm::BattleStart()  @ Scripts.GameCore.dll
//   BattleCommonTools::SetGameSpeed(float) static  @ Scripts.GameCore.dll
//
// Hook 3 – Auto thắng: set HP đối thủ = 0, gọi ForceFightOver(false, true, false)
//   LingBaoFightForm::UpdateBlood(selfBlood, selfBloodMax, enemyBlood, enemyBloodMax, selfShield, enemyShield)
//   SGW::ForceFightOver(isGm, isWin, isQuit) static  @ Scripts.Base.dll
//
// ─── Field/array offsets ─────────────────────────────────────────────────────
//
// LBMGStartGameData (class, System.Object):
//   + 0x20 : fight        (uint32)  — display score
//   + 0x28 : skill[]      (array*)
//   + 0x30 : equip[]      (array*)
//
// LBMGSkillData / LBMGEquipData inline (element stride = 0x20):
//   + 0x04 : level (uint32)  ← target
//
// IL2Cpp 1-D array header (64-bit):
//   + 0x10 : max_length (uint32)
//   + 0x18 : elements[0]
//
// LingBaoFightForm:
//   + 0x5F0 : _playSpeed (float)
// ─────────────────────────────────────────────────────────────────────────────

bool lingbao_boost_fight = false;
bool lingbao_auto_win    = false;
int  lingbao_speed_mode  = 0;   // 0=normal, 1=x3, 2=x5

// Prevents calling ForceFightOver more than once per battle
static bool g_force_win_called = false;

static const float LINGBAO_SPEEDS[] = { 1.0f, 3.0f, 5.0f };

// ─── SetGameSpeed pointer ─────────────────────────────────────────────────────
typedef void (*fn_SetGameSpeed_t)(float speed, void* method_info);
static fn_SetGameSpeed_t fn_SetGameSpeed = nullptr;

// ─── ForceFightOver(isGm, isWin, isQuit) static @ SGW ────────────────────────
typedef void (*fn_ForceFightOver_t)(bool isGm, bool isWin, bool isQuit, void* method_info);
static fn_ForceFightOver_t fn_ForceFightOver = nullptr;

static inline void ApplyLingBaoSpeed(void* fightFormInstance) {
    float spd = LINGBAO_SPEEDS[lingbao_speed_mode];
    if (fn_SetGameSpeed) fn_SetGameSpeed(spd, nullptr);
    if (fightFormInstance)
        *(float*)((uint8_t*)fightFormInstance + 0x5F0) = spd;
}

// ─── Helper: boost all levels in an IL2Cpp array of LBMGSkillData/EquipData ──
static inline void BoostLBMGLevelArray(void* arr_ptr, uint32_t max_level) {
    if (!arr_ptr) return;
    uint32_t len = *(uint32_t*)((uint8_t*)arr_ptr + 0x10);
    if (len == 0 || len > 64) return;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t* elem = (uint8_t*)arr_ptr + 0x18 + i * 0x20;
        uint32_t* level = (uint32_t*)(elem + 0x04);
        if (*level > 0)
            *level = max_level;
    }
}

// ─── Hook 1: Boost fight = 9999999 + max skill/equip levels ─────────────────
static void (*_ReqStartLingBaoLevel)(void* instance, void* startParam, void* method_info);
static void new_ReqStartLingBaoLevel(void* instance, void* startParam, void* method_info) {
    if (!_ReqStartLingBaoLevel) return;
    if (lingbao_boost_fight && startParam != nullptr) {
        *(uint32_t*)((uint8_t*)startParam + 0x20) = 9999999u;
        void* skill_arr = *(void**)((uint8_t*)startParam + 0x28);
        BoostLBMGLevelArray(skill_arr, 99u);
        void* equip_arr = *(void**)((uint8_t*)startParam + 0x30);
        BoostLBMGLevelArray(equip_arr, 99u);
    }
    _ReqStartLingBaoLevel(instance, startParam, method_info);
}

// ─── Hook 2: Tốc độ trận – auto-apply khi BattleStart ────────────────────────
static void (*_LBBattleStart)(void* instance, void* method_info);
static void new_LBBattleStart(void* instance, void* method_info) {
    g_force_win_called = false;  // reset for new battle
    if (_LBBattleStart) _LBBattleStart(instance, method_info);
    if (lingbao_speed_mode > 0)
        ApplyLingBaoSpeed(instance);
}

// ─── Hook 3: Auto thắng – set HP đối thủ = 0, gọi ForceFightOver ─────────────
// UpdateBlood(selfBlood, selfBloodMax, enemyBlood, enemyBloodMax, selfShield, enemyShield)
static void (*_UpdateBlood)(void* instance, int32_t selfBlood, int32_t selfBloodMax,
                            int32_t enemyBlood, int32_t enemyBloodMax,
                            int32_t selfShield, int32_t enemyShield, void* method_info);
static void new_UpdateBlood(void* instance, int32_t selfBlood, int32_t selfBloodMax,
                            int32_t enemyBlood, int32_t enemyBloodMax,
                            int32_t selfShield, int32_t enemyShield, void* method_info) {
    if (lingbao_auto_win) {
        enemyBlood = 0;
        if (enemyBloodMax <= 0) enemyBloodMax = 1;
        if (!g_force_win_called && fn_ForceFightOver) {
            g_force_win_called = true;
            fn_ForceFightOver(false, true, false, nullptr);
        }
    }
    if (_UpdateBlood)
        _UpdateBlood(instance, selfBlood, selfBloodMax, enemyBlood, enemyBloodMax, selfShield, enemyShield, method_info);
}
