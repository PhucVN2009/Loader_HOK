#pragma once
#include <cstdint>
#include <cstring>

// ─── Chế Độ Thử Thách Linh Bảo ───────────────────────────────────────────────
//
// Hook 1 – Lực chiến 9999999: ghi đè selfData.fight trước khi trận bắt đầu
//   CLingBaoBattleSys::ReqStartLingBaoLevel(LBMGStartGameData startParam)
//   @ Scripts.System.dll
//
// Hook 2 – Auto thắng: ép isWin = true trong kết quả trận
//   GameFinishProcesser::OnReceiveLingBaoSettleResult(LBMGEndGameData settleData)
//   @ Scripts.GameCore.dll
//
// Hook 3 – Tốc độ trận nhanh (auto-apply khi BattleStart)
//   LingBaoFightForm::BattleStart()   @ Scripts.GameCore.dll  |  0x5AA62B4
//   BattleCommonTools::SetGameSpeed(float) – static, giữ pointer để gọi trực tiếp
//   @ Scripts.GameCore.dll  |  0x59ED9E8
//
// ─── Field offsets ────────────────────────────────────────────────────────────
//
// LBMGStartGameData (class):
//   + 0x08 : selfData  (LBMGPersonData inline struct, klass ptr stripped)
//   selfData.fight = class + 0x08 + 0x18 = class + 0x20
//
// LBMGEndGameData (class):
//   + 0x08 : isSvrSuccess (bool)
//   + 0x09 : isWin        (bool)
//
// LingBaoFightForm (class):
//   + 0x5F0 : _playSpeed (float)
// ─────────────────────────────────────────────────────────────────────────────

bool lingbao_boost_fight = false;
bool lingbao_auto_win    = false;
int  lingbao_speed_mode  = 0;     // 0=normal, 1=x3, 2=x5

static const float LINGBAO_SPEEDS[] = { 1.0f, 3.0f, 5.0f };

// ─── SetGameSpeed function pointer (static method, no instance) ──────────────
typedef void (*fn_SetGameSpeed_t)(float speed, void* method_info);
static fn_SetGameSpeed_t fn_SetGameSpeed = nullptr;

// Gọi từ UI hoặc hook khi muốn thay đổi tốc độ
static inline void ApplyLingBaoSpeed(void* fightFormInstance) {
    float spd = LINGBAO_SPEEDS[lingbao_speed_mode];
    if (fn_SetGameSpeed)
        fn_SetGameSpeed(spd, nullptr);
    // Ghi trực tiếp _playSpeed vào form object
    if (fightFormInstance)
        *(float*)((uint8_t*)fightFormInstance + 0x5F0) = spd;
}

// ─── Hook 1: Lực chiến 9999999 ───────────────────────────────────────────────
static void (*_ReqStartLingBaoLevel)(void* instance, void* startParam, void* method_info);
static void new_ReqStartLingBaoLevel(void* instance, void* startParam, void* method_info) {
    if (!_ReqStartLingBaoLevel) return;
    if (lingbao_boost_fight && startParam != nullptr) {
        *(uint32_t*)((uint8_t*)startParam + 0x20) = 9999999u;  // selfData.fight
    }
    _ReqStartLingBaoLevel(instance, startParam, method_info);
}

// ─── Hook 2: Auto thắng ──────────────────────────────────────────────────────
static void (*_OnReceiveLingBaoSettleResult)(void* instance, void* settleData, void* method_info);
static void new_OnReceiveLingBaoSettleResult(void* instance, void* settleData, void* method_info) {
    if (!_OnReceiveLingBaoSettleResult) return;
    if (lingbao_auto_win && settleData != nullptr) {
        *(bool*)((uint8_t*)settleData + 0x08) = true;  // isSvrSuccess
        *(bool*)((uint8_t*)settleData + 0x09) = true;  // isWin
    }
    _OnReceiveLingBaoSettleResult(instance, settleData, method_info);
}

// ─── Hook 3: Tốc độ trận – tự động áp dụng khi BattleStart ─────────────────
// LingBaoFightForm::BattleStart() override, params = 0
static void (*_LBBattleStart)(void* instance, void* method_info);
static void new_LBBattleStart(void* instance, void* method_info) {
    if (_LBBattleStart) _LBBattleStart(instance, method_info);
    if (lingbao_speed_mode > 0)
        ApplyLingBaoSpeed(instance);
}
