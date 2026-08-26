#ifndef _3DSMSURING_H_
#define _3DSMSURING_H_

#include <cstdint>
#include <cstring>

//---------------------------------------------------------------------------
// MSU-1 decoded-audio read-ahead ring (issue #55).
//
// One producer (the read-ahead tick, on its own thread on 3DS) appends
// decoded PCM in stream order; one consumer (the mixer, through the msu1
// audio-prefetch hook) reads strictly sequentially. Positions are byte
// offsets past the PCM header and follow the same loop arithmetic as
// msu1_read_audio: reaching audio_size continues at the loop point, so the
// ring can hold data across the loop seam and the seam plays gapless.
//
// Locking contract: every method expects the caller to hold one external
// leaf lock (LightLock on 3DS, no-op in host tests). The struct itself has
// no lock. A consumer position that does not match next_expected resets
// the window in place (gen bump) - the producer notices the new gen and
// re-seeks its decoder; the consumer meanwhile pads silence.
//---------------------------------------------------------------------------
struct MsuAudioRing {
    uint8_t* buf;
    uint32_t cap;

    uint32_t rd;             // physical index of the window's first byte
    uint32_t avail;          // valid bytes in the window

    uint32_t next_expected;  // logical pos the consumer must ask for next
    uint32_t prod_pos;       // logical pos the producer decodes next
    uint32_t audio_size;     // bytes past header; 0 = no track adopted
    uint32_t loop_bytes;     // wrap target when a position reaches audio_size

    uint32_t gen;            // bumped on every reset; producer re-adopts
    bool     producer_ok;    // false = persistent decode error upstream

    void init(uint8_t* storage, uint32_t capacity)
    {
        buf = (capacity > 0) ? storage : nullptr;
        cap = (storage != nullptr) ? capacity : 0;
        gen = 0;
        producer_ok = false;
        reset(0, 0, 0);
    }

    bool valid() const { return buf != nullptr && cap > 0; }

    // Drop the window and rebase both sides at 'pos'. The loop target is
    // clamped like msu1_read_audio's (a loop at/past the end restarts at 0).
    void reset(uint32_t pos, uint32_t size, uint32_t loop)
    {
        rd = 0;
        avail = 0;
        next_expected = pos;
        prod_pos = pos;
        audio_size = size;
        loop_bytes = (loop >= size) ? 0 : loop;
        gen++;
    }

    uint32_t free_space() const { return cap - avail; }

    // Largest chunk the producer may decode next: bounded by free space,
    // by the caller's scratch size, and by the loop seam - one append never
    // crosses audio_size, so prod_pos wraps exactly on the boundary.
    uint32_t producer_chunk(uint32_t max_chunk) const
    {
        if (!valid() || audio_size == 0) { return 0; }
        uint32_t n = free_space();
        if (n > max_chunk) { n = max_chunk; }
        uint32_t to_seam = audio_size - prod_pos;
        if (n > to_seam) { n = to_seam; }
        return n;
    }

    // Append n decoded bytes (the producer decoded them at prod_pos).
    // n must come from producer_chunk(); returns the new prod_pos so the
    // producer knows where its decoder must be (it jumps at the seam).
    uint32_t append(const uint8_t* src, uint32_t n)
    {
        uint32_t wr = (rd + avail) % cap;
        uint32_t first = cap - wr;
        if (first > n) { first = n; }
        memcpy(buf + wr, src, first);
        if (n > first) { memcpy(buf, src + first, n - first); }
        avail += n;
        prod_pos += n;
        if (prod_pos == audio_size) { prod_pos = loop_bytes; }
        return prod_pos;
    }

    // Serve up to n bytes at 'pos'. Sequential contract: pos must equal
    // next_expected; anything else is a stream jump (track seek, resume) -
    // the window resets there and 0 is returned while the producer catches
    // up. *alive tells the consumer whether silence is temporary (true) or
    // the producer is dead and the track should stall out (false).
    uint32_t serve(uint32_t pos, uint8_t* dst, uint32_t n, bool* alive)
    {
        if (alive != nullptr) { *alive = producer_ok; }
        if (!valid() || n == 0) { return 0; }
        if (pos != next_expected) {
            reset(pos, audio_size, loop_bytes);
            return 0;
        }
        uint32_t served = 0;
        while (served < n && avail > 0) {
            uint32_t chunk = n - served;
            if (chunk > avail) { chunk = avail; }
            uint32_t to_seam = (audio_size > 0) ? audio_size - next_expected
                                                : chunk;
            if (chunk > to_seam) { chunk = to_seam; }
            uint32_t first = cap - rd;
            if (first > chunk) { first = chunk; }
            memcpy(dst + served, buf + rd, first);
            if (chunk > first) { memcpy(dst + served + first, buf, chunk - first); }
            rd = (rd + chunk) % cap;
            avail -= chunk;
            served += chunk;
            next_expected += chunk;
            if (audio_size > 0 && next_expected == audio_size) {
                next_expected = loop_bytes;
            }
        }
        return served;
    }
};

#endif
