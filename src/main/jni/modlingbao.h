#pragma once
#include <cstdint>
#include <cstring>

// ─── Chế Độ Thử Thách Linh Bảo ───────────────────────────────────────────────
//
// Hook 1 – Max skill/equip level + fight = 9999999 trước khi vào trận
//   CLingBaoBattleSys::ReqStartLingBaoLevel(LBMGStartGameData startParam)
//   @ Scripts.System.dll  |  0x8ED08C4
//
// Hook 2 – Auto thắng: đặt WinLoseForm.m_win = true TRƯỚC khi StartLingBaoSettlement
//   WinLoseForm::StartLingBaoSettlement()  @ Scripts.GameCore.dll  |  0x64885A0
//   Hàm này CHỈ được gọi trong trận linh bảo → an toàn, không ảnh hưởng trận thường
//
// Hook 3 – Phần thưởng win: ép isWin = true + addStar >= 1 trong kết quả settle
//   GameFinishProcesser::OnReceiveLingBaoSettleResult(LBMGEndGameData settleData)
//   @ Scripts.GameCore.dll  |  0x63313A0
//
// Hook 4 – Tốc độ trận: áp dụng khi BattleStart
//   LingBaoFightForm::BattleStart()  @ Scripts.GameCore.dll  |  0x5AA62B4
//   BattleCommonTools::SetGameSpeed(float)  – static
//   @ Scripts.GameCore.dll  |  0x59ED9E8
//
// Hook 5 – Màn hình thắng/thua: ép bWin=true khi đang trong trận linh bảo
//   WinLose::ShowPanel(bWin, bJumpShowPanel, showHeroId)
//   @ Scripts.GameCore.dll  |  0x64B02D0
//
// ─── Field/array offsets ─────────────────────────────────────────────────────
//
// LBMGStartGameData (class, System.Object):
//   + 0x08 : selfData  (LBMGPersonData struct inline, klass ptr stripped)
//
// LBMGPersonData inline (subtract 0x8 from dump offsets):
//   + 0x18 : fight        (uint32)  → obj + 0x08 + 0x18 = obj + 0x20
//   + 0x20 : skill[]      (array*)  → obj + 0x28
//   + 0x28 : equip[]      (array*)  → obj + 0x30
//
// LBMGSkillData / LBMGEquipData inline (klass ptr stripped):
//   + 0x00 : id    (uint32)
//   + 0x04 : level (uint32)  ← target
//   + 0x08 : name  (String*)
//   ...
//   element size = 0x20 bytes
//
// IL2Cpp 1-D array header (64-bit):
//   + 0x00 : klass*      (8 bytes)
//   + 0x08 : bounds*     (8 bytes, null for 1-D)
//   + 0x10 : max_length  (uint32)
//   + 0x18 : elements[0]
//
// LBMGEndGameData (class):
//   + 0x08 : isSvrSuccess (bool)
//   + 0x09 : isWin        (bool)
//   + 0x14 : addStar      (uint32)
//
// WinLoseForm (class):
//   + 0x08 : m_win        (bool)  ← target for auto win
//
// LingBaoFightForm (class):
//   + 0x5F0 : _playSpeed  (float)
// ─────────────────────────────────────────────────────────────────────────────

bool lingbao_boost_fight = false;
bool lingbao_auto_win    = false;
int  lingbao_speed_mode  = 0;   // 0=normal, 1=x3, 2=x5

// Set true when inside a LingBao battle, cleared after settlement received
static bool g_in_lingbao_battle = false;

static const float LINGBAO_SPEEDS[] = { 1.0f, 3.0f, 5.0f };

// ─── SetGameSpeed pointer ─────────────────────────────────────────────────────
typedef void (*fn_SetGameSpeed_t)(float speed, void* method_info);
static fn_SetGameSpeed_t fn_SetGameSpeed = nullptr;

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
    if (len == 0 || len > 64) return;  // sanity check
    for (uint32_t i = 0; i < len; i++) {
        uint8_t* elem = (uint8_t*)arr_ptr + 0x18 + i * 0x20;
        uint32_t* level = (uint32_t*)(elem + 0x04);  // level field
        if (*level > 0)  // only boost if it has a level (id != 0 means it exists)
            *level = max_level;
    }
}

