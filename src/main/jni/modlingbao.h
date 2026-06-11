#pragma once
#include <cstdint>

// ─── LingBao Mods ────────────────────────────────────────────────────────────
//
// 1. Auto Win
//    Hook: LingBaoFightForm::UpdateBlood → ép enemyBlood=0, gọi ForceFightOver
//    @ Scripts.GameCore.dll | 0x5AA53A8
//    SGW::ForceFightOver(isGm, isWin, isQuit) @ Scripts.Base.dll | 0x4853FF0
//
// 2. Star Cap Bypass (hiển thị, không thay đổi server)
//    Hook: GameFinishProcesser::OnReceiveLingBaoSettleResult
//    → khi isWin=true & addStar=0 (đã 100 sao), ép addStar=1 để hiện thị +1 sao
//    @ Scripts.GameCore.dll | 0x63313A0
//    LBMGEndGameData: isSvrSuccess@0x8 isWin@0x9 finalGrade@0xC finalStar@0x10 addStar@0x14
//
// 3. ValidateStartParam bypass
//    Hook: CLingBaoBattleSys::ValidateStartParam → luôn trả về true
//    @ Scripts.System.dll | 0x8ED0AC0
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

// ─── OnReceiveLingBaoSettleResult hook: ép addStar≥1 khi thắng mà bị cap ────
static void (*_OnReceiveLingBaoSettleResult)(void* instance, void* settleData, void* method_info);
static void new_OnReceiveLingBaoSettleResult(void* instance, void* settleData, void* method_info) {
    if (settleData) {
        uint8_t  isWin   = *reinterpret_cast<uint8_t* >((uint8_t*)settleData + 0x9);
        uint32_t addStar = *reinterpret_cast<uint32_t*>((uint8_t*)settleData + 0x14);
        if (isWin && addStar == 0) {
            *reinterpret_cast<uint32_t*>((uint8_t*)settleData + 0x14) = 1;
        }
    }
    if (_OnReceiveLingBaoSettleResult)
        _OnReceiveLingBaoSettleResult(instance, settleData, method_info);
}

// ─── ValidateStartParam hook: luôn cho phép bắt đầu trận ────────────────────
static bool (*_ValidateStartParam)(void* instance, void* param, void* method_info);
static bool new_ValidateStartParam(void* instance, void* param, void* method_info) {
    return true;
}
