// --- C Standard ---
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <math.h>
#include <ctime>
#include <limits>

// --- C++ Standard ---
#include <thread>
#include <vector>
#include <fstream>

// --- Android / System ---
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/system_properties.h>

// --- Graphics ---
#include <EGL/egl.h>
#include <GLES3/gl3.h>

// --- External libs / hooks ---
#include <xdl.h>
#include <SubstrateHook.h>
#include <CydiaSubstrate.h>

// --- KittyMemory ---
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

// --- Project headers ---
#include "include/Tools.hpp"
#include "include/obfuscate.h"
#include "include/Theme.h"
#include "imgui/Font.h"
#include "imgui/Roboto-Regular.h"
#include "QQInj.h"
#include "modlingbao.h"
#include "imgui/Icon.h"
#include "imgui/Iconcpp.h"
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"
#include "AutoUpdate/Tools/Call_Tools.h"
#include "Zygisk.hpp"


inline static int g_GlHeight, g_GlWidth;
inline static bool g_IsSetup = false;
inline int prevWidth, prevHeight;

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

char packageName[] = "com.levelinfinite.sgameGlobal.midaspay";

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
        is_game_ = (strcmp(process, packageName) == 0);
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
}


// ── ImGui setup ──────────────────────────────────────────────────────────────
void SetupImgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplAndroid_Init(nullptr);
    ImGuiIO& io = ImGui::GetIO();

    SetYetAnotherDarkTheme();

    ImGui::GetStyle().FrameRounding  = 8;
    ImGui::GetStyle().WindowPadding  = ImVec2(4, 4);
    ImGui::GetStyle().FramePadding   = ImVec2(2, 2);

    ImGui_ImplOpenGL3_Init("#version 100");
    io.IniFilename = nullptr;

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 40.0f;
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0 };
    io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, sizeof(Roboto_Regular), 40.0f, &font_cfg, ranges);

    ImFontConfig icons_cfg;
    icons_cfg.MergeMode  = true;
    icons_cfg.PixelSnapH = true;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(font_awesome_data, font_awesome_size, 40.0f, &icons_cfg, icon_ranges);
}


bool ImGuiOK = false;
bool clearMousePos = true;

// Touch input structs for Unity
struct UnityEngine_Vector2_Fields { float x; float y; };
struct UnityEngine_Vector2_o { UnityEngine_Vector2_Fields fields; };
enum TouchPhase { Began = 0, Moved = 1, Stationary = 2, Ended = 3, Canceled = 4 };
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
        default: r = v; g = p; b = q; break;
    }
    return ImVec4(r, g, b, 1.0f);
}

time_t GetExpiryTimestamp(const char* s) {
    struct tm tm = {0};
    int d, m, y;
    if (sscanf(s, "%d-%d-%d", &d, &m, &y) != 3) return 0;
    tm.tm_mday = d; tm.tm_mon = m - 1;
    tm.tm_year = (y < 100) ? y + 100 : y;
    tm.tm_isdst = -1;
    return mktime(&tm);
}


// ── Toggle button: tap = bật/tắt auto win, drag = di chuyển ─────────────────
void DrawLogo() {
    if (!ImGuiOK) return;

    static time_t expiry = GetExpiryTimestamp("28-10-35");
    time_t now = time(nullptr);
    ImVec2 screen = ImGui::GetIO().DisplaySize;

    if (now > expiry && expiry != 0) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("EXP", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextColored(ImVec4(1,0.2f,0.2f,1), "ModMenu expired");
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Btn", nullptr,
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar      | ImGuiWindowFlags_NoBackground);

    const float size = 100.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, size * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(10, 10));

    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
        lingbao_auto_win ? IM_COL32(0, 220, 80, 90) : IM_COL32(255, 255, 255, 60),
        size * 0.5f);

    ImGui::PushStyleColor(ImGuiCol_Button,
        lingbao_auto_win ? ImVec4(0.0f, 0.8f, 0.3f, 0.55f) : ImVec4(1, 1, 1, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        lingbao_auto_win ? ImVec4(0.0f, 0.9f, 0.4f, 0.65f) : ImVec4(1, 1, 1, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        lingbao_auto_win ? ImVec4(0.0f, 1.0f, 0.5f, 0.75f) : ImVec4(1, 1, 1, 0.45f));

    float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text,
        lingbao_auto_win ? ImVec4(0, 1, 0.4f, 1) : (ImVec4)ImColor::HSV(hue, 0.8f, 1.0f));

    ImGui::Button(ICON_FA_POWER_OFF, ImVec2(size, size));

    static bool dragging = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        dragging = true;
        ImVec2 d = ImGui::GetIO().MouseDelta;
        ImVec2 w = ImGui::GetWindowPos();
        ImGui::SetWindowPos(ImVec2(w.x + d.x, w.y + d.y));
    }
    if (ImGui::IsItemDeactivated()) {
        if (!dragging && ImGui::IsItemHovered()) {
            lingbao_auto_win   = !lingbao_auto_win;
            g_force_win_called = false;
        }
        dragging = false;
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    ImGui::End();
}


