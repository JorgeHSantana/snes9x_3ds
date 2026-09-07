#ifndef _3DSGROUNDSPRITES_H_
#define _3DSGROUNDSPRITES_H_

#include <stdint.h>
#include <string.h>

// Sprites on a Mode 7 plane follow the ground (issue #76). A sprite whose
// feet touch a plane row takes that row's distance, so a kart far down the
// track sits far and a box beside the player sits near - interpolated by
// the row between the profile's Ground near / Ground far gauges.
//
// Pure bookkeeping, no GPU: gfxhw fills a per-row table at the Mode 7 flush
// (the same w the plane's scanlines carry, 3dsmode7persp.h) and a per-frame
// table of sprite signatures (tile name + palette - stable per graphic);
// the sprite vertex carries both in its w:
//
//     w = rowW + 256 * slot
//
// rowW 1..255 = the bottom row's distance encoding (255 nearest, 1 the
// horizon), 0 = the sprite's bottom row is not on the plane (keeps the
// normal sprite gauges); slot 1..31 = the signature's index in this frame's
// table (0 = table full / no signature: treated as on the ground by
// position, never spotlit). The tile vertex shader reads the slot into an
// indexed uniform table (ground yes/no + editor spotlight) so the editor's
// "not on ground" marks and the spotlight apply live without rebuilding
// the frame.

#define GROUND_ROWS       240
#define GROUND_SLOTS      32          // slot 0 reserved
#define GROUND_ROW_ONE    255         // nearest row
#define GROUND_SLOT_UNIT  256

struct GroundFrame
{
    uint8_t  rowW[GROUND_ROWS];       // 0 = not a plane row
    uint32_t sig[GROUND_SLOTS];       // [0] unused
    uint8_t  sigX[GROUND_SLOTS];      // first sprite's screen position (editor label)
    uint8_t  sigY[GROUND_SLOTS];
    int      sigCount;                // slots used, including the reserved 0
};

static inline void groundFrameReset(GroundFrame *f)
{
    memset(f, 0, sizeof(*f));
    f->sigCount = 1;
}

static inline void groundFrameStart(GroundFrame *f)
{
    memset(f->rowW, 0, sizeof(f->rowW));
    f->sigCount = 1;
}

// the plane scanline's w (8..256, 3dsmode7persp.h) as a row byte
static inline uint8_t groundRowEncode(int m7w)
{
    if (m7w < 1) m7w = 1;
    if (m7w > GROUND_ROW_ONE) m7w = GROUND_ROW_ONE;
    return (uint8_t)m7w;
}

static inline void groundRowSet(GroundFrame *f, int y, int m7w)
{
    if (y >= 0 && y < GROUND_ROWS)
        f->rowW[y] = groundRowEncode(m7w);
}

static inline uint8_t groundRowAt(const GroundFrame *f, int y)
{
    if (y < 0) return 0;
    if (y >= GROUND_ROWS) y = GROUND_ROWS - 1;   // a sprite past the bottom stands on the last row
    return f->rowW[y];
}

static inline bool groundRowsPresent(const GroundFrame *f)
{
    for (int y = 0; y < GROUND_ROWS; y++)
        if (f->rowW[y]) return true;
    return false;
}

// a sprite graphic's identity: the tile name's row of the sprite sheet
// (name >> 4, 5 bits - animation frames usually walk along a row, so one
// mark covers the whole animation and the editor list stays short) +
// palette (3 bits)
static inline uint32_t groundSigMake(int name, int palette)
{
    return (uint32_t)(name & 0x1F0) | ((uint32_t)(palette & 7) << 9);
}

static inline int groundSigName(uint32_t sig)    { return (int)(sig & 0x1FF); }
static inline int groundSigPalette(uint32_t sig) { return (int)((sig >> 9) & 7); }

