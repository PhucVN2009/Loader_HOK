// --- C Standard ---
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <math.h>
#include <limits>

// --- C++ Standard ---
#include <thread>
#include <vector>
#include <fstream>

// --- Android / System / Dynamic loading ---
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/system_properties.h>

// --- Graphics ---
#include <EGL/egl.h>
#include <GLES3/gl3.h>

// --- External libs / hooks / utilities ---
#include <xdl.h>
#include <SubstrateHook.h>
#include <CydiaSubstrate.h>

// --- KittyMemory (memory tools) ---
#include <KittyMemory/KittyMemory.h>
#include <KittyMemory/MemoryPatch.h>
#include <KittyMemory/KittyScanner.h>
#include <KittyMemory/KittyUtils.h>
#include <KittyUtils.h>

// --- ImGui ---
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

// --- Project / Local headers ---
#include "include/Tools.hpp"
#include "include/obfuscate.h"
#include "include/Theme.h"
#include "imgui/Font.h"
#include "imgui/Roboto-Regular.h"
#include "QQInj.h"
#include "modskin.h"
#include "modmap.h"
#include "modcam.h"
#include "modlingbao.h"
#include "imgui/Icon.h"
#include "imgui/Iconcpp.h"
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"
#include "AutoUpdate/Tools/Call_Tools.h"
#include "Zygisk.hpp"


inline static int g_GlHeight, g_GlWidth;
inline static bool g_IsSetup = false;
inline int prevWidth, prevHeight;

std::string GetProp(const char* key) {
  char value[PROP_VALUE_MAX];
  __system_property_get(key, value);
  return std::string(value);
}

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// Your Game Package Name(s). Supports both global builds.
char packageName[] = "com.levelinfinite.sgameGlobal.midaspay";
static const char* g_packages[] = {
    "com.levelinfinite.sgameGlobal",
    "com.levelinfinite.sgameGlobal.midaspay",
};
static bool is_target_package(const char* name) {
    if (!name) return false;
    for (auto p : g_packages) if (strcmp(name, p) == 0) return true;
    return false;
}


void hack();
void writeLog(const std::string& logMessage, const std::string& filename = "/storage/emulated/0/Android/data/com.waxmoon.ma.gp/files/log.txt");

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api_ = api;
        this->env_ = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *process = env_->GetStringUTFChars(args->nice_name, nullptr);

        is_game_ = is_target_package(process);

        env_->ReleaseStringUTFChars(args->nice_name, process);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (is_game_) {
            std::thread{hack}.detach();
        }
    }

private:
    Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool is_game_ = false;
};


uintptr_t il2cpp_base = 0;
void *getRealAddr(ulong offset) {
  return reinterpret_cast<void*>(il2cpp_base + offset);
};


void SetupImgui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplAndroid_Init(nullptr);
  ImGuiIO& io = ImGui::GetIO();

  SetYetAnotherDarkTheme(); //Theme

  ImGuiStyle *style = &ImGui::GetStyle();
  ImGui::GetStyle().WindowTitleAlign = ImVec2(0.5f, 0.5f);
  ImGui::GetStyle().FrameBorderSize = 1.5f;
  ImGui::GetStyle().ScrollbarSize = 50.0f;
  ImGui::GetStyle().GrabMinSize = 20.0f;
  ImGui::GetStyle().FrameRounding = 8;
  ImGui::GetStyle().ScrollbarRounding = 12;

  ImGui::GetStyle().PopupRounding = 3;
  ImGui::GetStyle().WindowPadding = ImVec2(4, 4);
  ImGui::GetStyle().FramePadding = ImVec2(2, 2);
  ImGui::GetStyle().ItemSpacing = ImVec2(3, 3);

  ImGui::GetStyle().WindowBorderSize = 4;
  ImGui::GetStyle().ChildBorderSize = 1;
  ImGui::GetStyle().PopupBorderSize = 1;

  ImGui::GetStyle().WindowRounding = 3;
  ImGui::GetStyle().ChildRounding = 3;
  ImGui::GetStyle().GrabRounding = 3;

  ImGui_ImplOpenGL3_Init("#version 100");
  io.IniFilename = nullptr;

  // ---------------- Fonts ----------------
  // 1. Main font: Roboto (supports Cyrillic)
  ImFontConfig font_cfg;
  font_cfg.SizePixels = 40.0f;
  static const ImWchar ranges[] = {
    0x0020,
    0x00FF,
    // Basic Latin + Latin Supplement
    0x0100,
    0x024F,
    // Extended Latin
    0x0400,
    0x052F,
    // Cyrillic
    0
  };
  ImFont* mainFont = io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, sizeof(Roboto_Regular), 40.0f, &font_cfg, ranges);

  // 2. Font Awesome icons (merged into main font, separate size)
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.OversampleH = 2;
  icons_config.OversampleV = 2;
  static const ImWchar icons_ranges[] = {
    ICON_MIN_FA,
    ICON_MAX_FA,
    0
  };
  io.Fonts->AddFontFromMemoryCompressedTTF(font_awesome_data, font_awesome_size, 40.0f, &icons_config, icons_ranges);

  // 3. Optional Custom font (merged)
  if (Custom != nullptr) {
    ImFontConfig custom_cfg;
    custom_cfg.MergeMode = true;
    custom_cfg.PixelSnapH = true;
    io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(Custom), sizeof(Custom), 40.0f, &custom_cfg, nullptr);
  }
}


