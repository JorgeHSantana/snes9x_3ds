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
void     msu3dsSetGlobalVolume(float factor);   // MSU-1 Volume gauge, v*0.25 (independent of SNES volume)
void     msu3dsSetUserVolume(float factor);     // MSU-1 balance gauge, v*0.25 (4 = same as SNES)

// Data-track read-ahead (phase B). Storage is caller-owned; null/0 disables.
// Locks are leaf hooks (LightLock on 3DS, null = no-op on host tests).
// Fill runs on the mixing thread; Read is the msu1 core's prefetch source.
void     msu3dsDataPrefetchInit(uint8_t* storage, uint32_t capacity);
void     msu3dsDataPrefetchLocks(void (*lock)(void), void (*unlock)(void));
void     msu3dsDataPrefetchFill(void);
uint32_t msu3dsDataPrefetchRead(uint32_t pos, uint8_t* dst, uint32_t count);
void     msu3dsFillAudio(void);                 // mixing thread, under snesAccessLock

// Decoded-audio read-ahead (issue #55). Storage is caller-owned; null/0
// disables and leaves the core on its direct-decode path. Init installs the
// msu1 audio prefetch + notify hooks; Stop uninstalls them and closes the
// producer's decoder. Tick is the producer: it adopts stream changes and
// decodes into the ring - on 3DS it runs on its own thread (below the
// mixer's priority) so loop seeks never stall mixing; host tests call it
// directly. Locks are leaf hooks like the data ring's.
void     msu3dsAudioReadaheadInit(uint8_t* storage, uint32_t capacity);
void     msu3dsAudioReadaheadLocks(void (*lock)(void), void (*unlock)(void));
void     msu3dsAudioReadaheadTick(void);
void     msu3dsAudioReadaheadStop(void);
uint32_t msu3dsAudioReadaheadRead(uint32_t pos, uint8_t* dst, uint32_t count, bool* alive);
uint32_t msu3dsGetUnderrunCount(void);
bool     msu3dsIsMuted(void);                   // exposed for tests/diagnostics
