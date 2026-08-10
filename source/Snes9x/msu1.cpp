#include <stdarg.h>
#include "msu1.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

Msu1State MSU1 = {};

// Cross-thread lock hooks (see msu1.h for the contract and lock ordering).
static void (*s_lock_hook)(void)   = nullptr;
static void (*s_unlock_hook)(void) = nullptr;

void msu1_set_lock_hooks(void (*lock)(void), void (*unlock)(void))
{
    // a half-installed pair would unbalance the lock: install both or neither
    if ((lock == nullptr) != (unlock == nullptr)) { return; }
    s_lock_hook   = lock;
    s_unlock_hook = unlock;
}

static void msu1_lock(void)   { if (s_lock_hook   != nullptr) { s_lock_hook(); } }
static void msu1_unlock(void) { if (s_unlock_hook != nullptr) { s_unlock_hook(); } }

bool msu1_build_base_path(const char* rom_path, char* out, size_t out_size)
{
    if (rom_path == nullptr || out == nullptr || out_size == 0) { return false; }
    size_t len = strlen(rom_path);
    if (len == 0 || len >= out_size) { return false; }
    // strip the last extension of the basename only
    size_t cut = len;
    for (size_t i = len; i > 0; i--) {
        char c = rom_path[i - 1];
        if (c == '/' || c == '\\') { break; }
        if (c == '.') { cut = i - 1; break; }
    }
    memcpy(out, rom_path, cut);
    out[cut] = '\0';
    return true;
}

bool msu1_build_track_path(const char* base_path, uint16_t track,
                           char* out, size_t out_size)
{
    if (base_path == nullptr || out == nullptr || out_size == 0) { return false; }
    int written = snprintf(out, out_size, "%s-%u.pcm", base_path, (unsigned)track);
    return written > 0 && (size_t)written < out_size;
}

bool msu1_parse_pcm_header(FILE* file, Msu1PcmHeader& out)
{
    if (file == nullptr) { return false; }
    uint8_t raw[MSU1_PCM_HEADER_SIZE];
    if (fseek(file, 0, SEEK_SET) != 0) { return false; }
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) { return false; }
    uint32_t magic = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8)
                   | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    if (magic != MSU1_PCM_MAGIC) { return false; }
    out.loop_point = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8)
                   | ((uint32_t)raw[6] << 16) | ((uint32_t)raw[7] << 24);
    return true;
}

bool msu1_is_port_address(uint16_t address)
{
    return address >= 0x2000 && address <= 0x2007;
}

bool msu1_is_dma_source(uint8_t a_bank, uint16_t a_address, bool a_fixed)
{
    if (!a_fixed) { return false; }
    bool system_bank = (a_bank <= 0x3F) || (a_bank >= 0x80 && a_bank <= 0xBF);
    return system_bank && msu1_is_port_address(a_address);
}

bool msu1_detect(const char* rom_path)
{
    if (rom_path == nullptr) { return false; }
    char base[MSU1_MAX_BASE_PATH];
    char msu_path[MSU1_MAX_BASE_PATH + 8];
    if (!msu1_build_base_path(rom_path, base, sizeof(base))) { return false; }
    int n = snprintf(msu_path, sizeof(msu_path), "%s.msu", base);
    if (n <= 0 || (size_t)n >= sizeof(msu_path)) { return false; }
    FILE* f = fopen(msu_path, "rb");
    if (f == nullptr) { return false; }
    fclose(f);
    return true;
}

