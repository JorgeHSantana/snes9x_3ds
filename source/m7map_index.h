#ifndef M7MAP_INDEX_H
#define M7MAP_INDEX_H

#include <stdint.h>

// Reverse index of the Mode 7 tilemap: for each of the 256 chars, the
// map positions (0..16383) that use it (issue #79, item 1).
//
// The plane is baked into a 1024x1024 texture, one vertex per map
// position; when a char's pixels or palette change, every position using
// it must be re-blitted, and S9xPrepareMode7CheckAndUpdateCharTiles
// found them by walking all 16384 map entries once per frame with a
// dirty char (Mario Kart: every frame, for ~24 used chars, 0.23 ms).
// With the index the pass visits only those positions, and "is this
// char on the map" is a count.
//
// Doubly linked lists over two 16384-entry arrays (64 KiB), relinked in
// O(1) on every tilemap write (REGISTER_2118 hands over the old and new
// char). Built lazily from VRAM the first time Mode 7 prepares a frame
// and dropped whenever VRAM is replaced wholesale (reset, savestate,
// rewind), so games that never use Mode 7 never pay for it.

struct Mode7MapIndex
{
    static const int POSITIONS = 16384;
    static const int CHARS = 256;

    int16_t  head[CHARS];
    int16_t  next[POSITIONS];
    int16_t  prev[POSITIONS];
    uint16_t count[CHARS];
    bool     valid;

    void invalidate() { valid = false; }
    bool used(unsigned int c) const { return count[c & 0xFF] != 0; }

    // vram: the 64 KiB VRAM; the tilemap is its even bytes 0..32766.
    void rebuild(const uint8_t* vram)
    {
        for (int c = 0; c < CHARS; c++) { head[c] = -1; count[c] = 0; }
        for (int pos = POSITIONS - 1; pos >= 0; pos--)     // descending so lists run ascending
            link(pos, vram[pos * 2]);
        valid = true;
    }

    void relink(int pos, unsigned int oldChar, unsigned int newChar)
    {
        if (!valid || oldChar == newChar) return;
        unlink(pos, oldChar);
        link(pos, newChar);
    }

    template <typename F>
    void for_each_position(unsigned int c, F f) const
    {
        for (int p = head[c & 0xFF]; p >= 0; p = next[p]) f(p);
    }

private:
    void link(int pos, unsigned int c)
    {
        c &= 0xFF;
        prev[pos] = -1;
        next[pos] = head[c];
        if (head[c] >= 0) prev[head[c]] = (int16_t)pos;
        head[c] = (int16_t)pos;
        count[c]++;
    }

    void unlink(int pos, unsigned int c)
    {
        c &= 0xFF;
        if (prev[pos] >= 0) next[prev[pos]] = next[pos]; else head[c] = next[pos];
        if (next[pos] >= 0) prev[next[pos]] = prev[pos];
        if (count[c]) count[c]--;
    }
};

#endif