bool clearMousePos = true;
bool ImGuiOK = false;


struct UnityEngine_Vector2_Fields {
  float x;
  float y;
};

struct UnityEngine_Vector2_o {
  UnityEngine_Vector2_Fields fields;
};

enum TouchPhase {
  Began = 0,
  Moved = 1,
  Stationary = 2,
  Ended = 3,
  Canceled = 4
};

struct UnityEngine_Touch_Fields {
  int32_t m_FingerId;
  struct UnityEngine_Vector2_o m_Position;
  struct UnityEngine_Vector2_o m_RawPosition;
  struct UnityEngine_Vector2_o m_PositionDelta;
  float m_TimeDelta;
  int32_t m_TapCount;
  int32_t m_Phase;
  int32_t m_Type;
  float m_Pressure;
  float m_maximumPossiblePressure;
  float m_Radius;
  float m_RadiusVariance;
  float m_AltitudeAngle;
  float m_AzimuthAngle;
};



struct Point {
  ImVec2 position;
  ImVec2 velocity;
  float radius;
};

static std::vector < Point > points;

float randomFloat(float min, float max) {
  return min + (rand() / (float)RAND_MAX) * (max - min);
}

void BackGroundDots(int numberOfDots) {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 windowSize = ImGui::GetIO().DisplaySize;

  static auto lastTime = std::chrono::high_resolution_clock::now();
  auto currentTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration < float > deltaTime = currentTime - lastTime;
  lastTime = currentTime;
  float t = std::chrono::duration < float > (currentTime.time_since_epoch()).count();

  points.erase(std::remove_if(points.begin(), points.end(), [&](const Point& p) {
    return (p.position.x < 0 - p.radius || p.position.x > windowSize.x + p.radius ||
      p.position.y < 0 - p.radius || p.position.y > windowSize.y + p.radius);
  }), points.end());

  while (points.size() < numberOfDots) {
    Point newPoint;
    newPoint.position.x = randomFloat(0, windowSize.x);
    newPoint.position.y = randomFloat(0, windowSize.y);
    newPoint.velocity.x = randomFloat(-30.0f, 30.0f);
    newPoint.velocity.y = randomFloat(-30.0f, 30.0f);
    newPoint.radius = randomFloat(2.0f, 4.0f);
    points.push_back(newPoint);
  }

  for (int i = 0; i < points.size(); ++i) {
    points[i].position.x += points[i].velocity.x * deltaTime.count();
    points[i].position.y += points[i].velocity.y * deltaTime.count();

    if (points[i].position.x < 0) {
      points[i].position.x = 0; points[i].velocity.x *= -1;
    }
    if (points[i].position.x > windowSize.x) {
      points[i].position.x = windowSize.x; points[i].velocity.x *= -1;
    }
    if (points[i].position.y < 0) {
      points[i].position.y = 0; points[i].velocity.y *= -1;
    }
    if (points[i].position.y > windowSize.y) {
      points[i].position.y = windowSize.y; points[i].velocity.y *= -1;
    }

    float r = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t + i));
    float g = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t + i + 2.0f));
    float b = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t + i + 4.0f));
    ImVec4 dotColor = ImVec4(r, g, b, 0.8f);

    float maxDist = 60.0f;
    for (int j = i + 1; j < points.size(); ++j) {
      float dx = points[i].position.x - points[j].position.x;
      float dy = points[i].position.y - points[j].position.y;
      float dist2 = dx*dx + dy*dy;
      if (dist2 < maxDist*maxDist) {
        float alpha = 2.0f - (sqrtf(dist2) / maxDist);
        ImVec4 lineColor = ImVec4(0.2f, 0.2f, 0.3f, alpha * 0.7f);
        draw_list->AddLine(points[i].position, points[j].position, ImGui::ColorConvertFloat4ToU32(lineColor), 1.0f);
      }
    }

    draw_list->AddCircleFilled(points[i].position, points[i].radius, ImGui::ColorConvertFloat4ToU32(dotColor));
  }
}


