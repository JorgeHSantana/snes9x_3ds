#include "3dsmsu.h"
#include <cstring>

namespace {
struct BridgeState {
    bool             initialized;
    Msu1AudioBackend backend;
    int16_t*         staging;
    uint32_t         staging_samples;
    float            global_volume;
    bool             menu_muted;
    bool             turbo_muted;
    bool             apt_muted;
    bool             drain_active;
    uint32_t         underruns;
};
BridgeState g_bridge = {};

void bridge_volume_cb(void) { msu3dsOnEvent(Msu1Event::VolumeChanged); }

void apply_mix(void)
{
    if (!g_bridge.initialized) { return; }
    float mix = 0.0f;
    if (!g_bridge.menu_muted && !g_bridge.turbo_muted && !g_bridge.apt_muted) {
        mix = g_bridge.global_volume * ((float)MSU1.volume / 255.0f);
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
    g_bridge = BridgeState{};
    g_bridge.initialized     = true;
    g_bridge.backend         = backend;
    g_bridge.staging         = staging;
    g_bridge.staging_samples = staging_samples;
    g_bridge.global_volume   = 1.0f;
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
    g_bridge = BridgeState{};
}

void msu3dsSetGlobalVolume(float factor)
{
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
    const bool playing = MSU1.enabled
        && (MSU1.status & MSU1_FLAG_AUDIO_PLAYING) != 0;
    if (playing
        && g_bridge.backend.free_buffer_count() == g_bridge.backend.total_buffer_count()) {
        g_bridge.underruns++;
    }
    while (g_bridge.backend.free_buffer_count() > 0) {
        uint32_t cap_samples = g_bridge.backend.buffer_capacity_samples();
        uint32_t cap_bytes   = cap_samples * MSU1_BYTES_PER_SAMPLE;
        uint32_t got = 0;
        if (playing) {
            got = msu1_read_audio(MSU1, (uint8_t*)g_bridge.staging, cap_bytes);
        }
        if (got < cap_bytes) {
            memset((uint8_t*)g_bridge.staging + got, 0, cap_bytes - got);
        }
        if (!g_bridge.backend.queue_buffer(g_bridge.staging, cap_samples)) { break; }
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
            S9xMSU1Shutdown();
            apply_mix();
            return;
        case Msu1Event::SavestateLoaded:
            g_bridge.backend.clear_queue();
            apply_mix();
            return;
        case Msu1Event::VolumeChanged: apply_mix(); return;
        case Msu1Event::AppExit:       msu3dsFinalize(); return;
        case Msu1Event::Count:         return;
    }
}
