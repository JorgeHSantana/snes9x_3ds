#ifndef SRAM_SAVE_H
#define SRAM_SAVE_H

#include <atomic>
#include <stddef.h>
#include <stdint.h>

static constexpr size_t SRAM_SAVE_MAX_SIZE = 0x20000;

static inline size_t sram_save_size_bytes(uint8_t encoded_size, size_t rtc_pad)
{
    size_t size = 0;
    if (encoded_size >= 7) {
        size = SRAM_SAVE_MAX_SIZE;
    } else if (encoded_size != 0) {
        size = static_cast<size_t>(1) << (encoded_size + 10);
    }
    if (rtc_pad >= SRAM_SAVE_MAX_SIZE - size) {
        return SRAM_SAVE_MAX_SIZE;
    }
    return size + rtc_pad;
}

// Writer is owned exclusively by this synchronous transaction. In particular,
// a successful buffered write is not durable success until close succeeds.
template <class Writer>
bool write_sram_file(Writer& writer, const char* path, const uint8_t* data, size_t size)
{
    if (path == nullptr || path[0] == '\0' || data == nullptr || size == 0 || size > SRAM_SAVE_MAX_SIZE) {
        return false;
    }
    if (!writer.open(path, "wb")) {
        return false;
    }
    const bool written = writer.write(data, size) == size;
    const bool closed = writer.close() == 0;
    return written && closed;
}

// The emulation thread owns dirty and is not executing CPU instructions
// during save. The audio thread observes silence, which must be restored.
template <class Save>
bool attempt_sram_save(uint8_t& dirty, std::atomic<bool>& silence, Save save)
{
    const bool was_silent = silence.exchange(true);
    const bool success = save();
    dirty = success ? 0 : 1;
    silence.store(was_silent);
    return success;
}

#endif
