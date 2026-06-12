#pragma once
#include <cstdint>

// =============================================================================
// Map Hack – native libGameCore.so only.
//
// The real, correct map logic lives in the native engine (libGameCore.so).
// All il2cpp/Unity-side map code was removed: it caused desync ("fake match")
// and frozen out-of-sight actors. See Main.cpp:EnableNativeMapHack() for how the
// fog-reveal hook (IsCellVisible) and the anti-freeze patch are applied.
//
// new_GC_IsCellVisible is the hook callback: when Map Hack is ON it reports every
// surface cell as visible (reveals the fog); otherwise it is a passthrough.
// =============================================================================

bool maphack = false;

static bool (*_GC_IsCellVisible)(void* thiz, void* a1, int a2, int a3) = nullptr;
static bool new_GC_IsCellVisible(void* thiz, void* a1, int a2, int a3) {
    if (maphack) return true;
    return _GC_IsCellVisible ? _GC_IsCellVisible(thiz, a1, a2, a3) : false;
}
