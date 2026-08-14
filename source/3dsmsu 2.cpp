#include "3dsmsu.h"
#include <atomic>
#include <cstring>

namespace {
struct BridgeState {
    bool             initialized;
    Msu1AudioBackend backend;
    int16_t*         staging;
    uint32_t         staging_samples;
    float            global_volume;
    float            user_volume;
    // mute/drain flags: written by the emu/main thread (msu3dsOnEvent),
    // read by the mixing thread (msu3dsFillAudio) — atomic per coding
    // standard section 7, matching snd3DS.generateSilence
    std::atomic<bool> menu_muted;
    std::atomic<bool> turbo_muted;
    std::atomic<bool> apt_muted;
    std::atomic<bool> drain_active;
    bool             queued_since_clear;   // mixing-thread-only (set in fill, cleared under drain)
    uint32_t         underruns;
};
BridgeState g_bridge = {};

// atomics make BridgeState non-copyable: reset member-wise instead of {}-assign
void reset_bridge(void)
{
    g_bridge.initialized        = false;
    g_bridge.backend            = Msu1AudioBackend{};
    g_bridge.staging            = nullptr;
    g_bridge.staging_samples    = 0;
    g_bridge.global_volume      = 0.0f;
    g_bridge.user_volume        = 0.0f;
    g_bridge.menu_muted         = false;
    g_bridge.turbo_muted        = false;
    g_bridge.apt_muted          = false;
    g_bridge.drain_active       = false;
    g_bridge.queued_since_clear = false;
    g_bridge.underruns          = 0;
}

void bridge_volume_cb(void) { msu3dsOnEvent(Msu1Event::VolumeChanged); }

void apply_mix(void)
{
    if (!g_bridge.initialized) { return; }
    float mix = 0.0f;
    if (!g_bridge.menu_muted && !g_bridge.turbo_muted && !g_bridge.apt_muted) {
        mix = g_bridge.global_volume * g_bridge.user_volume * ((float)MSU1.volume / 255.0f);
    }
    g_bridge.backend.set_mix(mix);
}
} // namespace

namespace {

// Data-track read-ahead ring (phase B). Producer = mixing thread
// (msu3dsDataPrefetchFill), consumer = emulation thread via the msu1 core
// prefetch hook. The ring lock is a LEAF: nothing else is acquired while
// holding it, and both critical sections are memcpy/index-sized — the
// consumer never touches the mixer's long-held snesAccessLock.
struct PrefetchRing {
    uint8_t* buf;
    uint32_t cap;
    uint32_t rd;            // physical index of the window's first byte
    uint32_t avail;         // valid bytes in the window
    uint32_t file_pos_next; // file offset one past the window's last byte
    uint32_t gen;           // bumped on every window reset (miss/seek/event)
    FILE*    prod_file;     // producer's own handle on <base>.msu
    uint32_t prod_pos;      // producer file position (UINT32_MAX = unknown)
    void (*lock)(void);
    void (*unlock)(void);
};
PrefetchRing g_ring = {};

inline void ring_lock(void)   { if (g_ring.lock)   { g_ring.lock(); } }
inline void ring_unlock(void) { if (g_ring.unlock) { g_ring.unlock(); } }

// caller must hold the ring lock
void ring_reset_locked(uint32_t pos)
{
    g_ring.rd = 0;
    g_ring.avail = 0;
    g_ring.file_pos_next = pos;
    g_ring.gen++;
}

void ring_close_producer(void)
{
    if (g_ring.prod_file != nullptr) { fclose(g_ring.prod_file); g_ring.prod_file = nullptr; }
    g_ring.prod_pos = UINT32_MAX;
}

constexpr uint32_t PREFETCH_MAX_FILL_PER_TICK = 32 * 1024;

} // namespace

void msu3dsDataPrefetchInit(uint8_t* storage, uint32_t capacity)
{
    ring_close_producer();
    g_ring.buf = (capacity > 0) ? storage : nullptr;
    g_ring.cap = (storage != nullptr) ? capacity : 0;
    ring_lock();
    ring_reset_locked(0);
    ring_unlock();
}

void msu3dsDataPrefetchLocks(void (*lock)(void), void (*unlock)(void))
{
    g_ring.lock = lock;
    g_ring.unlock = unlock;
}