ImVec4 HSVtoRGB(float h, float s, float v) {
  float r, g, b;

  int i = int(h * 6.0f);
  float f = h * 6.0f - i;
  float p = v * (1.0f - s);
  float q = v * (1.0f - f * s);
  float t = v * (1.0f - (1.0f - f) * s);

  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
  }

  return ImVec4(r, g, b, 1.0f);
}

#include <ctime>
#include <cstdio>

time_t GetExpiryTimestamp(const char* expiry_date_str) {
    struct tm expiry_tm = {0};
    int day, month, year;
    if (sscanf(expiry_date_str, "%d-%d-%d", &day, &month, &year) != 3) {
        return 0;
    }
    expiry_tm.tm_mday = day;
    expiry_tm.tm_mon = month - 1;
    expiry_tm.tm_year = (year < 100) ? (year + 100) : year;
    expiry_tm.tm_hour = 0;
    expiry_tm.tm_min = 0;
    expiry_tm.tm_sec = 0;
    expiry_tm.tm_isdst = -1;
    return mktime(&expiry_tm);
}

// Global toggle
static bool g_ShowMenu = false;

void DrawLogo() {
  if (!ImGuiOK) return;
  float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
  ImVec4 rainbow = HSVtoRGB(hue, 1.0f, 1.0f);

  ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
  ImGuiWindowFlags_AlwaysAutoResize |
  ImGuiWindowFlags_NoTitleBar |
  ImGuiWindowFlags_NoBackground;

  ImGui::Begin("Logo", nullptr, flags);

  float size = 100.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, size * 0.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));

  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 rect = ImVec2(pos.x + size, pos.y + size);
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(pos, rect, IM_COL32(255, 255, 255, 60), size * 0.5f);

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.35f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));

  hue += ImGui::GetIO().DeltaTime * 0.3f;
  if (hue > 1.0f) hue -= 1.0f;

  ImVec4 textColor = ImColor::HSV(hue, 0.8f, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, textColor);

  ImGui::Button(ICON_FA_POWER_OFF, ImVec2(size, size));

  static bool dragging = false;

  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    dragging = true;
    ImVec2 delta = ImGui::GetIO().MouseDelta;
    ImVec2 wpos = ImGui::GetWindowPos();
    ImGui::SetWindowPos(ImVec2(wpos.x + delta.x, wpos.y + delta.y));
  }

  if (ImGui::IsItemDeactivated()) {
    if (!dragging && ImGui::IsItemHovered()) {
      g_ShowMenu = !g_ShowMenu;
    }
    dragging = false;
  }

  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  ImGui::End();
}

inline ImVec2 operator*(const ImVec2& v, float s) {
  return ImVec2(v.x * s, v.y * s);
}

// Floating, draggable on/off button for Auto Win Linh Bao.
// Red = off, green = on. Tap to toggle; drag to move.
void DrawAutoWinButton() {
  if (!ImGuiOK || !g_showAutoWinBtn) return;

  ImGui::SetNextWindowPos(ImVec2(120, 360), ImGuiCond_FirstUseEver);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoBackground;
  ImGui::Begin("AutoWinBtn", nullptr, flags);

  float size = 80.0f;
  ImVec4 onCol (0.10f, 0.80f, 0.25f, 1.0f);   // green = on
  ImVec4 offCol(0.85f, 0.15f, 0.15f, 1.0f);   // red   = off
  ImVec4 c = lingbao_auto_win ? onCol : offCol;

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, size * 0.5f);
  ImGui::PushStyleColor(ImGuiCol_Button,        c);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.x, c.y, c.z, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(c.x, c.y, c.z, 0.70f));
  ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));

  ImGui::Button(lingbao_auto_win ? "WIN\nON" : "WIN\nOFF", ImVec2(size, size));

  static bool dragging = false;
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    dragging = true;
    ImVec2 d = ImGui::GetIO().MouseDelta;
    ImVec2 wp = ImGui::GetWindowPos();
    ImGui::SetWindowPos(ImVec2(wp.x + d.x, wp.y + d.y));
  }
  if (ImGui::IsItemDeactivated()) {
    if (!dragging && ImGui::IsItemHovered())
      lingbao_auto_win = !lingbao_auto_win;
    dragging = false;
  }

  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(1);
  ImGui::End();
}


