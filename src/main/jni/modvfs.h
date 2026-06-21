#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <android/log.h>

// =============================================================================
// QtsVFS dumper.  HOK packs all resources into encrypted/packed .db containers
// (QtsVFSCache/packages) served by libQtsVFS.so, which exports a clean POSIX-like
// API (VFS_OpenFile / VFS_Read / VFS_CloseFile ...). We hook those exports BY
// NAME via dlsym (auto-updates across versions) and reconstruct every file the
// game reads — already decompressed/decrypted — to disk, preserving its path.
// =============================================================================

extern char g_assetSaveDir[256];   // defined in modasset.h

static bool g_vfsDump   = false;
static int  g_vfsOpened = 0;
static int  g_vfsWritten = 0;
static char g_vfsLast[200] = {0};

struct VfsBuf { std::string path; std::vector<uint8_t> data; };
static std::map<void*, VfsBuf> g_vfsMap;
static std::mutex g_vfsMtx;

static void* (*orig_VFS_OpenFile)(const char*, const char*) = nullptr;
static long  (*orig_VFS_Read)(void*, void*, long)           = nullptr;
static int   (*orig_VFS_CloseFile)(void*)                   = nullptr;

// mkdir -p for the directory part of a full file path
static void vfs_mkparents(const char* full) {
    char tmp[512]; strncpy(tmp, full, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    }
}

static void vfs_save(const std::string& vpath, const std::vector<uint8_t>& d) {
    // sanitize: strip leading slashes, keep structure under save dir
    const char* vp = vpath.c_str();
    while (*vp == '/') vp++;
    char out[512];
    snprintf(out, sizeof(out), "%s/%s", g_assetSaveDir, vp);
    vfs_mkparents(out);
    FILE* f = fopen(out, "wb");
    if (!f) return;
    if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    g_vfsWritten++;
    strncpy(g_vfsLast, vp, sizeof(g_vfsLast)-1);
    __android_log_print(ANDROID_LOG_DEBUG, "HOKVFS", "saved %s (%zu bytes)", out, d.size());
}

static void* h_VFS_OpenFile(const char* path, const char* mode) {
    void* h = orig_VFS_OpenFile ? orig_VFS_OpenFile(path, mode) : nullptr;
    if (g_vfsDump && h && path) {
        std::lock_guard<std::mutex> lk(g_vfsMtx);
        VfsBuf b; b.path = path;
        g_vfsMap[h] = std::move(b);
        g_vfsOpened++;
    }
    return h;
}
static long h_VFS_Read(void* h, void* buf, long size) {
    long n = orig_VFS_Read ? orig_VFS_Read(h, buf, size) : 0;
    if (g_vfsDump && h && buf && n > 0) {
        std::lock_guard<std::mutex> lk(g_vfsMtx);
        auto it = g_vfsMap.find(h);
        if (it != g_vfsMap.end())
            it->second.data.insert(it->second.data.end(), (uint8_t*)buf, (uint8_t*)buf + n);
    }
    return n;
}
static int h_VFS_CloseFile(void* h) {
    if (g_vfsDump && h) {
        std::lock_guard<std::mutex> lk(g_vfsMtx);
        auto it = g_vfsMap.find(h);
        if (it != g_vfsMap.end()) {
            if (!it->second.data.empty()) vfs_save(it->second.path, it->second.data);
            g_vfsMap.erase(it);
        }
    }
    return orig_VFS_CloseFile ? orig_VFS_CloseFile(h) : 0;
}

static bool g_vfsHooked = false;
static void InstallVfsHooks() {
    if (g_vfsHooked) return;
    void* lib = nullptr;
    for (int i = 0; i < 90 && !lib; i++) { lib = dlopen("libQtsVFS.so", RTLD_NOLOAD); if (!lib) sleep(1); }
    if (!lib) { __android_log_print(ANDROID_LOG_ERROR, "HOKVFS", "libQtsVFS.so not loaded"); return; }
    void* o = dlsym(lib, "VFS_OpenFile");
    void* r = dlsym(lib, "VFS_Read");
    void* c = dlsym(lib, "VFS_CloseFile");
    if (o) DobbyHook(o, (void*)h_VFS_OpenFile, (void**)&orig_VFS_OpenFile);
    if (r) DobbyHook(r, (void*)h_VFS_Read,     (void**)&orig_VFS_Read);
    if (c) DobbyHook(c, (void*)h_VFS_CloseFile,(void**)&orig_VFS_CloseFile);
    g_vfsHooked = true;
    __android_log_print(ANDROID_LOG_DEBUG, "HOKVFS", "hooks: open=%p read=%p close=%p", o, r, c);
}