// the signature's slot this frame, registering it on first sight; 0 when
// the table is full
static inline int groundSlotFor(GroundFrame *f, uint32_t sig, int x, int y)
{
    for (int i = 1; i < f->sigCount; i++)
        if (f->sig[i] == sig) return i;
    if (f->sigCount >= GROUND_SLOTS) return 0;
    int i = f->sigCount++;
    f->sig[i] = sig;
    f->sigX[i] = (uint8_t)(x < 0 ? 0 : (x > 255 ? 255 : x));
    f->sigY[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));
    return i;
}

static inline int16_t groundVertexW(int rowW, int slot)
{
    return (int16_t)(rowW + GROUND_SLOT_UNIT * slot);
}

static inline void groundVertexDecode(int16_t w, int *rowW, int *slot)
{
    *slot = w / GROUND_SLOT_UNIT;
    *rowW = w - *slot * GROUND_SLOT_UNIT;
}

// the shader's formula, mirrored for tests: the row's shift between the
// near gauge (rowW 255) and the far gauge (rowW -> 0, the horizon)
static inline float groundRowShift(int rowW, float nearShift, float farShift)
{
    float t = 1.0f - (float)rowW / (float)GROUND_ROW_ONE;
    return nearShift + (farShift - nearShift) * t;
}

// the game's "not on ground" marks (.3d GROUNDX=)
#define GROUND_EXCEPTIONS_MAX 32

static inline bool groundIsException(const uint32_t *list, int count, uint32_t sig)
{
    for (int i = 0; i < count; i++)
        if (list[i] == sig) return true;
    return false;
}

// toggles the mark; returns the new count
static inline int groundToggleException(uint32_t *list, int count, uint32_t sig)
{
    for (int i = 0; i < count; i++) {
        if (list[i] == sig) {
            list[i] = list[count - 1];
            return count - 1;
        }
    }
    if (count >= GROUND_EXCEPTIONS_MAX) return count;
    list[count] = sig;
    return count + 1;
}

// the sprite's bottom row on screen: a SNES VPos wraps at 256 (a sprite
// hanging off the top starts at VPos - 256)
static inline int groundSpriteTop(int vpos)
{
    return vpos >= 240 ? vpos - 256 : vpos;
}

static inline int groundSpriteBottom(int vpos, int height)
{
    return groundSpriteTop(vpos) + height - 1;
}

// A character is several hardware sprites (kart + driver + shadow); each
// has its own bottom row, so the driver's head would sit on a farther row
// than the wheels and the character would split across depths (Jorge's
// report). Sprites whose boxes touch (within `expand` px) are clustered;
// the cluster's lowest bottom row - its feet - is the master every member
// follows. Union-find over the visible boxes; `cluster[i]` receives the
// root index of box i. Returns the number of clusters.
struct GroundBox { int16_t x0, y0, x1, y1; };   // inclusive edges

static inline int groundClusterFind(uint8_t *parent, int i)
{
    while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}

static inline int groundClusterBoxes(const GroundBox *b, const bool *visible, int n,
                                     int expand, uint8_t *cluster)
{
    uint8_t parent[128];
    if (n > 128) n = 128;
    for (int i = 0; i < n; i++) parent[i] = (uint8_t)i;
    for (int i = 0; i < n; i++) {
        if (!visible[i]) continue;
        for (int j = i + 1; j < n; j++) {
            if (!visible[j]) continue;
            // inclusive edges: a gap of `expand` pixels or less still touches
            if (b[i].x1 + expand + 1 < b[j].x0 || b[j].x1 + expand + 1 < b[i].x0) continue;
            if (b[i].y1 + expand + 1 < b[j].y0 || b[j].y1 + expand + 1 < b[i].y0) continue;
            int ri = groundClusterFind(parent, i), rj = groundClusterFind(parent, j);
            if (ri != rj) parent[rj < ri ? ri : rj] = (uint8_t)(rj < ri ? rj : ri);
        }
    }
    int count = 0;
    for (int i = 0; i < n; i++) {
        cluster[i] = (uint8_t)groundClusterFind(parent, i);
        if (cluster[i] == i && visible[i]) count++;
    }
    return count;
}

#endif
