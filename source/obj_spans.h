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

struct ObjSpan { uint16_t from, count; bool large, sub; int16_t x0, y0, x1, y1; };   // x0..y1: the OAM rectangle (SNES px, inclusive)

// A character is several OAM sprites that touch: the stretch scales them
// about ONE anchor (the bottom centre of their union) or neighbours would
// overlap (a darker band) and part (a gap) as each scaled about its own feet.
struct ObjCluster { int16_t ax, ay; };

struct ObjSpanTable
{
    static const int MAX = 128;
    ObjSpan spans[MAX];
    int     n;

    void reset() { n = 0; }

    void add(unsigned int from, unsigned int count, bool large, bool sub, int x0 = 0, int y0 = 0, int x1 = 0, int y1 = 0)
    {
        if (count == 0 || n >= MAX) return;
        spans[n].from = (uint16_t)from; spans[n].count = (uint16_t)count;
        spans[n].large = large; spans[n].sub = sub;
        spans[n].x0 = (int16_t)x0; spans[n].y0 = (int16_t)y0; spans[n].x1 = (int16_t)x1; spans[n].y1 = (int16_t)y1;
        n++;
    }

    static bool touching(const ObjSpan& a, const ObjSpan& b)
    {
        return a.x0 <= b.x1 + 1 && b.x0 <= a.x1 + 1 && a.y0 <= b.y1 + 1 && b.y0 <= a.y1 + 1;
    }

    // Groups the sprites of one target into clusters of touching sprites.
    // clusterOf[i] = the cluster of span i, or 0xFF when it is left out
    // (other target, or large under smallOnly). out[c] = the cluster's
    // anchor: bottom centre of the union rectangle. Returns the count.
    int clusters(bool sub, bool smallOnly, uint8_t clusterOf[MAX], ObjCluster* out, int cap) const
    {
        int m = 0;
        for (int i = 0; i < n; i++) clusterOf[i] = 0xFF;
        for (int i = 0; i < n; i++) {
            const ObjSpan& s = spans[i];
            if (s.sub != sub || (smallOnly && s.large)) continue;
            int c = -1;
            for (int j = 0; j < i; j++) {
                if (clusterOf[j] == 0xFF || !touching(s, spans[j])) continue;
                if (c < 0) { c = clusterOf[j]; continue; }
                if (clusterOf[j] != c) {        // bridges two clusters: merge them into c
                    const uint8_t old = clusterOf[j];
                    for (int k = 0; k < i; k++) if (clusterOf[k] == old) clusterOf[k] = (uint8_t)c;
                }
            }
            if (c < 0) { if (m >= cap) continue; c = m++; }
            clusterOf[i] = (uint8_t)c;
        }
        // anchors from the union of each cluster (merged clusters leave empty ids: anchor 0, no members)
        for (int c = 0; c < m; c++) {
            int x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768; bool any = false;
            for (int i = 0; i < n; i++) {
                if (clusterOf[i] != c) continue;
                const ObjSpan& s = spans[i]; any = true;
                if (s.x0 < x0) x0 = s.x0;
                if (s.y0 < y0) y0 = s.y0;
                if (s.x1 > x1) x1 = s.x1;
                if (s.y1 > y1) y1 = s.y1;
            }
            out[c].ax = any ? (int16_t)((x0 + x1 + 1) / 2) : 0;
            out[c].ay = any ? (int16_t)(y1 + 1) : 0;
        }
        return m;
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
