#pragma once
#include <jni.h>
#include <pthread.h>
#include <string>
#include <cstdlib>   // rand()
#include <ctime>     // time()

// =========================================================
//  SETUP DANH SÁCH NHẠC TẠI ĐÂY
// =========================================================
struct Song { const char* name; const char* url; };
static const Song SONG_LIST[] = {
    { "Nhu Mot Nguoi Dung",  "https://hh3d36.site/youtube_bazvD6lmBJg.m4a" },
    { "Dia Nguc Tran Gian",  "https://hh3d36.site/youtube_05S2uf37ufc.m4a" },
    { "Bat Van Biet Ly",     "https://hh3d36.site/youtube_RMDRA_DqbEw.m4a" },
};
static const int SONG_COUNT = sizeof(SONG_LIST) / sizeof(SONG_LIST[0]);
// =========================================================

extern JavaVM* g_jvm;

namespace MP {

// --- Trạng thái ---
static jobject  obj        = nullptr;
static int      cur        = 0;
static bool     playing    = false;
static bool     preparing  = false;
static float    speed      = 1.0f;    // 0.5 – 2.0
static float    volume     = 1.0f;    // 0.0 – 1.0  ← MỚI
static bool     shuffle    = false;   // ← MỚI
// loop_mode: 0=tắt, 1=lặp tất cả, 2=lặp 1 bài
static int      loop_mode  = 1;

// --- Bảo vệ chống race khi user click đổi bài liên tục ---
static volatile int play_seq      = 0;   // sequence number hiện tại
static int          last_started  = 0;   // frame counter khi bài bắt đầu phát
static int          tick_counter  = 0;   // tăng mỗi lần Tick() được gọi
static int          not_playing_cnt = 0; // số frame liên tiếp isPlaying()==false

// --- Lấy JNIEnv ---
static JNIEnv* Env() {
    if (!g_jvm) return nullptr;
    JNIEnv* env = nullptr;
    g_jvm->AttachCurrentThread(&env, nullptr);
    return env;
}

// --- Bài tiếp theo khi shuffle ---
static int NextIdx() {
    if (shuffle && SONG_COUNT > 1) {
        int next;
        do { next = rand() % SONG_COUNT; } while (next == cur);
        return next;
    }
    return (cur + 1) % SONG_COUNT;
}

// --- Helper: release một MediaPlayer cụ thể (không phải global obj) ---
static void ReleaseLocal(JNIEnv* env, jobject mp) {
    if (!env || !mp) return;
    jclass cls = env->GetObjectClass(mp);
    jmethodID mid_stop    = env->GetMethodID(cls, "stop",    "()V");
    jmethodID mid_release = env->GetMethodID(cls, "release", "()V");
    if (mid_stop)    { env->CallVoidMethod(mp, mid_stop);    env->ExceptionClear(); }
    if (mid_release) { env->CallVoidMethod(mp, mid_release); env->ExceptionClear(); }
}

// --- Giải phóng MediaPlayer cũ ---
static void Release() {
    ++play_seq;          // invalidate any in-flight load
    JNIEnv* env = Env();
    if (!env || !obj) {
        playing = false;
        preparing = false;
        return;
    }
    jobject old = obj;
    obj = nullptr;       // clear trước để PlayThread khác đọc thấy null
    ReleaseLocal(env, old);
    env->DeleteGlobalRef(old);
    playing  = false;
    preparing = false;
    not_playing_cnt = 0;
}

// --- Áp dụng speed, loop, volume sau khi start ---
static void ApplyParams(JNIEnv* env, jobject mp) {
    if (!env || !mp) return;
    jclass cls = env->GetObjectClass(mp);

    // setLooping cho loop 1 bài
    jmethodID mid_loop = env->GetMethodID(cls, "setLooping", "(Z)V");
    if (mid_loop)
        env->CallVoidMethod(mp, mid_loop, (jboolean)(loop_mode == 2));

    // setVolume(left, right)  ← MỚI
    jmethodID mid_vol = env->GetMethodID(cls, "setVolume", "(FF)V");
    if (mid_vol)
        env->CallVoidMethod(mp, mid_vol, (jfloat)volume, (jfloat)volume);

    // setPlaybackParams (API 23+)
    jclass pp_cls = env->FindClass("android/media/PlaybackParams");
    if (!pp_cls) { env->ExceptionClear(); return; }
    jmethodID pp_init  = env->GetMethodID(pp_cls, "<init>", "()V");
    jmethodID pp_speed = env->GetMethodID(pp_cls, "setSpeed", "(F)Landroid/media/PlaybackParams;");
    if (!pp_init || !pp_speed) { env->ExceptionClear(); return; }
    jobject pp = env->NewObject(pp_cls, pp_init);
    if (pp) {
        env->CallObjectMethod(pp, pp_speed, speed);
        if (!env->ExceptionCheck()) {
            jmethodID mid_pbp = env->GetMethodID(cls, "setPlaybackParams",
                "(Landroid/media/PlaybackParams;)V");
            if (mid_pbp) env->CallVoidMethod(mp, mid_pbp, pp);
        }
        env->ExceptionClear();
        env->DeleteLocalRef(pp);
    }
}

// --- Cập nhật volume realtime  ← MỚI ---
static void SetVolume() {
    JNIEnv* env = Env();
    if (!env || !obj) return;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "setVolume", "(FF)V");
    if (mid) env->CallVoidMethod(obj, mid, (jfloat)volume, (jfloat)volume);
}

