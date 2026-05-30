#pragma once
// =============================================================================
// Aimbot – override the skill aim direction toward the best enemy (with movement
// prediction). Ported from hok4, re-resolved BY NAME for this game version.
//
// Hook: SkillControlIndicator.GetUseSkillDirection() -> Vector3
//   indicator + 0x58 → SkillSlotLinker* (skillSlot)
//   SkillSlotLinker + 0x30 → SlotType (CoreDef.SkillSlotType: 1/2/3)
// Enemy data comes from the shared ESP actor cache (modesp.h).
// =============================================================================
#include "modesp.h"
#include <cmath>

static bool  m_aimEnabled = false;
static int   m_aimType    = 2;       // 0 = lowest HP, 1 = lowest HP%, 2 = closest
static float m_aimDist    = 130.0f;  // max range
static float m_aimBullet  = 12.0f;   // prediction "bullet speed" (lead amount)
static bool  m_aimSkill1  = true;
static bool  m_aimSkill2  = true;
static bool  m_aimSkill3  = true;

struct AimV3 { float x, y, z; };
static AimV3 (*_orig_GetUseSkillDir)(void* inst) = nullptr;

static inline bool aim_host_pos(float out[3]) {
    if (!g_hostPid) return false;
    std::lock_guard<std::mutex> lk(g_espMtx);
    for (auto& kv : g_espActors) {
        if (kv.second.pid == g_hostPid) {
            out[0]=kv.second.pos[0]; out[1]=kv.second.pos[1]; out[2]=kv.second.pos[2];
            return true;
        }
    }
    return false;
}

static inline bool aim_best_target(const float host[3], float outDir[3]) {
    float best = 1e30f; bool found = false; EspActor bt;
    {
        std::lock_guard<std::mutex> lk(g_espMtx);
        for (auto& kv : g_espActors) {
            EspActor& a = kv.second;
            if (a.type != ACTOR_TYPE_HERO) continue;
            if (g_hostCamp < 0 || a.camp == g_hostCamp) continue;  // enemies only
            if (a.maxHp <= 0 || a.hp <= 0) continue;
            float dx = a.pos[0]-host[0], dz = a.pos[2]-host[2];
            float dist = sqrtf(dx*dx + dz*dz);
            if (dist > m_aimDist) continue;
            float score = (m_aimType == 0) ? (float)a.hp
                        : (m_aimType == 1) ? (float)a.hp / a.maxHp
                                           : dist;
            if (score < best) { best = score; bt = a; found = true; }
        }
    }
    if (!found) return false;

    float tpos[3] = { bt.pos[0], bt.pos[1], bt.pos[2] };
    float dx = bt.pos[0]-host[0], dz = bt.pos[2]-host[2];
    float dist = sqrtf(dx*dx + dz*dz);
    if (m_aimBullet > 0.1f && bt.speed > 0.1f) {           // lead prediction
        float t = dist / m_aimBullet;
        if (t > 0 && t < 3.0f) { tpos[0]+=bt.fwd[0]*bt.speed*t; tpos[2]+=bt.fwd[2]*bt.speed*t; }
    }
    float ddx = tpos[0]-host[0], ddz = tpos[2]-host[2];
    float dd = sqrtf(ddx*ddx + ddz*ddz);
    if (dd < 0.01f) return false;
    outDir[0]=ddx/dd; outDir[1]=0; outDir[2]=ddz/dd;
    return true;
}

static AimV3 hook_GetUseSkillDir(void* inst) {
    if (m_aimEnabled && inst) {
        void* slot = *(void**)((uint64_t)inst + 0x58);          // skillSlot
        int   st   = slot ? *(int*)((uint64_t)slot + 0x30) : 0; // SlotType (1/2/3)
        bool  allow = (st==1 && m_aimSkill1) || (st==2 && m_aimSkill2) || (st==3 && m_aimSkill3);
        if (allow) {
            float host[3], dir[3];
            if (aim_host_pos(host) && aim_best_target(host, dir))
                return AimV3{ dir[0], dir[1], dir[2] };
        }
    }
    return _orig_GetUseSkillDir ? _orig_GetUseSkillDir(inst) : AimV3{0,0,1};
}
