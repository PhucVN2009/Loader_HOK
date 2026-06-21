#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <mutex>
#include <sys/stat.h>
#include <android/log.h>

// =============================================================================
// AssetBundle dumper.
//
// The game decrypts each AssetBundle in memory and passes the plaintext to
// UnityEngine.AssetBundle.LoadFromMemory_Internal(byte[] binary, uint crc).
// We hook it, grab `binary`, then call the original.
//
// Two modes:
//   1 = Dump All  : every loaded bundle is written to disk immediately.
//   2 = List/Pick : bundles are kept in memory and listed in the menu; you tap
//                   Save on the ones you want (or Save All).
//
// Output dir: /storage/emulated/0/HokAss/   (files: ab_NNNN.unity3d, "UnityFS")
// il2cpp byte[] layout: length @0x18, data @0x20.
// =============================================================================

#define HOK_ASSET_DIR "/storage/emulated/0/HokAss"

static int  g_assetMode  = 0;   // 0 off, 1 dump all, 2 list/pick
static int  g_assetCalls = 0;   // LoadFromMemory calls seen
static int  g_assetSaved = 0;   // files written to disk

struct AssetRec {
    int   id   = 0;
    int   size = 0;
    bool  isFS = false;
    bool  saved = false;
    std::vector<uint8_t> bytes;  // kept only in List mode
};
static std::vector<AssetRec> g_assetList;
static std::mutex            g_assetMtx;
static size_t                g_assetMem    = 0;
static const size_t          g_assetMemCap = 320ULL * 1024 * 1024; // 320 MB cache
static int                   g_assetSeq    = 0;

static bool asset_write(int id, const uint8_t* data, int len, bool isFS) {
    mkdir(HOK_ASSET_DIR, 0777);
    char path[160];
    snprintf(path, sizeof(path), HOK_ASSET_DIR "/ab_%04d%s.unity3d", id, isFS ? "" : "_raw");
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    __android_log_print(ANDROID_LOG_DEBUG, "HOKDUMP", "wrote %s (%d bytes)", path, len);
    return true;
}

static void* (*_orig_ABLoadMem)(void* binary, uint32_t crc) = nullptr;
static void* hook_ABLoadMem(void* binary, uint32_t crc) {
    if (g_assetMode && binary) {
        g_assetCalls++;
        int32_t  len  = *(int32_t*)((uint64_t)binary + 0x18);
        uint8_t* data = (uint8_t*)((uint64_t)binary + 0x20);
        if (len > 16 && len < 400 * 1024 * 1024) {
            bool isFS = data[0]=='U'&&data[1]=='n'&&data[2]=='i'&&data[3]=='t'&&
                        data[4]=='y'&&data[5]=='F'&&data[6]=='S';
            if (g_assetMode == 1) {
                if (asset_write(g_assetSeq++, data, len, isFS)) g_assetSaved++;
            } else { // mode 2: cache for the picker
                std::lock_guard<std::mutex> lk(g_assetMtx);
                while (g_assetMem + (size_t)len > g_assetMemCap && !g_assetList.empty()) {
                    g_assetMem -= g_assetList.front().bytes.size();
                    g_assetList.erase(g_assetList.begin());
                }
                AssetRec r; r.id = g_assetSeq++; r.size = len; r.isFS = isFS;
                r.bytes.assign(data, data + len);
                g_assetMem += (size_t)len;
                g_assetList.push_back(std::move(r));
            }
        }
    }
    return _orig_ABLoadMem ? _orig_ABLoadMem(binary, crc) : nullptr;
}

// ── Menu UI (ImGui available because included after imgui in Main.cpp) ────────
static void DrawAssetUI() {
    ImGui::Text("Che do dump AssetBundle:");
    ImGui::RadioButton("Tat", &g_assetMode, 0);        ImGui::SameLine();
    ImGui::RadioButton("Dump tat ca", &g_assetMode, 1); ImGui::SameLine();
    ImGui::RadioButton("Chon tu danh sach", &g_assetMode, 2);
    ImGui::TextColored(ImColor(180, 230, 255), "Luu vao " HOK_ASSET_DIR);
    ImGui::Text("calls=%d  da luu=%d", g_assetCalls, g_assetSaved);

    if (g_assetMode == 2) {
        if (ImGui::Button("Luu tat ca", ImVec2(-1, 0))) {
            std::lock_guard<std::mutex> lk(g_assetMtx);
            for (auto& r : g_assetList) if (!r.saved && asset_write(r.id, r.bytes.data(), r.size, r.isFS)) { r.saved = true; g_assetSaved++; }
        }
        ImGui::BeginChild("ablist", ImVec2(-1, 220), true);
        std::lock_guard<std::mutex> lk(g_assetMtx);
        for (int i = (int)g_assetList.size() - 1; i >= 0; --i) {   // newest first
            AssetRec& r = g_assetList[i];
            ImGui::PushID(r.id);
            char btn[16]; snprintf(btn, sizeof(btn), r.saved ? "Da luu" : "Luu");
            if (!r.saved) { if (ImGui::SmallButton(btn) && asset_write(r.id, r.bytes.data(), r.size, r.isFS)) { r.saved = true; g_assetSaved++; } }
            else          { ImGui::TextDisabled("Da luu"); }
            ImGui::SameLine();
            ImGui::Text("ab_%04d  %d KB  %s", r.id, r.size / 1024, r.isFS ? "UnityFS" : "raw");
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}
