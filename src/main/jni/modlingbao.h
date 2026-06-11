#pragma once
#include <cstdint>

// ─── Auto Win Linh Bảo ────────────────────────────────────────────────────────
//
// Hook – Ép HP đối thủ = 0 → gọi SGW::ForceFightOver(false, true, false)
//   LingBaoFightForm::UpdateBlood(selfBlood, selfBloodMax, enemyBlood,
//                                  enemyBloodMax, selfShield, enemyShield)
//   @ Scripts.GameCore.dll  |  0x5AA53A8
//
//   SGW::ForceFightOver(isGm, isWin, isQuit) static
//   @ Scripts.Base.dll  |  0x4853FF0
//
// ─────────────────────────────────────────────────────────────────────────────

bool lingbao_auto_win  = false;
static bool g_force_win_called = false;

// ─── ForceFightOver(isGm, isWin, isQuit) static ──────────────────────────────
typedef void (*fn_ForceFightOver_t)(bool isGm, bool isWin, bool isQuit, void* method_info);
static fn_ForceFightOver_t fn_ForceFightOver = nullptr;

// ─── UpdateBlood hook: ép enemyBlood=0, gọi ForceFightOver một lần ───────────
static void (*_UpdateBlood)(void* instance, int32_t selfBlood, int32_t selfBloodMax,
                            int32_t enemyBlood, int32_t enemyBloodMax,
                            int32_t selfShield, int32_t enemyShield, void* method_info);
static void new_UpdateBlood(void* instance, int32_t selfBlood, int32_t selfBloodMax,
                            int32_t enemyBlood, int32_t enemyBloodMax,
                            int32_t selfShield, int32_t enemyShield, void* method_info) {
    if (lingbao_auto_win) {
        enemyBlood  = 0;
        if (enemyBloodMax <= 0) enemyBloodMax = 1;
        if (!g_force_win_called && fn_ForceFightOver) {
            g_force_win_called = true;
            fn_ForceFightOver(false, true, false, nullptr);
        }
    }
    if (_UpdateBlood)
        _UpdateBlood(instance, selfBlood, selfBloodMax,
                     enemyBlood, enemyBloodMax, selfShield, enemyShield, method_info);
}
