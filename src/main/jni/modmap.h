#pragma once
#include <cstdint>

// =============================================================================
// Map Hack – libGameCore native, minimal 2-operation design
//
//   A) Hook IsCellVisible  → force the fog/vision query to report "visible".
//   B) Patch B (mov w21,w1 → mov w21,wzr) in the related vision setup.
//
// Both are AUTO-UPDATE: resolved at runtime by a unique code-signature scan
// (KittyScanner) instead of a hardcoded offset, so they survive game updates.
// Install/resolve happens in Main.cpp (hack_injec).
// =============================================================================

bool maphack = false;

// ── A) IsCellVisible ─────────────────────────────────────────────────────────
// Native signature (recovered from the dump prologue):
//   int64 IsCellVisible(void* thiz, int a1, int a2, int a3, void* a4)
// Forcing the return to 1 ("visible") clears the fog for every queried cell.
static int64_t (*_IsCellVisible)(void* thiz, int a1, int a2, int a3, void* a4) = nullptr;
static int64_t new_IsCellVisible(void* thiz, int a1, int a2, int a3, void* a4) {
    if (maphack) return 1;
    return _IsCellVisible ? _IsCellVisible(thiz, a1, a2, a3, a4) : 0;
}