uint32_t msu3dsDataPrefetchRead(uint32_t pos, uint8_t* dst, uint32_t count)
{
    if (g_ring.buf == nullptr || count == 0) { return 0; }
    ring_lock();
    uint32_t win_start = g_ring.file_pos_next - g_ring.avail;
    uint32_t served = 0;
    if (g_ring.avail != 0 && pos >= win_start && pos < g_ring.file_pos_next) {
        uint32_t skip = pos - win_start;
        g_ring.rd = (g_ring.rd + skip) % g_ring.cap;
        g_ring.avail -= skip;
        served = count;
        if (served > g_ring.avail) { served = g_ring.avail; }
        uint32_t first = g_ring.cap - g_ring.rd;
        if (first > served) { first = served; }
        memcpy(dst, g_ring.buf + g_ring.rd, first);
        if (served > first) { memcpy(dst + first, g_ring.buf, served - first); }
        g_ring.rd = (g_ring.rd + served) % g_ring.cap;
        g_ring.avail -= served;
    } else {
        // miss: re-base the window so the producer prefetches from here on
        ring_reset_locked(pos);
    }
    ring_unlock();
    return served;
}

void msu3dsDataPrefetchFill(void)
{
    if (g_ring.buf == nullptr) { return; }
    if (!MSU1.enabled || MSU1.data_size == 0 || MSU1.base_path[0] == '\0') {
        ring_close_producer();
        return;
    }
    if (g_ring.prod_file == nullptr) {
        char path[MSU1_MAX_BASE_PATH + 8];
        int n = snprintf(path, sizeof(path), "%s.msu", MSU1.base_path);
        if (n <= 0 || (size_t)n >= sizeof(path)) { return; }
        g_ring.prod_file = fopen(path, "rb");
        if (g_ring.prod_file == nullptr) { return; }
        g_ring.prod_pos = UINT32_MAX;
    }

    ring_lock();
    uint32_t my_gen = g_ring.gen;
    uint32_t target = g_ring.file_pos_next;
    uint32_t space  = g_ring.cap - g_ring.avail;
    uint32_t wr     = (g_ring.rd + g_ring.avail) % g_ring.cap;
    ring_unlock();

    if (space == 0 || target >= MSU1.data_size) { return; }
    uint32_t want = MSU1.data_size - target;
    if (want > space) { want = space; }
    if (want > PREFETCH_MAX_FILL_PER_TICK) { want = PREFETCH_MAX_FILL_PER_TICK; }
    uint32_t span = g_ring.cap - wr;              // one contiguous span per tick
    if (want > span) { want = span; }

    if (g_ring.prod_pos != target) {
        if (fseek(g_ring.prod_file, (long)target, SEEK_SET) != 0) { return; }
        g_ring.prod_pos = target;
    }
    // fread OUTSIDE the lock: the free region belongs to the producer unless
    // the generation changes, in which case the bytes are discarded below.
    size_t got = fread(g_ring.buf + wr, 1, want, g_ring.prod_file);
    g_ring.prod_pos += (uint32_t)got;

    ring_lock();
    if (g_ring.gen == my_gen && got > 0) {
        g_ring.avail += (uint32_t)got;
        g_ring.file_pos_next += (uint32_t)got;
    }
    ring_unlock();
}

bool msu3dsInitialize(const Msu1AudioBackend& backend,
                      int16_t* staging, uint32_t staging_samples)
{
    if (backend.init_channel == nullptr || backend.set_mix == nullptr
     || backend.queue_buffer == nullptr || backend.free_buffer_count == nullptr
     || backend.total_buffer_count == nullptr || backend.buffer_capacity_samples == nullptr
     || backend.clear_queue == nullptr || backend.shutdown_channel == nullptr) {
        return false;
    }
    if (staging == nullptr) { return false; }
    if (staging_samples < backend.buffer_capacity_samples()) { return false; }
    if (!backend.init_channel(MSU1_SAMPLE_RATE)) { return false; }
    reset_bridge();
    g_bridge.initialized     = true;
    g_bridge.backend         = backend;
    g_bridge.staging         = staging;
    g_bridge.staging_samples = staging_samples;
    g_bridge.global_volume   = 1.0f;
    g_bridge.user_volume     = 1.0f;
    MSU1.volume_changed_cb   = bridge_volume_cb;
    msu1_set_data_prefetch(msu3dsDataPrefetchRead);
    apply_mix();
    return true;
}

void msu3dsFinalize(void)
{
    ring_close_producer();
    if (!g_bridge.initialized) { return; }
    g_bridge.backend.clear_queue();
    g_bridge.backend.shutdown_channel();
    MSU1.volume_changed_cb = nullptr;
    reset_bridge();
}

void msu3dsSetUserVolume(float factor)
{
    if (!g_bridge.initialized) { return; }
    if (factor < 0.0f) { factor = 0.0f; }
    if (factor > 2.0f) { factor = 2.0f; }
    g_bridge.user_volume = factor;
    apply_mix();
}

