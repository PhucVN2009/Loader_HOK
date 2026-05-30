#pragma once
#include <cstdint>

// =============================================================================
// Camera Zoom ("Cam xa") – widen the battle camera by overriding the camera's
// ZoomRateFromAge every frame from CameraSystem.Update().
//
//   CameraSystem.Update()              – Scripts.GameCore.dll, global namespace, 0 params
//   CameraSystem.set_ZoomRateFromAge(float)                                    1 param
// =============================================================================

static bool m_CameraZoom      = false;  // Camera Zoom on/off
static int  m_CameraZoomValue = 30;     // SeekBar value 0-100 → zoom 0.0f - 10.0f

typedef void (*set_ZoomRateFromAge_t)(void *instance, float value, const void *method);
static set_ZoomRateFromAge_t set_ZoomRateFromAge = nullptr;

static void (*orig_CameraUpdate)(void *instance, const void *method) = nullptr;
static void CameraUpdate_hook(void *instance, const void *method) {
    if (instance && m_CameraZoom && set_ZoomRateFromAge) {
        // SeekBar value 0-100 → zoom 0.0f - 10.0f
        float zoomVal = (float)m_CameraZoomValue / 10.0f;
        set_ZoomRateFromAge(instance, zoomVal, nullptr);
    }
    orig_CameraUpdate(instance, method);
}