void DrawMenu() {
    static int activeFeature = 0;

    if (!g_ShowMenu) return;

    const ImVec2 window_size = ImVec2(600, 600);
    ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
    ImVec2 pos = ImVec2(center.x - window_size.x * 0.5f, center.y - window_size.y * 0.5f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("IMGUI MODMENU", nullptr, flags);

    float screenHeight = ImGui::GetIO().DisplaySize.y;
    if (screenHeight > 720)
        BackGroundDots(250);
    else
        BackGroundDots(125);

    ImGui::SetCursorPos(ImVec2(20, 15));
    float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
    ImVec4 rainbow = HSVtoRGB(hue, 1.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Separator, rainbow);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, rainbow);
    ImGui::TextColored(rainbow, ICON_FA_SUN " Hok Mod - By Telegram @userKeera");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float padding = 15.0f;
    float totalPadding = padding * 3;
    float buttonWidth = (window_size.x - totalPadding) / 2.0f;
    float buttonHeight = 65.0f;

    ImGui::SetCursorPosX(padding);
    ImGui::PushID(1);
    if (ImGui::Button(ICON_FA_HOME, ImVec2(buttonWidth, buttonHeight))) {
        activeFeature = 0;
    }
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::SetCursorPosX(padding * 2 + buttonWidth);
    ImGui::PushID(2);
    if (ImGui::Button(ICON_FA_DATABASE, ImVec2(buttonWidth, buttonHeight))) {
        activeFeature = 1;
    }
    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (activeFeature == 0) {

        // ── Map Hack ──────────────────────────────────────────────────────
        if (ImGui::Checkbox("Map Hack", &maphack)) {}

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Camera Zoom ───────────────────────────────────────────────────
        ImGui::Checkbox("Camera Zoom", &m_CameraZoom);
        if (m_CameraZoom) {
            ImGui::SliderInt("Zoom", &m_CameraZoomValue, 0, 100);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Auto Win Linh Bao Chien ────────────────────────────────────────
        ImGui::Checkbox("Auto Win Linh Bao Chien", &g_showAutoWinBtn);
        if (g_showAutoWinBtn)
            ImGui::TextColored(ImColor(180, 230, 255),
                "Nut tron noi: do = tat, xanh = bat. Keo de di chuyen.");
    }
    else if (activeFeature == 1) {
        ImGui::Columns(2, "deviceInfo", false);

        ImGui::TextColored(ImColor(255, 200, 0), "Device Name:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.device").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "Model:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.model").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "Manufacturer:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.manufacturer").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "Android Version:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.build.version.release").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "SDK Version:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.build.version.sdk").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "CPU ABI:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.cpu.abi").c_str());
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
}



inline EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
inline EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {

  eglQuerySurface(dpy, surface, EGL_WIDTH, &g_GlWidth);
  eglQuerySurface(dpy, surface, EGL_HEIGHT, &g_GlHeight);

  static bool should_clear_mouse_pos = false;

  if (!g_IsSetup) {
    prevWidth = g_GlWidth;
    prevHeight = g_GlHeight;
    SetupImgui();

    g_IsSetup = true;
  }
  ImGuiIO &io = ImGui::GetIO();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplAndroid_NewFrame(g_GlWidth, g_GlHeight);
  ImGui::NewFrame();
  if (ImGuiOK) {
    int (*TouchCount)(void*) = (int (*)(void*)) Il2CppGetMethodOffset("UnityEngine.dll", "UnityEngine", "Input", "get_touchCount", 0);
    int touchCount = TouchCount(nullptr);
    if (touchCount > 0) {
      UnityEngine_Touch_Fields touch = ((UnityEngine_Touch_Fields (*)(int)) Il2CppGetMethodOffset("UnityEngine.dll", "UnityEngine", "Input", "GetTouch", 1)) (0);
      float reverseY = io.DisplaySize.y - touch.m_Position.fields.y;

      switch (touch.m_Phase) {
        case TouchPhase::Began:
        case TouchPhase::Stationary:
        io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
        io.MouseDown[0] = true;
        break;
        case TouchPhase::Ended:
        case TouchPhase::Canceled:
        io.MouseDown[0] = false;
        should_clear_mouse_pos = true;
        break;
        case TouchPhase::Moved:
        io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
        break;
        default:
        break;
      }
    } else {
      io.MouseDown[0] = false;
    }
  }

  DrawLogo();
  DrawAutoWinButton();
  DrawMenu();

  ImGui::End();
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  ImGui::EndFrame();
  if (should_clear_mouse_pos) {
    io.MousePos = ImVec2(-1, -1);
    should_clear_mouse_pos = false;
  }

  return old_eglSwapBuffers(dpy, surface);
}


typedef unsigned long DWORD;
static uintptr_t libBase;

uintptr_t string2Offset(const char *c) {
  int base = 16;
  static_assert(sizeof(uintptr_t) == sizeof(unsigned long) || sizeof(uintptr_t) == sizeof(unsigned long long),
    "Please add string to handle conversion for this architecture.");

  if (sizeof(uintptr_t) == sizeof(unsigned long)) {
    return strtoul(c, nullptr, base);
  }

  return strtoull(c, nullptr, base);
}