// ─── Hook 1: Boost fight = 9999999 + max skill/equip levels ─────────────────
static void (*_ReqStartLingBaoLevel)(void* instance, void* startParam, void* method_info);
static void new_ReqStartLingBaoLevel(void* instance, void* startParam, void* method_info) {
    if (!_ReqStartLingBaoLevel) return;
    g_in_lingbao_battle = true;  // mark entering a LingBao battle
    if (lingbao_boost_fight && startParam != nullptr) {
        // fight display score
        *(uint32_t*)((uint8_t*)startParam + 0x20) = 9999999u;

        // skill[] pointer: selfData + 0x20 (inline) = obj + 0x28
        void* skill_arr = *(void**)((uint8_t*)startParam + 0x28);
        BoostLBMGLevelArray(skill_arr, 99u);

        // equip[] pointer: selfData + 0x28 (inline) = obj + 0x30
        void* equip_arr = *(void**)((uint8_t*)startParam + 0x30);
        BoostLBMGLevelArray(equip_arr, 99u);
    }
    _ReqStartLingBaoLevel(instance, startParam, method_info);
}

// ─── Hook 2: Auto thắng – ép m_win trước StartLingBaoSettlement ──────────────
// WinLoseForm::StartLingBaoSettlement()  private, params=0
// Chỉ được gọi trong trận linh bảo → không ảnh hưởng trận PVP thường
static void (*_StartLingBaoSettlement)(void* instance, void* method_info);
static void new_StartLingBaoSettlement(void* instance, void* method_info) {
    if (!_StartLingBaoSettlement) return;
    if (lingbao_auto_win && instance != nullptr) {
        *(bool*)((uint8_t*)instance + 0x08) = true;  // m_win = true
    }
    _StartLingBaoSettlement(instance, method_info);
}

// ─── Hook 3: Ép isWin + addStar trong settle result (để nhận phần thưởng win) ──
static void (*_OnReceiveLingBaoSettleResult)(void* instance, void* settleData, void* method_info);
static void new_OnReceiveLingBaoSettleResult(void* instance, void* settleData, void* method_info) {
    if (!_OnReceiveLingBaoSettleResult) return;
    if (lingbao_auto_win && settleData != nullptr) {
        *(bool*)((uint8_t*)settleData + 0x08) = true;    // isSvrSuccess
        *(bool*)((uint8_t*)settleData + 0x09) = true;    // isWin
        uint32_t* addStar = (uint32_t*)((uint8_t*)settleData + 0x14);
        if (*addStar == 0) *addStar = 1u;                // addStar >= 1
    }
    _OnReceiveLingBaoSettleResult(instance, settleData, method_info);
    g_in_lingbao_battle = false;  // battle ended
}

// ─── Hook 4: Tốc độ trận – auto-apply khi BattleStart ────────────────────────
static void (*_LBBattleStart)(void* instance, void* method_info);
static void new_LBBattleStart(void* instance, void* method_info) {
    if (_LBBattleStart) _LBBattleStart(instance, method_info);
    if (lingbao_speed_mode > 0)
        ApplyLingBaoSpeed(instance);
}

// ─── Hook 5: WinLose::ShowPanel – hiển thị màn hình thắng khi auto win ───────
// Signature: void ShowPanel(bool bWin, bool bJumpShowPanel, uint32 showHeroId)
// @ Scripts.GameCore.dll | 0x64B02D0
static void (*_WinLoseShowPanel)(void* instance, bool bWin, bool bJumpShowPanel, uint32_t showHeroId, void* method_info);
static void new_WinLoseShowPanel(void* instance, bool bWin, bool bJumpShowPanel, uint32_t showHeroId, void* method_info) {
    if (!_WinLoseShowPanel) return;
    if (lingbao_auto_win && g_in_lingbao_battle) {
        bWin = true;
    }
    _WinLoseShowPanel(instance, bWin, bJumpShowPanel, showHeroId, method_info);
}