Msu1Result msu1_init(Msu1State& state, const char* rom_path)
{
    msu1_shutdown(state);
    if (rom_path == nullptr) { return Msu1Result::InvalidParam; }
    if (!msu1_build_base_path(rom_path, state.base_path, sizeof(state.base_path))) {
        return Msu1Result::InvalidParam;
    }
    char msu_path[MSU1_MAX_BASE_PATH + 8];
    int n = snprintf(msu_path, sizeof(msu_path), "%s.msu", state.base_path);
    if (n <= 0 || (size_t)n >= sizeof(msu_path)) { return Msu1Result::InvalidParam; }
    state.data_file = fopen(msu_path, "rb");
    if (state.data_file == nullptr) {
        state.base_path[0] = '\0';
        return Msu1Result::FileMissing;
    }
    if (fseek(state.data_file, 0, SEEK_END) != 0) {
        msu1_shutdown(state);
        return Msu1Result::IoError;
    }
    long size = ftell(state.data_file);
    if (size < 0) {
        msu1_shutdown(state);
        return Msu1Result::IoError;
    }
    state.data_size = (uint32_t)size;
    fseek(state.data_file, 0, SEEK_SET);
    state.data_file_pos = 0;
    state.enabled = true;
    state.status  = MSU1_REVISION;
    state.volume  = 0;
    return Msu1Result::Ok;
}

void msu1_shutdown(Msu1State& state)
{
    if (state.data_file != nullptr)  { fclose(state.data_file); }
    if (state.audio_file != nullptr) { fclose(state.audio_file); }
    void (*saved_cb)(void) = state.volume_changed_cb;   // survives ROM switches
    state = Msu1State{};
    state.volume_changed_cb = saved_cb;
}

void msu1_soft_reset(Msu1State& state)
{
    if (state.audio_file != nullptr) { fclose(state.audio_file); state.audio_file = nullptr; }
    state.status           = state.enabled ? MSU1_REVISION : 0;
    state.current_track    = 0;
    state.track_latch      = 0;
    state.data_seek_latch  = 0;
    state.audio_play_pos   = 0;
    state.audio_size       = 0;
    state.audio_loop_point = 0;
    state.resume_track     = 0;
    state.resume_pos       = 0;
    if (state.data_file != nullptr) { fseek(state.data_file, 0, SEEK_SET); state.data_file_pos = 0; }
    state.data_pos = 0;
    // keeps: data_file, data_size, enabled, base_path, volume, volume_changed_cb
}

static const uint8_t MSU1_ID[6] = { 'S', '-', 'M', 'S', 'U', '1' };

static void (*g_log_hook)(const char*) = nullptr;
static uint32_t (*g_data_prefetch)(uint32_t, uint8_t*, uint32_t) = nullptr;
void msu1_set_data_prefetch(uint32_t (*read)(uint32_t pos, uint8_t* dst, uint32_t count))
{ g_data_prefetch = read; }
void msu1_set_log_hook(void (*log)(const char* message)) { g_log_hook = log; }

void msu1_diag(const char* fmt, ...)
{
    if (g_log_hook == nullptr) { return; }
    char buf[MSU1_MAX_BASE_PATH + 64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_log_hook(buf);
}

static void msu1_load_track(Msu1State& state, uint16_t track)
{
    if (state.audio_file != nullptr) { fclose(state.audio_file); state.audio_file = nullptr; }
    state.status &= (uint8_t)~(MSU1_FLAG_AUDIO_PLAYING | MSU1_FLAG_AUDIO_REPEAT
                             | MSU1_FLAG_AUDIO_ERROR);
    state.current_track   = track;
    state.audio_play_pos  = 0;
    state.audio_size      = 0;
    state.audio_loop_point = 0;

    char path[MSU1_MAX_BASE_PATH + 16];
    if (!msu1_build_track_path(state.base_path, track, path, sizeof(path))) {
        state.status |= MSU1_FLAG_AUDIO_ERROR;
        msu1_diag("track %u: path build failed (base too long)", (unsigned)track);
        return;
    }
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        state.status |= MSU1_FLAG_AUDIO_ERROR;
        msu1_diag("track %u: fopen failed: %s", (unsigned)track, path);
        return;
    }
    Msu1PcmHeader hdr = {};
    if (!msu1_parse_pcm_header(f, hdr)) {
        fclose(f);
        state.status |= MSU1_FLAG_AUDIO_ERROR;
        msu1_diag("track %u: bad MSU1 PCM header: %s", (unsigned)track, path);
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); state.status |= MSU1_FLAG_AUDIO_ERROR; return; }
    long end = ftell(f);
    if (end < (long)MSU1_PCM_HEADER_SIZE) { fclose(f); state.status |= MSU1_FLAG_AUDIO_ERROR; return; }
    state.audio_size      = (uint32_t)end - MSU1_PCM_HEADER_SIZE;
    state.audio_loop_point = hdr.loop_point;
    fseek(f, (long)MSU1_PCM_HEADER_SIZE, SEEK_SET);
    state.audio_file = f;
    msu1_diag("track %u: loaded, %u bytes, loop=%u", (unsigned)track,
             (unsigned)state.audio_size, (unsigned)state.audio_loop_point);
}

