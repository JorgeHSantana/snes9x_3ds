#include "3dsmsu.h"
#include <atomic>
#include <cstring>
#include <cstdio>

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
    apply_mix();
    return true;
}

void msu3dsFinalize(void)
{
    if (!g_bridge.initialized) { return; }
    g_bridge.backend.clear_queue();
    g_bridge.backend.shutdown_channel();
    MSU1.volume_changed_cb = nullptr;
    reset_bridge();
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

void msu3dsSetUserVolume(float factor)
{
    // User volume multiplier can be set at any time; if not yet initialized,
    // the value is safely ignored (next initialize will use 1.0f default).
    if (!g_bridge.initialized) { return; }
    if (factor < 0.0f) { factor = 0.0f; }
    if (factor > 2.0f) { factor = 2.0f; }
    g_bridge.user_volume = factor;
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
    if (!MSU1.enabled) { return; }        // zero-cost path when no MSU-1 game is loaded
    const bool playing = (MSU1.status & MSU1_FLAG_AUDIO_PLAYING) != 0;
    // Menu/APT pause must FREEZE the track position (spec section 6 row 1):
    // keep the channel fed with silence but consume no PCM. Turbo only mutes
    // the mix and keeps consuming (fast-forward drift is inherent).
    const bool frozen = g_bridge.menu_muted || g_bridge.apt_muted;
    if (playing && !frozen && g_bridge.queued_since_clear
        && g_bridge.backend.free_buffer_count() == g_bridge.backend.total_buffer_count()) {
        g_bridge.underruns++;
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
            g_bridge.underruns = 0;   // per-session stat: don't leak into the next game
            S9xMSU1Shutdown();
            apply_mix();
            return;
        case Msu1Event::SavestateLoaded:
            g_bridge.backend.clear_queue();
            g_bridge.queued_since_clear = false;
            apply_mix();
            return;
        case Msu1Event::VolumeChanged: apply_mix(); return;
        case Msu1Event::ConsoleReset:
            g_bridge.backend.clear_queue();
            g_bridge.queued_since_clear = false;
            msu1_soft_reset(MSU1);
            apply_mix();
            return;
        case Msu1Event::AppExit:       msu3dsFinalize(); return;
        case Msu1Event::Count:         return;
    }
}

Msu1EnableAction msu3dsDecideEnableAction(bool setting_enabled, bool chip_active)
{
    if (setting_enabled == chip_active) { return Msu1EnableAction::None; }
    return setting_enabled ? Msu1EnableAction::Detect : Msu1EnableAction::TearDown;
}

bool msu3dsFormatStatus(bool msu_present, bool setting_enabled, const Msu1State& state,
                        uint32_t underruns,
                        char* line, size_t line_size,
                        char* subtitle, size_t subtitle_size)
{
    // Validate parameters
    if (line == nullptr || subtitle == nullptr) { return false; }
    if (line_size == 0 || subtitle_size == 0) { return false; }

    // Compute playing state once (single source of truth)
    const bool playing = msu_present && ((state.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    // Determine the line string
    const char* line_fmt = nullptr;
    uint32_t track_arg = 0;

    if (!msu_present && !setting_enabled) {
        line_fmt = "MSU-1: disabled";
    } else if (!msu_present) {
        line_fmt = "MSU-1: not detected";
    } else if (playing) {
        line_fmt = "MSU-1: playing track %u";
        track_arg = state.current_track;
    } else {
        line_fmt = "MSU-1: detected";
    }

    // Format the line
    int result = 0;
    if (playing) {
        result = snprintf(line, line_size, line_fmt, track_arg);
    } else {
        result = snprintf(line, line_size, "%s", line_fmt);
    }

    // Check for snprintf overflow (docs/CODING_STANDARD.md sections 3, 6)
    if (result < 0 || (size_t)result >= line_size) { return false; }

    // Determine subtitle string
    const char* subtitle_str = "";
    if (msu_present) {
        if (underruns >= MSU1_STUTTER_SEVERE_THRESHOLD) {
            subtitle_str = "Audio is stuttering - a faster SD card may help";
        } else if (underruns >= MSU1_STUTTER_MINOR_THRESHOLD) {
            subtitle_str = "Minor audio stutter detected";
        }
    }

    // Format subtitle
    result = snprintf(subtitle, subtitle_size, "%s", subtitle_str);

    // Check for snprintf overflow
    if (result < 0 || (size_t)result >= subtitle_size) { return false; }

    return true;
}
