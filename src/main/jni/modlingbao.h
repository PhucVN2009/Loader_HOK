#pragma once
#include <cstdint>
#include <cstring>

// ─── Chế Độ Thử Thách Linh Bảo ───────────────────────────────────────────────
//
// Hook 1 – Boost chiến lực (fight power) trước khi trận bắt đầu
//   CLingBaoBattleSys::ReqStartLingBaoLevel(LBMGStartGameData startParam)
//   @ Scripts.System.dll  |  0x8ED08C4
//
// Hook 2 – Auto thắng: ép isWin = true trong kết quả trận
//   GameFinishProcesser::OnReceiveLingBaoSettleResult(LBMGEndGameData settleData)
//   @ Scripts.GameCore.dll  |  0x63313A0
//
// LBMGStartGameData (class) layout:
//   + 0x08 : selfData  (LBMGPersonData struct, inline – no klass ptr)
//   + 0x40 : enemyData
//   + 0x78 : addStar
//
// LBMGPersonData inline layout (klass ptr stripped):
//   + 0x00 : uid        (String*)   [ orig dump 0x08 → inline 0x00 ]
//   + 0x08 : name       (String*)
//   + 0x10 : headUrl    (String*)
//   + 0x18 : fight      (uint32)    ← target field
//   + 0x20 : skill      (array*)
//   + 0x28 : equip      (array*)
//   + 0x30 : lingBaoPartIDs (array*)
//
//   selfData.fight  = startParam + 0x08 + 0x18 = startParam + 0x20
//   enemyData.fight = startParam + 0x40 + 0x18 = startParam + 0x58
//
// LBMGEndGameData (class) layout:
//   + 0x08 : isSvrSuccess  (bool)
//   + 0x09 : isWin         (bool)  ← target field
//   + 0x0C : finalGrade    (uint32)
//   + 0x10 : finalStar     (uint32)
//   + 0x14 : addStar       (uint32)
// ─────────────────────────────────────────────────────────────────────────────

bool lingbao_boost_fight = false;
bool lingbao_auto_win    = false;
int  lingbao_fight_mult  = 5;     // slider range 1–20

// ─── Hook 1: Boost chiến lực ─────────────────────────────────────────────────
static void (*_ReqStartLingBaoLevel)(void* instance, void* startParam);
static void new_ReqStartLingBaoLevel(void* instance, void* startParam) {
    if (!_ReqStartLingBaoLevel) return;
    if (lingbao_boost_fight && startParam != nullptr) {
        uint32_t* fight = (uint32_t*)((uint8_t*)startParam + 0x20);
        if (*fight > 0)
            *fight = *fight * (uint32_t)lingbao_fight_mult;
    }
    _ReqStartLingBaoLevel(instance, startParam);
}

// ─── Hook 2: Auto thắng ──────────────────────────────────────────────────────
static void (*_OnReceiveLingBaoSettleResult)(void* instance, void* settleData);
static void new_OnReceiveLingBaoSettleResult(void* instance, void* settleData) {
    if (!_OnReceiveLingBaoSettleResult) return;
    if (lingbao_auto_win && settleData != nullptr) {
        *(bool*)((uint8_t*)settleData + 0x08) = true;  // isSvrSuccess
        *(bool*)((uint8_t*)settleData + 0x09) = true;  // isWin
    }
    _OnReceiveLingBaoSettleResult(instance, settleData);
}