uint8_t msu1_read_port(Msu1State& state, uint8_t port)
{
    if (port > 7) { return 0x00; }
    if (!state.enabled) { return 0x00; }
    switch (port) {
        case 0: return state.status;
        case 1: {
            uint8_t b = 0;
            return (msu1_read_data_bulk(state, &b, 1) == 1) ? b : 0x00;
        }
        default: return MSU1_ID[port - 2];
    }
}

uint32_t msu1_read_data_bulk(Msu1State& state, uint8_t* dst, uint32_t count)
{
    if (!state.enabled) { return 0; }
    if (state.data_file == nullptr) { return 0; }
    if (state.data_pos >= state.data_size) { return 0; }
    uint32_t remaining = state.data_size - state.data_pos;
    if (count > remaining) { count = remaining; }

    uint32_t served = 0;
    if (g_data_prefetch != nullptr) {
        served = g_data_prefetch(state.data_pos, dst, count);
        state.data_pos += served;               // FILE* position now lags data_pos
        if (served == count) { return served; }
    }
    if (state.data_file_pos != state.data_pos) {
        if (fseek(state.data_file, (long)state.data_pos, SEEK_SET) != 0) { return served; }
        state.data_file_pos = state.data_pos;
    }
    size_t got = fread(dst + served, 1, count - served, state.data_file);
    state.data_pos      += (uint32_t)got;
    state.data_file_pos += (uint32_t)got;
    return served + (uint32_t)got;
}

uint8_t msu1_dma_b_offset(uint8_t transfer_mode, uint32_t byte_index)
{
    switch (transfer_mode & 0x7) {
        case 1: case 5: return (uint8_t)(byte_index & 1);
        case 3: case 7: return (uint8_t)((byte_index >> 1) & 1);
        case 4:         return (uint8_t)(byte_index & 3);
        default:        return 0;   // modes 0, 2, 6 write one register
    }
}

