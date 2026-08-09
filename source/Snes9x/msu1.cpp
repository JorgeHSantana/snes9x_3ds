#include "msu1.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

Msu1State MSU1 = {};

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

void S9xMSU1Shutdown(void) { msu1_shutdown(MSU1); }
