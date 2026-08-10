#pragma once
#include <cstdint>
#include "Snes9x/msu1.h"

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
void     msu3dsSetGlobalVolume(float factor);
void     msu3dsSetUserVolume(float factor);     // per-game/global MSU-1 volume, 1.0 + setting*0.25   // 1.0 + setting*0.25 (same curve as ch 0)
void     msu3dsFillAudio(void);                 // mixing thread, under snesAccessLock
uint32_t msu3dsGetUnderrunCount(void);
bool     msu3dsIsMuted(void);                   // exposed for tests/diagnostics
