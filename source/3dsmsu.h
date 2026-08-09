#pragma once
#include <cstdint>
#include <cstddef>
#include "Snes9x/msu1.h"

inline constexpr uint32_t MSU1_STUTTER_MINOR_THRESHOLD  = 1;
inline constexpr uint32_t MSU1_STUTTER_SEVERE_THRESHOLD = 6;

enum class Msu1Event : uint8_t {
    MenuEnter, MenuExit, MixerDrain, MixerResume, TurboOn, TurboOff,
    AptSuspend, AptResume, RomUnload, SavestateLoaded, VolumeChanged, AppExit,
    ConsoleReset,
    Count   // sentinel — keep last; the matrix test iterates up to here
};

struct Msu1AudioBackend {
    bool     (*init_channel)(uint32_t sample_rate);
    void     (*set_mix)(float volume);                       // 0.0 .. 2.0
    bool     (*queue_buffer)(const int16_t* samples, uint32_t sample_count);
    uint32_t (*free_buffer_count)(void);
    uint32_t (*total_buffer_count)(void);
    uint32_t (*buffer_capacity_samples)(void);               // samples per buffer
    void     (*clear_queue)(void);
    void     (*shutdown_channel)(void);
};

bool     msu3dsInitialize(const Msu1AudioBackend& backend,
                          int16_t* staging, uint32_t staging_samples);
void     msu3dsFinalize(void);
void     msu3dsOnEvent(Msu1Event event);
void     msu3dsSetGlobalVolume(float factor);   // 1.0 + setting*0.25 (same curve as ch 0)
void     msu3dsSetUserVolume(float factor);     // user volume multiplier [0.0, 2.0]
void     msu3dsFillAudio(void);                 // mixing thread, under snesAccessLock
uint32_t msu3dsGetUnderrunCount(void);
bool     msu3dsIsMuted(void);                   // exposed for tests/diagnostics

// Decides what a change to the per-game Msu1Enabled setting must do to the
// live chip, given the current Settings.MSU1 truth. Pure decision table —
// no I/O, no side effects; the platform layer executes the returned action.
enum class Msu1EnableAction : uint8_t { None, TearDown, Detect };
Msu1EnableAction msu3dsDecideEnableAction(bool setting_enabled, bool chip_active);

// Formats the menu status line (+ optional warning subtitle) from chip state.
// line/subtitle are always NUL-terminated on success; subtitle becomes ""
// when there is no warning. Returns false on invalid args (null/zero sizes).
bool msu3dsFormatStatus(bool msu_present, const Msu1State& state,
                        uint32_t underruns,
                        char* line, size_t line_size,
                        char* subtitle, size_t subtitle_size);
