#pragma once
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>
#include <android/log.h>

// =============================================================================
// AssetBundle dumper.
//
// The game decrypts each AssetBundle in memory and hands the plaintext bytes to
// UnityEngine.AssetBundle.LoadFromMemory_Internal(byte[] binary, uint crc). We
// hook that, write `binary` to disk, then call the original. The dumped files are
// standard Unity bundles (magic "UnityFS") that AssetStudio / AssetRipper open.
//
// Output: /storage/emulated/0/HOK_dump/ab_NNNN.unity3d
// il2cpp byte[] layout: length @0x18, data @0x20.
// =============================================================================

bool g_dumpAssets = false;
static int  g_assetCount  = 0;     // files written (shown in menu)
static int  g_assetCalls  = 0;     // LoadFromMemory calls seen
#define HOK_DUMP_DIR "/storage/emulated/0/HOK_dump"

static void* (*_orig_ABLoadMem)(void* binary, uint32_t crc) = nullptr;
static void* hook_ABLoadMem(void* binary, uint32_t crc) {
    if (g_dumpAssets && binary) {
        g_assetCalls++;
        int32_t  len  = *(int32_t*)((uint64_t)binary + 0x18);   // il2cpp byte[] length
        uint8_t* data = (uint8_t*)((uint64_t)binary + 0x20);    // raw bytes
        if (len > 16 && len < 400 * 1024 * 1024) {
            mkdir(HOK_DUMP_DIR, 0777);
            bool isFS = data[0]=='U' && data[1]=='n' && data[2]=='i' && data[3]=='t' &&
                        data[4]=='y' && data[5]=='F' && data[6]=='S';
            char path[160];
            snprintf(path, sizeof(path), HOK_DUMP_DIR "/ab_%04d%s.unity3d",
                     g_assetCount, isFS ? "" : "_raw");
            FILE* f = fopen(path, "wb");
            if (f) {
                fwrite(data, 1, (size_t)len, f);
                fclose(f);
                g_assetCount++;
                __android_log_print(ANDROID_LOG_DEBUG, "HOKDUMP",
                    "wrote %s (%d bytes, UnityFS=%d)", path, len, isFS);
            }
        }
    }
    return _orig_ABLoadMem ? _orig_ABLoadMem(binary, crc) : nullptr;
}