inline void hack_injec();
inline void StartGUI() {
  void *ptr_eglSwapBuffer = DobbySymbolResolver("/system/lib/libEGL.so", "eglSwapBuffers");
  if (NULL != ptr_eglSwapBuffer) {
    DobbyHook((void *)ptr_eglSwapBuffer, (void*)hook_eglSwapBuffers, (void**)&old_eglSwapBuffers);
    LOGD("Gui Started");
    hack_injec();
  }
}

bool libLoaded = false;

DWORD findLibrary(const char *library) {
  char filename[0xFF] = {
    0
  },
  buffer[1024] = {
    0
  };
  FILE *fp = NULL;
  DWORD address = 0;

  sprintf(filename, OBFUSCATE("/proc/self/maps"));

  fp = fopen(filename, OBFUSCATE("rt"));
  if (fp == NULL) {
    perror(OBFUSCATE("fopen"));
    goto done;
  }

  while (fgets(buffer, sizeof(buffer), fp)) {
    if (strstr(buffer, library)) {
      address = (DWORD) strtoul(buffer, NULL, 16);
      goto done;
    }
  }

  done:

  if (fp) {
    fclose(fp);
  }

  return address;
}

DWORD getAbsoluteAddress(const char *libraryName, DWORD relativeAddr) {
  libBase = findLibrary(libraryName);
  if (libBase == 0)
  return 0;
  return (reinterpret_cast<DWORD > (libBase + relativeAddr));
}
ProcMap unityMap, anogsMap, il2cppMap;
using KittyScanner::RegisterNativeFn;

void hack() {
  LOGD("Inject Ok");
}
uintptr_t get_symbol_addr_in_pid(pid_t pid, const char* libname, uintptr_t offset_in_lib) {
  char maps_path[64];
  snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

  FILE* fp = fopen(maps_path, "r");
  if (!fp) return 0;

  char line[512];
  uintptr_t base = 0;

  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, libname)) {
      sscanf(line, "%lx-%*lx", &base);
      break;
    }
  }
  fclose(fp);

  if (base == 0) return 0;
  return base + offset_in_lib;
}

pid_t get_pid_by_name(const char* process_name) {
  DIR* proc_dir = opendir("/proc");
  if (!proc_dir) return -1;

  struct dirent* entry;
  while ((entry = readdir(proc_dir)) != NULL) {
    if (entry->d_type != DT_DIR) continue;

    pid_t pid = atoi(entry->d_name);
    if (pid <= 0) continue;

    char cmdline_path[256];
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);

    FILE* fp = fopen(cmdline_path, "r");
    if (!fp) continue;

    char cmdline[256];
    fgets(cmdline, sizeof(cmdline), fp);
    fclose(fp);

    if (strstr(cmdline, process_name)) {
      closedir(proc_dir);
      return pid;
    }
  }

  closedir(proc_dir);
  return -1;
}

// Find the game pid across any supported package name.
pid_t get_pid_by_packages() {
  for (auto p : g_packages) {
    pid_t pid = get_pid_by_name(p);
    if (pid != -1) return pid;
  }
  return -1;
}

void writeLog(const std::string& logMessage, const std::string& filename) {
  std::ofstream outFile(filename, std::ios::app);
  if (outFile.is_open()) {
    outFile << logMessage << std::endl;
    outFile.close();
  } else {
    //std::cerr << "Log file log: << filename << std::endl;
  }
}

bool is_current_process(const char* target_name) {
  char cmdline_path[64];
  snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", getpid());

  FILE* fp = fopen(cmdline_path, "r");
  if (!fp) return false;

  char cmdline[256] = {
    0
  };
  fgets(cmdline, sizeof(cmdline), fp);
  fclose(fp);

  return strcmp(cmdline, target_name) == 0;
}



// ── AnoSDK anti-cheat bypass ─────────────────────────────────────────────────
// Intercept GetReportData variants so the SDK has nothing to upload.
static int32_t (*_AnoSDKGetReportData)(char* buf, int32_t len)  = nullptr;
static int32_t (*_AnoSDKGetReportData3)(char* buf, int32_t len) = nullptr;
static int32_t (*_AnoSDKGetReportData4)(char* buf, int32_t len) = nullptr;
static int32_t new_AnoSDKGetReportData(char* buf, int32_t len)  { return 0; }
static int32_t new_AnoSDKGetReportData3(char* buf, int32_t len) { return 0; }
static int32_t new_AnoSDKGetReportData4(char* buf, int32_t len) { return 0; }

