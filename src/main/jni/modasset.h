#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <mutex>
#include <dirent.h>
#include <sys/stat.h>
#include <android/log.h>
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"

// =============================================================================
// AssetBundle dumper. Hooks both load paths Unity exposes here:
//   AssetBundle.LoadFromMemory_Internal(byte[] binary, uint crc)
//   AssetBundle.LoadFromFile_Internal(string path, uint crc, ulong offset)
// and writes the (decrypted) bundle bytes to disk. Files carry the "UnityFS"
// magic and open in AssetStudio / AssetRipper.
//
// Modes:  0 off | 1 dump all | 2 list & pick
// =============================================================================

static int  g_assetMode  = 0;
static int  g_assetCalls = 0;     // load calls seen (mem + file)
static int  g_assetSaved = 0;     // files written
static char g_assetSaveDir[256] = "/storage/emulated/0/HokAss";
static char g_assetScanDir[256] = "/storage/emulated/0/HokAss";
static char g_assetLastPath[256] = "";   // last LoadFromFile path seen

struct AssetRec {
    int   id = 0, size = 0;
    bool  isFS = false, saved = false;
    char  src[80] = {0};          // short source name
    std::vector<uint8_t> bytes;
};
static std::vector<AssetRec> g_assetList;
static std::vector<std::string> g_scanList;
static std::mutex g_assetMtx;
static size_t g_assetMem = 0;
static const size_t g_assetMemCap = 320ULL * 1024 * 1024;
static int g_assetSeq = 0;

static bool asset_write_bytes(int id, const uint8_t* data, int len, bool isFS) {
    mkdir(g_assetSaveDir, 0777);
    char path[400];
    snprintf(path, sizeof(path), "%s/ab_%04d%s.unity3d", g_assetSaveDir, id, isFS ? "" : "_raw");
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    __android_log_print(ANDROID_LOG_DEBUG, "HOKDUMP", "wrote %s (%d bytes)", path, len);
    return true;
}

// shared capture path for both hooks
static void asset_capture(const uint8_t* data, int len, const char* src) {
    if (len <= 16 || len >= 400 * 1024 * 1024) return;
    bool isFS = data[0]=='U'&&data[1]=='n'&&data[2]=='i'&&data[3]=='t'&&
                data[4]=='y'&&data[5]=='F'&&data[6]=='S';
    if (g_assetMode == 1) {
        if (asset_write_bytes(g_assetSeq++, data, len, isFS)) g_assetSaved++;
        return;
    }
    std::lock_guard<std::mutex> lk(g_assetMtx);
    while (g_assetMem + (size_t)len > g_assetMemCap && !g_assetList.empty()) {
        g_assetMem -= g_assetList.front().bytes.size();
        g_assetList.erase(g_assetList.begin());
    }
    AssetRec r; r.id = g_assetSeq++; r.size = len; r.isFS = isFS;
    if (src) { strncpy(r.src, src, sizeof(r.src)-1); }
    r.bytes.assign(data, data + len);
    g_assetMem += (size_t)len;
    g_assetList.push_back(std::move(r));
}

// ── Hook 1: LoadFromMemory_Internal(byte[] binary, uint crc) ──────────────────
static void* (*_orig_ABLoadMem)(void* binary, uint32_t crc) = nullptr;
static void* hook_ABLoadMem(void* binary, uint32_t crc) {
    if (g_assetMode && binary) {
        g_assetCalls++;
        int32_t len = *(int32_t*)((uint64_t)binary + 0x18);
        uint8_t* data = (uint8_t*)((uint64_t)binary + 0x20);
        asset_capture(data, len, "mem");
    }
    return _orig_ABLoadMem ? _orig_ABLoadMem(binary, crc) : nullptr;
}

