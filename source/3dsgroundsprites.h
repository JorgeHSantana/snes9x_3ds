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

// the ground under a sprite whose feet fell just below the plane's last
// row - the band between the track and the map in Mario Kart, where a
// spinning or drifting kart's box dips (Jorge's log: "127,110:0" while
// the kart normally stands on row 101): the nearest plane row above,
// within `reach` rows, is the ground there
static inline uint8_t groundRowNear(const GroundFrame *f, int y, int reach)
{
    uint8_t r = groundRowAt(f, y);
    for (int k = 1; r == 0 && k <= reach; k++)
        r = groundRowAt(f, y - k);
    return r;
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

// the depths a sprite on the ground takes at the nearest row and at the
// horizon: the plane's own ramp (BG1 depth x the perspective gain at the
// nearest row, x (1 - k) at the horizon) plus the lift toward the viewer
static inline void groundLiftDepths(float bg1Depth, float k, float gain, float lift,
                                    float *nearDepth, float *farDepth)
{
    *nearDepth = bg1Depth * gain + lift;
    *farDepth = bg1Depth * gain * (1.0f - k) + lift;
}

// An OAM sprite's row jumps when it hops, spins or changes size
// (Mario Kart drifting / hitting a wall: the depth snapped, Jorge's
// reports). Two remedies:
//  1. the row a sprite USES slides toward the row it stands on by at
//     most GROUND_SLEW_STEP per frame (4/255: the whole plane in ~64
//     frames, a hop's few rows in a handful);
//  2. a sprite is tracked by POSITION, not by its tiles - a spin or a
//     hit changes the animation frame (another sheet row) and would have
//     restarted the glide as a "new" sprite.
#define GROUND_SLEW_STEP 4
#define GROUND_REVERSE_CONFIRM_FRAMES 2

static inline int groundSlew(int prev, int target, int maxStep)
{
    if (prev <= 0) return target;                 // first sight / was off the plane
    if (target <= 0) return target;               // left the plane: no glide into "nowhere"
    int d = target - prev;
    if (d > maxStep) d = maxStep;
    if (d < -maxStep) d = -maxStep;
    return prev + d;
}

// One temporal memory per physical SNES OAM slot. There is deliberately no
// spatial matching: nearby smoke, shadows, items and character pieces can
// never steal or merge memories. A slot forgotten for two frames starts fresh.
#define GROUND_OAM_COUNT 128

struct GroundOamTrack
{
    uint8_t rowW[GROUND_OAM_COUNT];
    int8_t  pendingDir[GROUND_OAM_COUNT];
    uint8_t pendingFrames[GROUND_OAM_COUNT];
    uint8_t age[GROUND_OAM_COUNT];
    uint8_t seen[GROUND_OAM_COUNT];
};

static inline void groundOamFrameStart(GroundOamTrack *t)
{
    for (int i = 0; i < GROUND_OAM_COUNT; i++) {
        if (!t->seen[i]) continue;
        if (t->age[i] >= 1) {
            t->seen[i] = 0;
            t->pendingDir[i] = 0;
            t->pendingFrames[i] = 0;
        } else {
            t->age[i]++;
        }
    }
}

// Sprite animations can change an OAM entry's size for one frame. Because the
// OAM position is its top-left corner, that moves the inferred bottom edge and
// makes the ground target alternate even though the character did not move.
// A plain slew limiter turns that alternating target into a permanent depth
// wobble. Require a direction change to survive two rendered frames; motion
// along the road keeps the same direction and then proceeds every frame.
static inline int groundOamStableSlew(GroundOamTrack *t, int i, int target)
{
    int current = t->rowW[i];
    if (current <= 0 || target <= 0) {
        t->pendingDir[i] = 0; t->pendingFrames[i] = 0;
        return target;
    }
    int dir = target > current ? 1 : (target < current ? -1 : 0);
    if (dir == 0) {
        t->pendingDir[i] = 0; t->pendingFrames[i] = 0;
        return current;
    }
    if (t->pendingDir[i] != dir) {
        t->pendingDir[i] = (int8_t)dir;
        t->pendingFrames[i] = 1;
        return current;
    }
    if (t->pendingFrames[i] < GROUND_REVERSE_CONFIRM_FRAMES)
        t->pendingFrames[i]++;
    if (t->pendingFrames[i] < GROUND_REVERSE_CONFIRM_FRAMES)
        return current;
    int next = groundSlew(current, target, GROUND_SLEW_STEP);
    if (next == target) { t->pendingDir[i] = 0; t->pendingFrames[i] = 0; }
    return next;
}

static inline int groundOamRow(GroundOamTrack *t, int slot, int targetRowW)
{
    if (slot < 0 || slot >= GROUND_OAM_COUNT) return targetRowW;
    int row = targetRowW;
    if (t->seen[slot]) {
        row = groundOamStableSlew(t, slot, targetRowW);
    } else {
        t->pendingDir[slot] = 0;
        t->pendingFrames[slot] = 0;
    }
    t->rowW[slot] = (uint8_t)row;
    t->age[slot] = 0;
    t->seen[slot] = 1;
    return row;
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

#endif
