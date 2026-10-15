#ifndef OBJ_SPANS_H
#define OBJ_SPANS_H

#include <stdint.h>

// Where each sprite's tiles landed in the OBJ vertex list this frame
// (issue #77, "Small sprites only"): the sprite pass emits one sprite at a
// time in the fast path, so a sprite is a contiguous vertex span. The
// shadow phase draws only the spans of small sprites, merging neighbours
// so a scene of 40 small sprites is a handful of draw calls.
//
// The per-scanline sprite path interleaves sprites; it records nothing,
// and the phase then draws the whole section (no size filter).

struct ObjSpan { uint16_t from, count; bool large, sub; };

struct ObjSpanTable
{
    static const int MAX = 128;
    ObjSpan spans[MAX];
    int     n;

    void reset() { n = 0; }

    void add(unsigned int from, unsigned int count, bool large, bool sub)
    {
        if (count == 0 || n >= MAX) return;
        spans[n].from = (uint16_t)from; spans[n].count = (uint16_t)count;
        spans[n].large = large; spans[n].sub = sub;
        n++;
    }

    // Small sprites of one target, adjacent spans merged. Returns how many
    // ranges `out` holds (at most `cap`).
    int small_ranges(bool sub, ObjSpan* out, int cap) const
    {
        int m = 0;
        for (int i = 0; i < n; i++) {
            const ObjSpan& s = spans[i];
            if (s.sub != sub || s.large) continue;
            if (m > 0 && out[m - 1].from + out[m - 1].count == s.from) { out[m - 1].count += s.count; continue; }
            if (m >= cap) break;
            out[m] = s; m++;
        }
        return m;
    }
};

#endif