// ── Hook 2: LoadFromFile_Internal(string path, uint crc, ulong offset) ────────
static void* (*_orig_ABLoadFile)(void* pathStr, uint32_t crc, uint64_t offset) = nullptr;
static void* hook_ABLoadFile(void* pathStr, uint32_t crc, uint64_t offset) {
    if (g_assetMode && pathStr) {
        g_assetCalls++;
        const char* p = ((String*)pathStr)->CString();
        if (p) {
            strncpy(g_assetLastPath, p, sizeof(g_assetLastPath)-1);
            FILE* f = fopen(p, "rb");
            if (f) {
                fseek(f, 0, SEEK_END); long fsz = ftell(f);
                long start = (long)offset; if (start < 0 || start > fsz) start = 0;
                long n = fsz - start;
                if (n > 16 && n < 400L*1024*1024) {
                    fseek(f, start, SEEK_SET);
                    std::vector<uint8_t> buf((size_t)n);
                    if (fread(buf.data(), 1, (size_t)n, f) == (size_t)n) {
                        const char* base = strrchr(p, '/'); base = base ? base+1 : p;
                        asset_capture(buf.data(), (int)n, base);
                    }
                }
                fclose(f);
            }
        }
    }
    return _orig_ABLoadFile ? _orig_ABLoadFile(pathStr, crc, offset) : nullptr;
}

static void asset_scan(const char* dir) {
    g_scanList.clear();
    DIR* d = opendir(dir);
    if (!d) { g_scanList.push_back("(khong mo duoc thu muc)"); return; }
    struct dirent* e;
    while ((e = readdir(d)) != nullptr && g_scanList.size() < 500) {
        const char* n = e->d_name;
        if (n[0] == '.') continue;
        const char* dot = strrchr(n, '.');
        bool ok = dot && (!strcmp(dot, ".assetbundle") || !strcmp(dot, ".ab") ||
                          !strcmp(dot, ".unity3d") || !strcmp(dot, ".bundle") || !strcmp(dot, ".bytes"));
        if (ok || e->d_type == DT_REG) g_scanList.push_back(n);
    }
    closedir(d);
}

// ── Menu UI ──────────────────────────────────────────────────────────────────
static void DrawAssetUI() {
    ImGui::Text("Hook: mem=%s file=%s", _orig_ABLoadMem ? "OK" : "NULL", _orig_ABLoadFile ? "OK" : "NULL");
    ImGui::RadioButton("Tat", &g_assetMode, 0);          ImGui::SameLine();
    ImGui::RadioButton("Dump tat ca", &g_assetMode, 1);  ImGui::SameLine();
    ImGui::RadioButton("Chon tu danh sach", &g_assetMode, 2);

    ImGui::InputText("Luu vao", g_assetSaveDir, sizeof(g_assetSaveDir));
    ImGui::Text("calls=%d  da luu=%d", g_assetCalls, g_assetSaved);
    if (g_assetLastPath[0]) ImGui::TextWrapped("Game nap: %s", g_assetLastPath);

    if (g_assetMode == 2) {
        if (ImGui::Button("Luu tat ca", ImVec2(-1, 0))) {
            std::lock_guard<std::mutex> lk(g_assetMtx);
            for (auto& r : g_assetList) if (!r.saved && asset_write_bytes(r.id, r.bytes.data(), r.size, r.isFS)) { r.saved = true; g_assetSaved++; }
        }
        ImGui::BeginChild("ablist", ImVec2(-1, 180), true);
        std::lock_guard<std::mutex> lk(g_assetMtx);
        for (int i = (int)g_assetList.size() - 1; i >= 0; --i) {
            AssetRec& r = g_assetList[i];
            ImGui::PushID(r.id);
            if (!r.saved) { if (ImGui::SmallButton("Luu") && asset_write_bytes(r.id, r.bytes.data(), r.size, r.isFS)) { r.saved = true; g_assetSaved++; } }
            else          { ImGui::TextDisabled("Da luu"); }
            ImGui::SameLine();
            ImGui::Text("ab_%04d %dKB %s %s", r.id, r.size/1024, r.isFS?"FS":"raw", r.src);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::InputText("Quet", g_assetScanDir, sizeof(g_assetScanDir));
    if (ImGui::Button("Scan files")) asset_scan(g_assetScanDir);
    if (!g_scanList.empty()) {
        ImGui::Text("Tim thay %d files:", (int)g_scanList.size());
        ImGui::BeginChild("scanlist", ImVec2(-1, 140), true);
        for (auto& s : g_scanList) ImGui::TextUnformatted(s.c_str());
        ImGui::EndChild();
    }
}
