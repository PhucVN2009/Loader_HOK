#pragma once
#include <cstdint>

bool maphack = false;

// =============================================================================
// Map Hack – native libGameCore.so hook only.
//
// GameGridFow::IsSurfaceCellVisibleConsiderNeighbor is the single choke-point
// for the Horizon fog-of-war system.  Forcing it to return true makes every
// surface cell appear visible, lifting the minimap fog completely.
//
// The function is resolved at runtime by two stable string references it
// contains (auto-update: survives game recompiles without a new offset).
// See ResolveGCFuncByString() in Main.cpp for the resolver logic.
// =============================================================================
static bool (*_GC_IsCellVisible)(void* thiz, void* a1, int a2, int a3) = nullptr;
static bool new_GC_IsCellVisible(void* thiz, void* a1, int a2, int a3) {
    if (maphack) return true;
    return _GC_IsCellVisible ? _GC_IsCellVisible(thiz, a1, a2, a3) : false;
}