// --- Thread phát nhạc (prepare blocking) ---
struct PlayArg { int idx; int seq; };
static void* PlayThread(void* arg) {
    PlayArg* a = (PlayArg*)arg;
    int idx = a->idx;
    int my_seq = a->seq;
    delete a;

    JNIEnv* env = Env();
    if (!env) return nullptr;

    jclass mp_cls = env->FindClass("android/media/MediaPlayer");
    if (!mp_cls) return nullptr;
    jmethodID mp_init = env->GetMethodID(mp_cls, "<init>", "()V");
    jobject mp_local  = env->NewObject(mp_cls, mp_init);
    if (!mp_local) return nullptr;

    jmethodID set_ds = env->GetMethodID(mp_cls, "setDataSource", "(Ljava/lang/String;)V");
    jstring jurl = env->NewStringUTF(SONG_LIST[idx].url);
    env->CallVoidMethod(mp_local, set_ds, jurl);
    env->DeleteLocalRef(jurl);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ReleaseLocal(env, mp_local);
        env->DeleteLocalRef(mp_local);
        if (my_seq == play_seq) preparing = false;
        return nullptr;
    }

    jmethodID prepare = env->GetMethodID(mp_cls, "prepare", "()V");
    env->CallVoidMethod(mp_local, prepare);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ReleaseLocal(env, mp_local);
        env->DeleteLocalRef(mp_local);
        if (my_seq == play_seq) preparing = false;
        return nullptr;
    }

    // ── Nếu user đã click sang bài khác trong lúc đang load,
    //    bỏ kết quả này — KHÔNG đụng vào obj toàn cục ──
    if (my_seq != play_seq) {
        ReleaseLocal(env, mp_local);
        env->DeleteLocalRef(mp_local);
        return nullptr;
    }

    jobject mp_global = env->NewGlobalRef(mp_local);
    env->DeleteLocalRef(mp_local);

    // Giải phóng player cũ (nếu có)
    if (obj) {
        ReleaseLocal(env, obj);
        env->DeleteGlobalRef(obj);
        obj = nullptr;
    }
    obj = mp_global;

    jmethodID start = env->GetMethodID(mp_cls, "start", "()V");
    env->CallVoidMethod(obj, start);
    env->ExceptionClear();

    ApplyParams(env, obj);

    // Đặt cờ playing CUỐI cùng + reset bộ đếm chống nhảy bài
    last_started   = tick_counter;
    not_playing_cnt = 0;
    playing   = true;
    preparing = false;
    return nullptr;
}

// --- Phát bài theo index ---
static void Play(int idx) {
    if (idx < 0) idx = SONG_COUNT - 1;
    if (idx >= SONG_COUNT) idx = 0;
    cur       = idx;
    preparing = true;
    playing   = false;
    not_playing_cnt = 0;
    int my_seq = ++play_seq;          // bumb sequence → invalidate thread cũ
    pthread_t t;
    auto* a = new PlayArg{idx, my_seq};
    pthread_create(&t, nullptr, PlayThread, a);
    pthread_detach(t);
}

// --- Pause / Resume ---
static void TogglePause() {
    JNIEnv* env = Env();
    if (!env || !obj) return;
    jclass cls = env->GetObjectClass(obj);
    if (playing) {
        jmethodID mid = env->GetMethodID(cls, "pause", "()V");
        if (mid) env->CallVoidMethod(obj, mid);
        playing = false;
    } else {
        jmethodID mid = env->GetMethodID(cls, "start", "()V");
        if (mid) env->CallVoidMethod(obj, mid);
        playing = true;
        ApplyParams(env, obj);
    }
}

// --- Cập nhật speed realtime ---
// FIX: bỏ check !playing → áp dụng speed kể cả khi đang pause
static void UpdateSpeed() {
    JNIEnv* env = Env();
    if (!env || !obj) return;   // chỉ cần có obj là đủ
    ApplyParams(env, obj);
}

// --- Kiểm tra bài có đang phát không ---
static bool IsActuallyPlaying() {
    JNIEnv* env = Env();
    if (!env || !obj) return false;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "isPlaying", "()Z");
    if (!mid) return false;
    return (bool)env->CallBooleanMethod(obj, mid);
}

// --- Gọi mỗi frame để auto next ---
static void Tick() {
    tick_counter++;
    if (!obj || preparing) {
        not_playing_cnt = 0;
        return;
    }
    if (!playing) return;

    // Giai đoạn ân hạn: ngay sau khi start(), MediaPlayer cần vài frame
    // để isPlaying() thực sự trả về true → tránh trigger Play(NextIdx) sai.
    if (tick_counter - last_started < 30) return;

    if (IsActuallyPlaying()) {
        not_playing_cnt = 0;
        return;
    }
    // Yêu cầu liên tiếp ~20 frame !isPlaying mới coi là bài đã hết.
    if (++not_playing_cnt < 20) return;

    not_playing_cnt = 0;
    playing = false;
    if (loop_mode == 1) {
        Play(NextIdx());
    }
    // loop_mode == 0: dừng hẳn
    // loop_mode == 2: MediaPlayer.setLooping tự lặp
}

// --- Lấy vị trí / tổng thời gian (ms) ---
static int GetPosition() {
    JNIEnv* env = Env();
    if (!env || !obj) return 0;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "getCurrentPosition", "()I");
    return mid ? (int)env->CallIntMethod(obj, mid) : 0;
}

static int GetDuration() {
    JNIEnv* env = Env();
    if (!env || !obj) return 1;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "getDuration", "()I");
    int d = mid ? (int)env->CallIntMethod(obj, mid) : 0;
    return d > 0 ? d : 1;
}

// --- Format mm:ss ---
static std::string FmtTime(int ms) {
    int s = ms / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

} // namespace MP