void msu1_write_port(Msu1State& state, uint8_t port, uint8_t value)
{
    if (port > 7 || !state.enabled) { return; }
    switch (port) {
        case 0: state.data_seek_latch = (state.data_seek_latch & 0xFFFFFF00u) | value; return;
        case 1: state.data_seek_latch = (state.data_seek_latch & 0xFFFF00FFu) | ((uint32_t)value << 8); return;
        case 2: state.data_seek_latch = (state.data_seek_latch & 0xFF00FFFFu) | ((uint32_t)value << 16); return;
        case 3: {
            state.data_seek_latch = (state.data_seek_latch & 0x00FFFFFFu) | ((uint32_t)value << 24);
            uint32_t target = state.data_seek_latch;
            if (target > state.data_size) { target = state.data_size; }  // clamp
            if (state.data_file != nullptr && fseek(state.data_file, (long)target, SEEK_SET) == 0) {
                state.data_pos = target;
                state.data_file_pos = target;
            }
            return;
        }
        case 4: state.track_latch = (uint16_t)((state.track_latch & 0xFF00u) | value); return;
        case 5:
            state.track_latch = (uint16_t)((state.track_latch & 0x00FFu) | ((uint16_t)value << 8));
            msu1_load_track(state, state.track_latch);
            return;
        case 6:
            if ((state.volume == 0) != (value == 0)) {
                msu1_diag("volume %s (%u)", value ? "unmuted" : "muted to 0", (unsigned)value);
            }
            state.volume = value;
            if (state.volume_changed_cb != nullptr) { state.volume_changed_cb(); }
            return;
        case 7: {
            if ((state.status & (MSU1_FLAG_AUDIO_BUSY | MSU1_FLAG_AUDIO_ERROR)) != 0) {
                msu1_diag("control write %02X DROPPED (status=%02X busy/error)",
                         (unsigned)value, (unsigned)state.status);
                return;
            }
            if (state.audio_file == nullptr) {
                msu1_diag("control write %02X DROPPED (no audio file)", (unsigned)value);
                return;
            }
            msu1_diag("control write %02X (track %u, status=%02X, volume=%u)",
                     (unsigned)value, (unsigned)state.current_track,
                     (unsigned)state.status, (unsigned)state.volume);
            bool was_playing = (state.status & MSU1_FLAG_AUDIO_PLAYING) != 0;
            state.status &= (uint8_t)~(MSU1_FLAG_AUDIO_PLAYING | MSU1_FLAG_AUDIO_REPEAT);
            if ((value & 0x02) != 0) { state.status |= MSU1_FLAG_AUDIO_REPEAT; }
            if ((value & 0x01) == 0) {
                // pause: remember where this track stopped. The stored resume
                // persists until the next pause overwrites it (a track load
                // does NOT clear it), so pause -> load other -> reload -> resume
                // still works; a plain play consumes nothing and just ignores it.
                if (was_playing) {
                    state.resume_track = state.current_track;
                    state.resume_pos   = state.audio_play_pos;
                }
                return;
            }
            // play: bit 2 resumes the stored position when it belongs to this
            // track and is still inside the file; otherwise play from the start.
            uint32_t start_pos = 0;
            if ((value & 0x04) != 0
                && state.resume_track == state.current_track
                && state.resume_pos <= state.audio_size) {
                start_pos = state.resume_pos;
            }
            if (fseek(state.audio_file,
                      (long)(MSU1_PCM_HEADER_SIZE + start_pos), SEEK_SET) == 0) {
                state.audio_play_pos = start_pos;
                state.status |= MSU1_FLAG_AUDIO_PLAYING;
            }
            return;
        }
        default: return;
    }
}

uint32_t msu1_read_audio(Msu1State& state, uint8_t* out, uint32_t max_bytes)
{
    if (out == nullptr || max_bytes == 0) { return 0; }
    if (!state.enabled || state.audio_file == nullptr) { return 0; }
    if ((state.status & MSU1_FLAG_AUDIO_PLAYING) == 0) { return 0; }

    uint32_t written = 0;
    while (written < max_bytes) {
        uint32_t remaining_in_track = state.audio_size - state.audio_play_pos;
        if (remaining_in_track == 0) {
            if ((state.status & MSU1_FLAG_AUDIO_REPEAT) != 0) {
                if (state.audio_size == 0) {
                    state.status &= (uint8_t)~MSU1_FLAG_AUDIO_PLAYING;
                    break;
                }
                uint32_t loop_bytes = state.audio_loop_point * MSU1_BYTES_PER_SAMPLE;
                if (loop_bytes >= state.audio_size) { loop_bytes = 0; }   // defensive clamp
                if (fseek(state.audio_file,
                          (long)(MSU1_PCM_HEADER_SIZE + loop_bytes), SEEK_SET) != 0) {
                    state.status &= (uint8_t)~MSU1_FLAG_AUDIO_PLAYING;
                    break;
                }
                state.audio_play_pos = loop_bytes;
                continue;
            }
            state.status &= (uint8_t)~MSU1_FLAG_AUDIO_PLAYING;
            msu1_diag("track %u ended (played to end)", (unsigned)state.current_track);
            break;
        }
        uint32_t chunk = max_bytes - written;
        if (chunk > remaining_in_track) { chunk = remaining_in_track; }
        size_t got = fread(out + written, 1, chunk, state.audio_file);
        if (got == 0) {
            // Transient read failure (SD latency spike / hiccup): keep the
            // track alive, resync the stream, and let the caller pad silence.
            // Only a persistent failure stops playback.
            state.audio_read_stalls++;
            clearerr(state.audio_file);
            fseek(state.audio_file,
                  (long)(MSU1_PCM_HEADER_SIZE + state.audio_play_pos), SEEK_SET);
            if (state.audio_read_stalls == 1) {
                msu1_diag("track %u: audio read stall at %u/%u",
                          (unsigned)state.current_track,
                          (unsigned)state.audio_play_pos, (unsigned)state.audio_size);
            }
            if (state.audio_read_stalls > MSU1_AUDIO_STALL_LIMIT) {
                state.status &= (uint8_t)~MSU1_FLAG_AUDIO_PLAYING;
                msu1_diag("track %u: giving up after %u stalled reads",
                          (unsigned)state.current_track, (unsigned)state.audio_read_stalls);
            }
            break;
        }
        state.audio_read_stalls = 0;
        state.audio_play_pos += (uint32_t)got;
        written += (uint32_t)got;
    }
    return written;
}

