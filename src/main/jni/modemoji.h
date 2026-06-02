#pragma once
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>

// ─── Feature toggles ─────────────────────────────────────────────────────────
bool SpamSticker_mine  = false;
bool SpamSticker_other = false;

// ─── Local actor reference (set from within the hook) ────────────────────────
static void* Lactor = nullptr;

// ─── Internal state ───────────────────────────────────────────────────────────
static std::vector<void*>                    insList;
static size_t                                currentIndex = 0;
static void*                                 lastLactor   = nullptr;
static std::chrono::steady_clock::time_point lastCall     = std::chrono::steady_clock::now();

// Battle emoji IDs for 4 spam steps – change to actual IDs for your version
static const uint32_t kBattleEmojiIDs[4] = { 1u, 2u, 3u, 4u };

// ─── Function pointers ───────────────────────────────────────────────────────
static bool  (*ActorLinker_IsHostPlayer)(void* ins)                    = nullptr;
static void* (*_GetBattleEmojiMgr)()                                  = nullptr;
static void  (*_SendPlayBattleEmojiCmd)(void* mgr, uint32_t emojiId)  = nullptr;
static void  (*_PushPlayEmoji)(uint32_t playerId, uint32_t emojiId)   = nullptr;
static void  (*_Emoji_Update)(void* ins, int del)                     = nullptr;

// ─── Helper: extract PlayerId from an EffectPlayComponent instance ───────────
//   ins (EffectPlayComponent)
//     └─ [0x20] LogicComponent.actor → ActorLinker*
//                  └─ [0x598] theMeta (ActorMeta, inline struct)
//                               └─ [0x10] PlayerId (uint32)
static uint32_t GetPlayerId(void* ins) {
    void* actor = *(void**)((uintptr_t)ins + 0x20);
    if (!actor) return 0;
    return *(uint32_t*)((uintptr_t)actor + 0x598 + 0x10);
}

// ─── Hook: EffectPlayComponent.LateUpdate(int delta) ─────────────────────────
void Emoji_Update(void* ins, int del) {
    // Track local actor: whichever EffectPlayComponent belongs to IsHostPlayer
    void* actorOfIns = *(void**)((uintptr_t)ins + 0x20);
    if (actorOfIns && ActorLinker_IsHostPlayer && ActorLinker_IsHostPlayer(actorOfIns))
        Lactor = actorOfIns;

    // Reset other-player list whenever the local actor changes
    if (Lactor != lastLactor) {
        insList.clear();
        currentIndex = 0;
        lastLactor = Lactor;
    }

    if ((SpamSticker_mine || SpamSticker_other) && Lactor != nullptr) {
        // Collect other actors' EffectPlayComponent instances (skip self)
        if (SpamSticker_other && actorOfIns != Lactor) {
            if (std::find(insList.begin(), insList.end(), ins) == insList.end())
                insList.push_back(ins);
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCall).count() >= 150) {
            void* mgr = _GetBattleEmojiMgr ? _GetBattleEmojiMgr() : nullptr;

            for (int s = 0; s < 4; s++) {
                // ── Self: CBattleEmojiManager Singleton ──────────────────────
                if (SpamSticker_mine && mgr && _SendPlayBattleEmojiCmd)
                    _SendPlayBattleEmojiCmd(mgr, kBattleEmojiIDs[s]);

                // ── Others: SGW.PushPlayEmoji (client-side trigger) ───────────
                if (SpamSticker_other && !insList.empty() && _PushPlayEmoji) {
                    uint32_t pid = GetPlayerId(insList[currentIndex]);
                    if (pid) _PushPlayEmoji(pid, kBattleEmojiIDs[s]);
                    currentIndex = (currentIndex + 1) % insList.size();
                }
            }
            lastCall = now;
        }
    }

    return _Emoji_Update(ins, del);
}
