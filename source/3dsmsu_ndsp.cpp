// source/3dsmsu_ndsp.cpp — NDSP channel 1 backend. 3DS-only, logic-free.
#include <stdlib.h>
#include <3ds.h>
#include <cstring>
#include "3dsmsu.h"
#include "3dsmsu_ndsp.h"

namespace {
constexpr int      MSU_CHANNEL      = 1;
constexpr uint32_t MSU_BUF_COUNT    = 8;
constexpr uint32_t MSU_BUF_SAMPLES  = 1024;      // ~23 ms each, ~186 ms total queue
constexpr uint32_t MSU_BUF_BYTES    = MSU_BUF_SAMPLES * MSU1_BYTES_PER_SAMPLE;

uint8_t*    g_pcm_block = nullptr;               // one linearAlloc for all wavebufs
int16_t*    g_staging   = nullptr;               // one linearAlloc for the staging buffer
ndspWaveBuf g_wavebufs[MSU_BUF_COUNT];
uint32_t    g_next = 0;

bool ndsp_init_channel(uint32_t sample_rate)
{
    ndspChnReset(MSU_CHANNEL);
    ndspChnSetInterp(MSU_CHANNEL, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(MSU_CHANNEL, (float)sample_rate);
    ndspChnSetFormat(MSU_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    memset(g_wavebufs, 0, sizeof(g_wavebufs));
    for (uint32_t i = 0; i < MSU_BUF_COUNT; i++) {
        g_wavebufs[i].data_vaddr = g_pcm_block + i * MSU_BUF_BYTES;
        g_wavebufs[i].status     = NDSP_WBUF_DONE;
    }
    g_next = 0;
    return true;
}

void ndsp_set_mix(float volume)
{
    float mix[12] = {};
    mix[0] = volume;    // front L
    mix[1] = volume;    // front R
    ndspChnSetMix(MSU_CHANNEL, mix);
}

bool ndsp_queue_buffer(const int16_t* samples, uint32_t sample_count)
{
    if (samples == nullptr || sample_count == 0 || sample_count > MSU_BUF_SAMPLES) {
        return false;
    }
    ndspWaveBuf& wb = g_wavebufs[g_next];
    if (wb.status != NDSP_WBUF_DONE && wb.status != NDSP_WBUF_FREE) { return false; }
    memcpy((void*)wb.data_vaddr, samples, sample_count * MSU1_BYTES_PER_SAMPLE);
    DSP_FlushDataCache(wb.data_vaddr, sample_count * MSU1_BYTES_PER_SAMPLE);
    wb.nsamples = sample_count;
    ndspChnWaveBufAdd(MSU_CHANNEL, &wb);
    g_next = (g_next + 1) % MSU_BUF_COUNT;
    return true;
}

uint32_t ndsp_free_buffer_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < MSU_BUF_COUNT; i++) {
        if (g_wavebufs[i].status == NDSP_WBUF_DONE || g_wavebufs[i].status == NDSP_WBUF_FREE) {
            n++;
        }
    }
    return n;
}

uint32_t ndsp_total_buffer_count(void)    { return MSU_BUF_COUNT; }
uint32_t ndsp_buffer_capacity_samples(void) { return MSU_BUF_SAMPLES; }

void ndsp_clear_queue(void)
{
    ndspChnWaveBufClear(MSU_CHANNEL);
    for (uint32_t i = 0; i < MSU_BUF_COUNT; i++) { g_wavebufs[i].status = NDSP_WBUF_DONE; }
    g_next = 0;
}

void ndsp_shutdown_channel(void) { ndspChnWaveBufClear(MSU_CHANNEL); }

// phase B: data-track read-ahead ring (see 3dsmsu.cpp). 512 KB is ~1.7 s of
// Road Blaster's stream; the lock is a leaf held only for index updates.
constexpr uint32_t PREFETCH_RING_BYTES = 512 * 1024;
uint8_t*  g_prefetch_ring = nullptr;
LightLock g_prefetch_lock;
void prefetch_lock(void)   { LightLock_Lock(&g_prefetch_lock); }
void prefetch_unlock(void) { LightLock_Unlock(&g_prefetch_lock); }
} // namespace

bool msu3dsNdspInstall(void)
{
    if (g_pcm_block == nullptr) {
        g_pcm_block = (uint8_t*)linearAlloc(MSU_BUF_COUNT * MSU_BUF_BYTES);
        if (g_pcm_block == nullptr) { return false; }
    }
    if (g_staging == nullptr) {
        g_staging = (int16_t*)linearAlloc(MSU_BUF_BYTES);
        if (g_staging == nullptr) { linearFree(g_pcm_block); g_pcm_block = nullptr; return false; }
    }
    if (g_prefetch_ring == nullptr) {
        g_prefetch_ring = (uint8_t*)malloc(PREFETCH_RING_BYTES);
        if (g_prefetch_ring != nullptr) {
            LightLock_Init(&g_prefetch_lock);
            msu3dsDataPrefetchLocks(prefetch_lock, prefetch_unlock);
            msu3dsDataPrefetchInit(g_prefetch_ring, PREFETCH_RING_BYTES);
        }
        // allocation failure: prefetch stays disabled, phase-A behavior
    }
    Msu1AudioBackend backend = {
        ndsp_init_channel, ndsp_set_mix, ndsp_queue_buffer, ndsp_free_buffer_count,
        ndsp_total_buffer_count, ndsp_buffer_capacity_samples,
        ndsp_clear_queue, ndsp_shutdown_channel
    };
    return msu3dsInitialize(backend, g_staging, MSU_BUF_SAMPLES);
}

void msu3dsNdspUninstall(void)
{
    msu3dsFinalize();
    if (g_staging   != nullptr) { linearFree(g_staging);   g_staging   = nullptr; }
    if (g_pcm_block != nullptr) { linearFree(g_pcm_block); g_pcm_block = nullptr; }
    if (g_prefetch_ring != nullptr) {
        msu3dsDataPrefetchInit(nullptr, 0);   // detach before the free
        free(g_prefetch_ring);
        g_prefetch_ring = nullptr;
    }
}
