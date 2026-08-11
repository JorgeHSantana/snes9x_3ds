#pragma once
#include <cstdint>
#include <cstdio>
#include <cstddef>

inline constexpr uint32_t MSU1_PCM_HEADER_SIZE  = 8;
inline constexpr uint32_t MSU1_PCM_MAGIC        = 0x3155534D; // "MSU1" as u32-LE
inline constexpr uint8_t  MSU1_REVISION         = 0x01;
inline constexpr size_t   MSU1_MAX_BASE_PATH    = 768;
inline constexpr uint32_t MSU1_AUDIO_STALL_LIMIT = 64;   // consecutive failed reads (~1s of fills) before a track stops
inline constexpr uint32_t MSU1_BYTES_PER_SAMPLE = 4;          // stereo s16
inline constexpr uint32_t MSU1_SAMPLE_RATE      = 44100;

inline constexpr uint8_t MSU1_FLAG_AUDIO_ERROR   = 0x08;
inline constexpr uint8_t MSU1_FLAG_AUDIO_PLAYING = 0x10;
inline constexpr uint8_t MSU1_FLAG_AUDIO_REPEAT  = 0x20;
inline constexpr uint8_t MSU1_FLAG_AUDIO_BUSY    = 0x40;
inline constexpr uint8_t MSU1_FLAG_DATA_BUSY     = 0x80;

enum class Msu1Result : uint8_t { Ok, InvalidParam, FileMissing, BadHeader, IoError };

struct Msu1PcmHeader { uint32_t loop_point; };

struct Msu1Snapshot {
    uint8_t  status;
    uint8_t  volume;
    uint16_t current_track;
    uint16_t resume_track;
    uint32_t data_pos;
    uint32_t audio_play_pos;   // bytes past header
    uint32_t resume_pos;
};

struct Msu1State {
    // Written by the emulation thread. status/volume/audio_* also read by the
    // mixing thread — always under snesAccessLock (see 3dssound.cpp).
    bool     enabled;
    uint8_t  status;
    uint8_t  volume;
    uint16_t current_track;
    uint16_t track_latch;
    uint32_t data_seek_latch;
    uint32_t data_pos;
    uint32_t data_size;
    uint32_t audio_loop_point;   // samples
    uint32_t audio_play_pos;     // bytes past header
    uint32_t audio_size;         // bytes past header
    uint16_t resume_track;
    uint32_t resume_pos;
    FILE*    data_file;          // owned by this struct; nullptr when absent
    uint32_t audio_read_stalls;  // consecutive zero-byte audio freads (runtime only)
    uint32_t data_file_pos;      // actual FILE position of data_file (runtime only)
    FILE*    audio_file;         // owned by this struct; nullptr when absent
    char     base_path[MSU1_MAX_BASE_PATH];
    void     (*volume_changed_cb)(void);   // optional; installed by the platform bridge
};

extern Msu1State MSU1;

// pure helpers
bool msu1_parse_pcm_header(FILE* file, Msu1PcmHeader& out);
bool msu1_build_base_path(const char* rom_path, char* out, size_t out_size);
bool msu1_build_track_path(const char* base_path, uint16_t track, char* out, size_t out_size);
bool msu1_is_port_address(uint16_t address);
bool msu1_is_dma_source(uint8_t a_bank, uint16_t a_address, bool a_fixed);

// lifecycle
bool       msu1_detect(const char* rom_path);
Msu1Result msu1_init(Msu1State& state, const char* rom_path);
void       msu1_shutdown(Msu1State& state);

// Console reset (spec section 6 row 6): stops playback and clears chip
// registers/latches like a power cycle, but keeps the data file open, the
// chip enabled and the game volume — the same ROM keeps running.
void msu1_soft_reset(Msu1State& state);

// register interface (emulation thread)
uint8_t msu1_read_port(Msu1State& state, uint8_t port);

// Reads up to count bytes from the data track at data_pos with one fread.
// Returns bytes actually read (short at EOF, 0 when disabled or no data
// file); advances data_pos by the return value. No status flags change.
uint32_t msu1_read_data_bulk(Msu1State& state, uint8_t* dst, uint32_t count);

// B-bus register offset of the i-th byte of a DMA transfer for a given
// transfer mode (0-7; 5-7 mirror 1-3). Matches the SNES DMA write patterns.
uint8_t msu1_dma_b_offset(uint8_t transfer_mode, uint32_t byte_index);
void    msu1_write_port(Msu1State& state, uint8_t port, uint8_t value);

// audio pull (mixing thread, under snesAccessLock); returns bytes written
uint32_t msu1_read_audio(Msu1State& state, uint8_t* out, uint32_t max_bytes);

// savestate
void       msu1_capture(const Msu1State& state, Msu1Snapshot& out);
Msu1Result msu1_restore(Msu1State& state, const Msu1Snapshot& snap);

// Cross-thread lock hooks (installed once by the platform, both or neither).
// S9xMSU1WritePort takes them around writes that mutate files/status the
// mixing thread reads concurrently (ports 3/5/6/7); msu1_restore takes them
// around the whole restore. Null hooks (the default, and the host-test
// configuration) make every take/release a no-op.
// LOCK ORDERING: the hooks acquire snd3DS.snesAccessLock — they must never be
// invoked from code already holding it. The mixing thread holds that lock in
// snd3dsMixSamples but only ever calls msu1_read_audio, never these paths.
void msu1_set_lock_hooks(void (*lock)(void), void (*unlock)(void));

// Optional diagnostics hook (default null = silent). The core reports
// track-load outcomes through it; the platform forwards to its logger.
void msu1_set_log_hook(void (*log)(const char* message));

// Formats and forwards to the log hook (no-op when the hook is null).
// Public so platform-side MSU code can share the same diagnostics channel.
void msu1_diag(const char* fmt, ...);

// Optional data-track prefetch source (default null). When set, bulk reads
// try it first at the requested file offset; any shortfall falls back to a
// direct (position-corrected) fread. Installed by the platform bridge.
void msu1_set_data_prefetch(uint32_t (*read)(uint32_t pos, uint8_t* dst, uint32_t count));

// FMV tear tracking: large VRAM DMAs landing inside the visible frame mark
// it torn; the platform holds the previous presented frame instead of
// showing a half-uploaded video frame (blink/pink-tile suppression).
void msu1_mark_frame_torn(void);
bool msu1_consume_frame_torn(void);
void     msu1_note_visible_vram_write(void);   // CPU-store uploads (no DMA)
uint32_t msu1_take_visible_vram_writes(void);  // read + reset, once per frame

// legacy-boundary wrappers (operate on the global MSU1)
uint8_t S9xMSU1ReadPort(uint8_t port);
void    S9xMSU1WritePort(uint8_t port, uint8_t value);
void    S9xMSU1Shutdown(void);