// ─────────────────────────────────────────────────────────────────────────────
// Auto-resolve a libGameCore.so function by an internal string it references.
//   1. find the string in the module,
//   2. scan exec segments for the ADRP+ADD pair that loads that string,
//   3. walk back to the function prologue (sub sp, sp, #imm).
// Survives game updates (string identifiers are stable) — no manual offset.
// ─────────────────────────────────────────────────────────────────────────────
static uintptr_t ResolveGCFuncByString(const char* lib, const char* str) {
  auto maps = KittyMemory::getMapsByName(lib);
  if (maps.empty()) return 0;
  size_t slen = strlen(str);

  // 1. locate the string
  uintptr_t strAddr = 0;
  for (auto& m : maps) {
    if (!m.readable) continue;
    uintptr_t r = KittyScanner::findDataFirst(m.startAddress, m.endAddress, str, slen);
    if (r) { strAddr = r; break; }
  }
  if (!strAddr) return 0;

  // 2. scan executable segments for ADRP(reg)+ADD(reg,#imm) == strAddr
  uintptr_t lastPage[32]; bool has[32];
  for (auto& m : maps) {
    if (!m.executable) continue;
    for (int i = 0; i < 32; i++) has[i] = false;
    for (uintptr_t a = m.startAddress; a + 4 <= m.endAddress; a += 4) {
      uint32_t w = *(uint32_t*)a;
      if ((w & 0x9F000000) == 0x90000000) {            // ADRP Xd, imm
        int rd = w & 0x1f;
        int64_t imm = (((w >> 5) & 0x7ffff) << 2) | ((w >> 29) & 3);
        if (imm & (1 << 20)) imm -= (1 << 21);          // sign-extend 21-bit
        lastPage[rd] = (a & ~0xfffULL) + (imm << 12);
        has[rd] = true;
      } else if ((w & 0xFF800000) == 0x91000000) {      // ADD Xd, Xn, #imm12
        int rn = (w >> 5) & 0x1f, imm12 = (w >> 10) & 0xfff;
        if (has[rn] && (lastPage[rn] + imm12) == strAddr) {
          // 3. walk back to prologue: sub sp, sp, #imm
          for (uintptr_t b = a; b > a - 0x1200 && b >= m.startAddress; b -= 4) {
            uint32_t pw = *(uint32_t*)b;
            if ((pw & 0xFFC003FF) == 0xD10003FF) return b;  // sub sp, sp, #imm
          }
        }
      }
    }
  }
  return 0;
}

// Anti-freeze patch for out-of-sight actors. In the native camp-visibility
// function, the instruction `mov w21, w1` (F5 03 01 2A) feeds the camp-visibility
// flag into the cull decision; forcing it to `mov w21, wzr` (F5 03 FF 2A) makes
// OOS actors keep updating so they don't freeze. Located by a unique AOB so it
// auto-updates across game versions (no manual offset).
static bool ApplyAntiFreezePatch() {
  auto maps = KittyMemory::getMapsByName("libGameCore.so");
  // f403022a 76aa42b9 f503012a 080040f9  (mov w20,w2; ldr w22,[x19,#0x2a8]; mov w21,w1; ldr x8,[x0])
  std::string hex  = "F4 03 02 2A 76 AA 42 B9 F5 03 01 2A 08 00 40 F9";
  std::string mask = "xxxxxxxxxxxxxxxx";
  for (auto& m : maps) {
    if (!m.executable) continue;
    uintptr_t hit = KittyScanner::findHexFirst(m.startAddress, m.endAddress, hex, mask);
    if (hit) {
      uintptr_t patchAddr = hit + 8;          // the `mov w21, w1`
      uint32_t insn = 0x2AFF03F5;             // mov w21, wzr  → bytes F5 03 FF 2A
      KittyMemory::setAddressProtection((void*)patchAddr, 4, PROT_READ | PROT_WRITE | PROT_EXEC);
      bool ok = KittyMemory::memWrite((void*)patchAddr, &insn, 4);
      KittyMemory::setAddressProtection((void*)patchAddr, 4, PROT_READ | PROT_EXEC);
      LOGD("anti-freeze patch %s @ %p", ok ? "applied" : "FAILED", (void*)patchAddr);
      return ok;
    }
  }
  LOGD("anti-freeze patch: pattern not found");
  return false;
}