// ── Render hook ──────────────────────────────────────────────────────────────
inline EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
inline EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH,  &g_GlWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &g_GlHeight);

    static bool should_clear_mouse_pos = false;

    if (!g_IsSetup) {
        prevWidth = g_GlWidth; prevHeight = g_GlHeight;
        SetupImgui();
        g_IsSetup = true;
    }

    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_GlWidth, g_GlHeight);
    ImGui::NewFrame();

    if (ImGuiOK) {
        int (*TouchCount)(void*) = (int (*)(void*))
            Il2CppGetMethodOffset("UnityEngine.dll", "UnityEngine", "Input", "get_touchCount", 0);
        int touchCount = TouchCount(nullptr);
        if (touchCount > 0) {
            UnityEngine_Touch_Fields touch = ((UnityEngine_Touch_Fields (*)(int))
                Il2CppGetMethodOffset("UnityEngine.dll", "UnityEngine", "Input", "GetTouch", 1))(0);
            float ry = io.DisplaySize.y - touch.m_Position.fields.y;
            switch (touch.m_Phase) {
                case TouchPhase::Began:
                case TouchPhase::Stationary:
                    io.MousePos    = ImVec2(touch.m_Position.fields.x, ry);
                    io.MouseDown[0] = true;  break;
                case TouchPhase::Ended:
                case TouchPhase::Canceled:
                    io.MouseDown[0] = false;
                    should_clear_mouse_pos = true; break;
                case TouchPhase::Moved:
                    io.MousePos = ImVec2(touch.m_Position.fields.x, ry); break;
                default: break;
            }
        } else {
            io.MouseDown[0] = false;
        }
    }

    DrawLogo();

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


// ── Misc helpers ─────────────────────────────────────────────────────────────
typedef unsigned long DWORD;
static uintptr_t libBase;

uintptr_t string2Offset(const char *c) {
    int base = 16;
    static_assert(sizeof(uintptr_t) == sizeof(unsigned long) ||
                  sizeof(uintptr_t) == sizeof(unsigned long long), "arch");
    if (sizeof(uintptr_t) == sizeof(unsigned long)) return strtoul(c, nullptr, base);
    return strtoull(c, nullptr, base);
}

ProcMap unityMap, anogsMap, il2cppMap;
using KittyScanner::RegisterNativeFn;

void hack() { LOGD("Inject Ok"); }

uintptr_t get_symbol_addr_in_pid(pid_t pid, const char* libname, uintptr_t offset_in_lib) {
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    FILE* fp = fopen(maps_path, "r");
    if (!fp) return 0;
    char line[512]; uintptr_t base = 0;
    while (fgets(line, sizeof(line), fp))
        if (strstr(line, libname)) { sscanf(line, "%lx-%*lx", &base); break; }
    fclose(fp);
    return base ? base + offset_in_lib : 0;
}

pid_t get_pid_by_name(const char* process_name) {
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) return -1;
    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        pid_t pid = atoi(entry->d_name);
        if (pid <= 0) continue;
        char path[256]; snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE* fp = fopen(path, "r");
        if (!fp) continue;
        char cmd[256]; fgets(cmd, sizeof(cmd), fp); fclose(fp);
        if (strstr(cmd, process_name)) { closedir(proc_dir); return pid; }
    }
    closedir(proc_dir);
    return -1;
}

void writeLog(const std::string& logMessage, const std::string& filename) {
    std::ofstream f(filename, std::ios::app);
    if (f.is_open()) { f << logMessage << std::endl; f.close(); }
}


// ── AnoSDK bypass ────────────────────────────────────────────────────────────
static int32_t (*_AnoSDKGetReportData)(char*, int32_t)  = nullptr;
static int32_t (*_AnoSDKGetReportData3)(char*, int32_t) = nullptr;
static int32_t (*_AnoSDKGetReportData4)(char*, int32_t) = nullptr;
static int32_t new_AnoSDKGetReportData(char*, int32_t)  { return 0; }
static int32_t new_AnoSDKGetReportData3(char*, int32_t) { return 0; }
static int32_t new_AnoSDKGetReportData4(char*, int32_t) { return 0; }


// ── Hook injection ───────────────────────────────────────────────────────────
inline void hack_injec();
inline void StartGUI() {
    void *ptr = DobbySymbolResolver("/system/lib/libEGL.so", "eglSwapBuffers");
    if (ptr) {
        DobbyHook(ptr, (void*)hook_eglSwapBuffers, (void**)&old_eglSwapBuffers);
        LOGD("Gui Started");
        hack_injec();
    }
}

void hack_injec() {
    while (!unityMap.isValid()) {
        unityMap  = KittyMemory::getLibraryBaseMap("libunity.so");
        il2cppMap = KittyMemory::getLibraryBaseMap("libil2cpp.so");
        sleep(5);
    }
    il2cpp_base = il2cppMap.startAddress;
    sleep(5);
    Il2CppAttach("libil2cpp.so");

    // ── LingBao Auto Win ─────────────────────────────────────────────────────
    {
        void* lbAddr;

        // LingBaoFightForm::UpdateBlood – ép enemyBlood=0 + gọi ForceFightOver
        lbAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameSystem",
                                        "LingBaoFightForm", "UpdateBlood", 6);
        if (!lbAddr) lbAddr = getRealAddr(0x5AA53A8);
        if (lbAddr) DobbyHook(lbAddr, (void*)new_UpdateBlood, (void**)&_UpdateBlood);

        // SGW::ForceFightOver(isGm, isWin, isQuit) static
        lbAddr = Il2CppGetMethodOffset("Scripts.Base.dll", "", "SGW", "ForceFightOver", 3);
        if (!lbAddr) lbAddr = getRealAddr(0x4853FF0);
        if (lbAddr) fn_ForceFightOver = (fn_ForceFightOver_t)lbAddr;
    }

    // ── AnoSDK bypass ────────────────────────────────────────────────────────
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
    while (pid == -1) pid = get_pid_by_name(packageName);
    remote_inject(pid);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **)&env, JNI_VERSION_1_6);
    return JNI_VERSION_1_6;
}

__attribute__((constructor))
void lib_main() {
    std::thread thread_hack(hack_thread, get_pid_by_name(packageName));
    thread_hack.detach();
}

