#pragma once
#include <cstdint>

// ─── Auto Win Linh Bảo ────────────────────────────────────────────────────────
//
// Hook – Ép HP đối thủ = 0 → gọi SGW::ForceFightOver(false, true, false)
//   LingBaoFightForm::UpdateBlood(selfBlood, selfBloodMax, enemyBlood,
//                                  enemyBloodMax, selfShield, enemyShield)
//   @ Scripts.GameCore.dll  |  Assets.Scripts.GameSystem.LingBaoFightForm
//
//   SGW::ForceFightOver(isGm, isWin, isQuit) static
//   @ Scripts.Base.dll  |  class SGW
//
//   Both resolved BY NAME (auto-update across game versions).
// ─────────────────────────────────────────────────────────────────────────────

bool lingbao_auto_win   = false;   // feature on/off (driven by the floating button)
bool g_showAutoWinBtn   = false;   // show the floating toggle button (menu checkbox)
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
    } else {
        g_force_win_called = false;   // reset so it can fire again next time it's enabled
    }
    if (_UpdateBlood)
        _UpdateBlood(instance, selfBlood, selfBloodMax,
                     enemyBlood, enemyBloodMax, selfShield, enemyShield, method_info);
}