void hack_injec() {
  while (!unityMap.isValid()) {
    unityMap = KittyMemory::getLibraryBaseMap("libunity.so");
    il2cppMap = KittyMemory::getLibraryBaseMap("libil2cpp.so");
    sleep(5);
  }
  sleep(5);
  Il2CppAttach("libil2cpp.so");

  // ── Unlock Skin hooks ────────────────────────────────────────────────────
  void* skAddr;
  skAddr = Il2CppGetMethodOffset("Scripts.Plugins.dll", "CSProtocol", "COMDT_HERO_COMMON_INFO", "unpack", 2);
  if (skAddr) DobbyHook(skAddr, (void*)new_unpack, (void**)&_unpack);

  skAddr = Il2CppGetMethodOffset("Scripts.Base.dll", "Assets.Scripts.GameSystem", "CRoleInfo", "IsCanUseSkin", 3);
  if (skAddr) DobbyHook(skAddr, (void*)new_IsCanUseSkin, (void**)&_IsCanUseSkin);

  skAddr = Il2CppGetMethodOffset("Scripts.Base.dll", "Assets.Scripts.GameSystem", "CRoleInfo", "IsHaveHeroSkin", 4);
  if (skAddr) DobbyHook(skAddr, (void*)new_IsHaveHeroSkin, (void**)&_IsHaveHeroSkin);

  skAddr = Il2CppGetMethodOffset("Scripts.System.dll", "Assets.Scripts.GameSystem", "CSelectHeroFormLogic", "GetHeroWearSkinId", 1);
  if (skAddr) DobbyHook(skAddr, (void*)new_GetHeroWearSkinId, (void**)&_GetHeroWearSkinId);

  skAddr = Il2CppGetMethodOffset("Scripts.System.dll", "Assets.Scripts.GameSystem", "CSelectHeroFormLogic", "WearHeroSkin", 2);
  if (skAddr) DobbyHook(skAddr, (void*)new_WearHeroSkin, (void**)&_WearHeroSkin);

  // ── Map Hack hooks (FogOfWar – Scripts.GameCore.dll, global namespace) ────
  void* mapAddr;
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "FogOfWar", "IsEnable", 0);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_FowIsEnable, (void**)&_FowIsEnable);

  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "FogOfWar", "get_enable", 0);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_FowGetEnable, (void**)&_FowGetEnable);

  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "FogOfWar", "get_EnableRender", 0);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_FowGetEnableRender, (void**)&_FowGetEnableRender);

  // ActorLinker::SetVisible + ForceSetVisible – intercept server hide packets
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "SetVisible", 2);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_ActorSetVisible, (void**)&_ActorSetVisible);

  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "ForceSetVisible", 2);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_ActorForceSetVisible, (void**)&_ActorForceSetVisible);

  // SGC::CheckVisible – all visibility queries return true
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "SGC", "CheckVisible", 3);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_CheckVisible, (void**)&_CheckVisible);

  // ── Layer 4: position sync for out-of-sight actors ───────────────────────
  // 4a: cache real position/direction from every movement packet we see
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "SGC", "NtfActorMovementData", 1);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_NtfActorMovementData, (void**)&_NtfActorMovementData);

  // 4b: cache isMoving flag
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "SGC", "NtfActorMoveState", 2);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_NtfActorMoveState, (void**)&_NtfActorMoveState);

  // 4c/4d DISABLED: the il2cpp Interpolation / HOK_OnInterpolation hooks ran
  // sync_oos_transform, which wrote the frozen ActorLinker.position (0x50C) onto
  // the transform every frame — that is what made out-of-sight enemies freeze.
  // The native anti-freeze patch (ApplyAntiFreezePatch) handles OOS movement at
  // the engine level instead, so these il2cpp position writes are removed.
  // mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "Interpolation", 0);
  // if (mapAddr) DobbyHook(mapAddr, (void*)new_Interpolation, (void**)&_Interpolation);
  // mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "HOK_OnInterpolation", 0);
  // if (mapAddr) DobbyHook(mapAddr, (void*)new_HOKOnInterpolation, (void**)&_HOKOnInterpolation);

  // Transform write helper: UnityEngine.Transform::set_position_Injected(ref Vector3)
  {
    void* tp = Il2CppGetMethodOffset("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "set_position_Injected", 1);
    if (tp) _TransformSetPosInj = (void (*)(void*, float*))tp;
  }

  // ── Layer 5: HP sync for OOS actors ─────────────────────────────────────
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "SGC", "OnActorCurHpChange", 3);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_OnActorCurHpChange, (void**)&_OnActorCurHpChange);

  {
    void* fn = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic",
                                      "ValueLinkerComponent", "SetActorHp", 2);
    if (fn) _SetActorHp = (void (*)(void*, int32_t, int32_t))fn;
  }

  // ── Layer 6: keep HP/buff callbacks alive (skip UnregisterEvt) ───────────
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "SGC", "OnActorLeaveView_UnregisterEvt", 1);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_OnActorLeaveViewUnregEvt, (void**)&_OnActorLeaveViewUnregEvt);

  // ── Layer 7: skip ActorManager::OnActorLeaveView (instance method) ──────
  // SGC::OnActorLeaveView is left to run normally so actor.SetVisible(false)
  // still fires → Layer-2 hook captures it → g_oosSet populated for Layers 4c/5.
  // Only the inner ActorManager::OnActorLeaveView is suppressed so the actor
  // stays in HeroActors/SoldierActors/... → ActorManager::Interpolation()
  // still iterates it every frame → Layer 4c fires → positions sync.
  mapAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorManager", "OnActorLeaveView", 2);
  if (mapAddr) DobbyHook(mapAddr, (void*)new_ActorMgrLeaveView, (void**)&_ActorMgrLeaveView);

  // ── NATIVE libGameCore.so: Horizon fog – GameGridFow::IsSurfaceCellVisibleConsiderNeighbor ──
  // AUTO-UPDATE: resolve the function at runtime by the internal string it
  // references, so it survives game updates (no manual offset). Falls back to the
  // known static offset for this build if the scan fails.
  {
    ProcMap gcMap = KittyMemory::getLibraryBaseMap("libGameCore.so");
    for (int i = 0; i < 30 && !gcMap.isValid(); i++) { sleep(1); gcMap = KittyMemory::getLibraryBaseMap("libGameCore.so"); }
    if (gcMap.isValid()) {
      // (1) Fog reveal: hook the CORRECT IsCellVisible function. Hooking the wrong
      // cell function (e.g. IsSurfaceCellVisibleConsiderNeighbor) desyncs the client
      // into a 'fake match'. This one has no identifying string, so locate it by a
      // unique masked AOB of its prologue (only the relative bl is wildcarded, so it
      // tolerates the function shifting across game updates).
      //   02000014 01000014 f30300aa e0030091 <bl> 0c000014 f30300aa e0030191
      std::string cvHex  = "0200001401000014f30300aae0030091000000000c000014f30300aae0030191";
      std::string cvMask = "xxxxxxxxxxxxxxxx????xxxxxxxxxxxx";
      uintptr_t fn = 0;
      {
        auto maps = KittyMemory::getMapsByName("libGameCore.so");
        for (auto& m : maps) {
          if (!m.executable) continue;
          fn = KittyScanner::findHexFirst(m.startAddress, m.endAddress, cvHex, cvMask);
          if (fn) break;
        }
      }
      if (fn) {
        DobbyHook((void*)fn, (void*)new_GC_IsCellVisible, (void**)&_GC_IsCellVisible);
        LOGD("libGameCore.so base=%p, IsCellVisible hooked @ %p", (void*)gcMap.startAddress, (void*)fn);
      } else {
        LOGD("libGameCore.so: IsCellVisible AOB not found - fog reveal skipped");
      }
      // (2) Anti-freeze: native patch so OOS actors keep moving (the missing piece).
      ApplyAntiFreezePatch();
    } else {
      LOGD("libGameCore.so not found - native fow hook skipped");
    }
  }

  // ── Camera Zoom hooks (CameraSystem – Scripts.GameCore.dll, global namespace) ──
  {
    void* up = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "CameraSystem", "Update", 0);
    if (up) DobbyHook(up, (void*)CameraUpdate_hook, (void**)&orig_CameraUpdate);
    void* sz = Il2CppGetMethodOffset("Scripts.GameCore.dll", "", "CameraSystem", "set_ZoomRateFromAge", 1);
    if (sz) set_ZoomRateFromAge = (set_ZoomRateFromAge_t)sz;
  }

  // ── Auto Win Linh Bao (resolve by name) ──────────────────────────────────
  {
    void* ff = Il2CppGetMethodOffset("Scripts.Base.dll", "", "SGW", "ForceFightOver", 3);
    if (ff) fn_ForceFightOver = (fn_ForceFightOver_t)ff;
    void* ub = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameSystem",
                                     "LingBaoFightForm", "UpdateBlood", 6);
    if (ub) DobbyHook(ub, (void*)new_UpdateBlood, (void**)&_UpdateBlood);
  }

  // ── AnoSDK bypass: hook report-data functions so no reports are uploaded ──
  void* anogs = dlopen("libanogs.so", RTLD_NOLOAD);
  if (anogs) {
    void* fn;
    fn = dlsym(anogs, "AnoSDKGetReportData");
    if (fn) DobbyHook(fn, (void*)new_AnoSDKGetReportData, (void**)&_AnoSDKGetReportData);
    fn = dlsym(anogs, "AnoSDKGetReportData3");
    if (fn) DobbyHook(fn, (void*)new_AnoSDKGetReportData3, (void**)&_AnoSDKGetReportData3);
    fn = dlsym(anogs, "AnoSDKGetReportData4");
    if (fn) DobbyHook(fn, (void*)new_AnoSDKGetReportData4, (void**)&_AnoSDKGetReportData4);
    dlclose(anogs);
  }

  ImGuiOK = true;
}


void hack_thread(pid_t pid) {

  StartGUI();
  while(pid == -1) {
    pid = get_pid_by_packages();
  }
  remote_inject(pid);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void * reserved) {
  JNIEnv *env;
  vm->GetEnv((void **) &env, JNI_VERSION_1_6);
  return JNI_VERSION_1_6;
}

__attribute__((constructor))
void lib_main() {
  std::thread thread_hack(hack_thread, get_pid_by_packages());
  thread_hack.detach();
}