void msu1_capture(const Msu1State& state, Msu1Snapshot& out)
{
    out.status         = state.status;
    out.volume         = state.volume;
    out.current_track  = state.current_track;
    out.resume_track   = state.resume_track;
    out.data_pos       = state.data_pos;
    out.audio_play_pos = state.audio_play_pos;
    out.resume_pos     = state.resume_pos;
}

static Msu1Result msu1_restore_locked(Msu1State& state, const Msu1Snapshot& snap)
{
    if (!state.enabled) { return Msu1Result::InvalidParam; }   // init first
    state.volume       = snap.volume;
    state.resume_track = snap.resume_track;
    state.resume_pos   = snap.resume_pos;

    // data stream
    uint32_t dpos = snap.data_pos;
    if (dpos > state.data_size) { dpos = state.data_size; }
    if (state.data_file != nullptr && fseek(state.data_file, (long)dpos, SEEK_SET) == 0) {
        state.data_pos = dpos;
        state.data_file_pos = dpos;
    }

    // audio stream: reload the saved track from disk
    msu1_load_track(state, snap.current_track);
    if ((state.status & MSU1_FLAG_AUDIO_ERROR) != 0) {
        return Msu1Result::FileMissing;
    }
    if (snap.audio_play_pos <= state.audio_size
        && fseek(state.audio_file,
                 (long)(MSU1_PCM_HEADER_SIZE + snap.audio_play_pos), SEEK_SET) == 0) {
        state.audio_play_pos = snap.audio_play_pos;
        state.status |= (uint8_t)(snap.status
                        & (MSU1_FLAG_AUDIO_PLAYING | MSU1_FLAG_AUDIO_REPEAT));
    }
    // invalid position: track stays loaded but stopped (defensive)
    if (state.volume_changed_cb != nullptr) { state.volume_changed_cb(); }
    return Msu1Result::Ok;
}

Msu1Result msu1_restore(Msu1State& state, const Msu1Snapshot& snap)
{
    // Restore closes/reopens/seeks files the mixing thread may be reading;
    // fence it with the platform lock hooks (no-ops on host, see msu1.h).
    msu1_lock();
    Msu1Result result = msu1_restore_locked(state, snap);
    msu1_unlock();
    return result;
}

uint8_t S9xMSU1ReadPort(uint8_t port)               { return msu1_read_port(MSU1, port); }

void S9xMSU1WritePort(uint8_t port, uint8_t value)
{
    // Ports 3 (data fseek), 5 (track fclose/fopen), 6 (volume the mixer
    // mixes with) and 7 (status RMW the mixer also RMWs) race the mixing
    // thread; ports 0-2 and 4 only mutate emu-thread-only latch bytes.
    // msu1_write_port itself stays hook-free so host tests drive it directly.
    bool needs_lock = (port == 3) || (port >= 5 && port <= 7);
    if (needs_lock) { msu1_lock(); }
    msu1_write_port(MSU1, port, value);
    if (needs_lock) { msu1_unlock(); }
}

void S9xMSU1Shutdown(void) { msu1_shutdown(MSU1); }