void msu3dsSetGlobalVolume(float factor)
{
    // Callers may run before install (snd3dsInitialize applies the volume
    // before msu3dsNdspInstall); settings3dsUpdate re-applies it after
    // install on every ROM load/resume, so dropping this call is safe.
    if (!g_bridge.initialized) { return; }
    if (factor < 0.0f) { factor = 0.0f; }
    if (factor > 2.0f) { factor = 2.0f; }
    g_bridge.global_volume = factor;
    apply_mix();
}

uint32_t msu3dsGetUnderrunCount(void) { return g_bridge.underruns; }
bool     msu3dsIsMuted(void)
{ return g_bridge.menu_muted || g_bridge.turbo_muted || g_bridge.apt_muted; }

void msu3dsFillAudio(void)
{
    if (!g_bridge.initialized) { return; }
    // Drain contract: queue NOTHING — the channel playing out its already
    // queued buffers IS the drained state. All emu-thread clear_queue events
    // (RomUnload / SavestateLoaded) execute inside drain windows, so never
    // concurrently with a backend queue_buffer call.
    if (g_bridge.drain_active) { return; }
    msu3dsDataPrefetchFill();
    if (!MSU1.enabled) { return; }        // zero-cost path when no MSU-1 game is loaded
    const bool playing = (MSU1.status & MSU1_FLAG_AUDIO_PLAYING) != 0;
    // Menu/APT pause must FREEZE the track position (spec section 6 row 1):
    // keep the channel fed with silence but consume no PCM. Turbo only mutes
    // the mix and keeps consuming (fast-forward drift is inherent).
    const bool frozen = g_bridge.menu_muted || g_bridge.apt_muted;
    if (playing && !frozen && g_bridge.queued_since_clear
        && g_bridge.backend.free_buffer_count() == g_bridge.backend.total_buffer_count()) {
        g_bridge.underruns++;
        msu1_diag("audio underrun #%u (track %u)",
                  (unsigned)g_bridge.underruns, (unsigned)MSU1.current_track);
    }
    while (g_bridge.backend.free_buffer_count() > 0) {
        uint32_t cap_samples = g_bridge.backend.buffer_capacity_samples();
        uint32_t cap_bytes   = cap_samples * MSU1_BYTES_PER_SAMPLE;
        uint32_t got = 0;
        if (playing && !frozen) {
            got = msu1_read_audio(MSU1, (uint8_t*)g_bridge.staging, cap_bytes);
        }
        if (got < cap_bytes) {
            memset((uint8_t*)g_bridge.staging + got, 0, cap_bytes - got);
        }
        if (!g_bridge.backend.queue_buffer(g_bridge.staging, cap_samples)) { break; }
        if (got > 0) { g_bridge.queued_since_clear = true; }
    }
}

void msu3dsOnEvent(Msu1Event event)
{
    if (!g_bridge.initialized) { return; }
    switch (event) {
        case Msu1Event::MenuEnter:   g_bridge.menu_muted = true;   apply_mix(); return;
        case Msu1Event::MenuExit:    g_bridge.menu_muted = false;  apply_mix(); return;
        case Msu1Event::MixerDrain:  g_bridge.drain_active = true;              return;
        case Msu1Event::MixerResume: g_bridge.drain_active = false;             return;
        case Msu1Event::TurboOn:     g_bridge.turbo_muted = true;  apply_mix(); return;
        case Msu1Event::TurboOff:    g_bridge.turbo_muted = false; apply_mix(); return;
        case Msu1Event::AptSuspend:  g_bridge.apt_muted = true;    apply_mix(); return;
        case Msu1Event::AptResume:   g_bridge.apt_muted = false;   apply_mix(); return;
        case Msu1Event::RomUnload:
            g_bridge.backend.clear_queue();
            g_bridge.queued_since_clear = false;
            ring_close_producer();
            ring_lock(); ring_reset_locked(0); ring_unlock();
            S9xMSU1Shutdown();
            apply_mix();
            return;
        case Msu1Event::SavestateLoaded:
            g_bridge.backend.clear_queue();
            g_bridge.queued_since_clear = false;
            ring_lock(); ring_reset_locked(MSU1.data_pos); ring_unlock();
            apply_mix();
            return;
        case Msu1Event::VolumeChanged: apply_mix(); return;
        case Msu1Event::ConsoleReset:
            g_bridge.backend.clear_queue();
            g_bridge.queued_since_clear = false;
            ring_lock(); ring_reset_locked(0); ring_unlock();
            msu1_soft_reset(MSU1);
            apply_mix();
            return;
        case Msu1Event::AppExit:       msu3dsFinalize(); return;
        case Msu1Event::Count:         return;
    }
}
