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

// A character's row jumps when it hops, spins or splits from its shadow
// (Mario Kart drifting / hitting a wall: the depth snapped, Jorge's
// reports). Two remedies:
//  1. the row a character USES slides toward the row it stands on by at
//     most GROUND_SLEW_STEP per frame (4/255: the whole plane in ~64
//     frames, a hop's few rows in a handful);
//  2. a character is tracked by POSITION, not by its tiles - a spin or a
//     hit changes the animation frame (another sheet row) and would have
//     restarted the glide as a "new" character.
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

// per-character memory across frames: the feet's screen position last
// frame, matched by proximity (a character moves a few px per frame)
#define GROUND_TRACK_MAX   32
#define GROUND_TRACK_REACH 24

struct GroundTrack
{
    int16_t  x[GROUND_TRACK_MAX], y[GROUND_TRACK_MAX];   // feet: bottom-centre
    uint8_t  rowW[GROUND_TRACK_MAX];
    int8_t   pendingDir[GROUND_TRACK_MAX]; // unconfirmed target direction (-1/0/+1)
    uint8_t  pendingFrames[GROUND_TRACK_MAX];
    uint8_t  age[GROUND_TRACK_MAX];      // frames since last seen
    uint8_t  claimed[GROUND_TRACK_MAX];  // matched this frame already
    int      count;
};

static inline void groundTrackFrameStart(GroundTrack *t)
{
    int w = 0;
    for (int i = 0; i < t->count; i++) {
        if (t->age[i] >= 2) continue;             // not seen for 2 frames: forget
        t->x[w] = t->x[i]; t->y[w] = t->y[i]; t->rowW[w] = t->rowW[i];
        t->pendingDir[w] = t->pendingDir[i]; t->pendingFrames[w] = t->pendingFrames[i];
        t->age[w] = (uint8_t)(t->age[i] + 1); t->claimed[w] = 0;
        w++;
    }
    t->count = w;
}

// One character's request for this frame: where its feet are and the row
// they stand on; `rowW` receives the smoothed row. All requests of a
// frame are matched to the memories at once, closest pairs first: the
// smoke of a drift or the sparks of a wall hit appear a few px from the
// kart's feet and, matched one by one in sprite order, stole its memory
// (the kart then restarted from scratch - the snap Jorge still saw).
struct GroundTrackReq { int16_t x, y; uint8_t target; uint8_t rowW; };

// Sprite animations can change an OAM entry's size for one frame. Because the
// OAM position is its top-left corner, that moves the inferred bottom edge and
// makes the ground target alternate even though the character did not move.
// A plain slew limiter turns that alternating target into a permanent depth
// wobble. Require a direction change to survive two rendered frames; motion
// along the road keeps the same direction and then proceeds every frame.
static inline int groundTrackStableSlew(GroundTrack *t, int i, int target)
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

static inline void groundTrackAssign(GroundTrack *t, GroundTrackReq *req, int n)
{
    if (n > GROUND_TRACK_MAX) n = GROUND_TRACK_MAX;
    bool reqDone[GROUND_TRACK_MAX];
    for (int i = 0; i < n; i++) { reqDone[i] = false; req[i].rowW = req[i].target; }
    // greedy by distance: pick the globally closest unclaimed pair until
    // none is within reach (pairs <= 32 x 32, tiny)
    // cost = distance^2 + (row difference / 4)^2: a memory whose row is far
    // from the character's own is a worse match than one a few px away
    // (a passing item left its memory on the kart's spot and the kart
    // glided from the item's row - Jorge's log at 13.8 s)
    const int reach2 = GROUND_TRACK_REACH * GROUND_TRACK_REACH;
    for (;;) {
        int bi = -1, bj = -1, bd = reach2 + 1;
        for (int i = 0; i < n; i++) {
            if (reqDone[i]) continue;
            for (int j = 0; j < t->count; j++) {
                if (t->claimed[j]) continue;
                int dx = req[i].x - t->x[j], dy = req[i].y - t->y[j];
                int dr = ((int)req[i].target - (int)t->rowW[j]) / 4;
                int d = dx * dx + dy * dy + dr * dr;
                if (d < bd) { bd = d; bi = i; bj = j; }
            }
        }
        if (bi < 0) break;
        int r = groundTrackStableSlew(t, bj, req[bi].target);
        t->rowW[bj] = (uint8_t)r; t->x[bj] = req[bi].x; t->y[bj] = req[bi].y;
        t->age[bj] = 0; t->claimed[bj] = 1;
        req[bi].rowW = (uint8_t)r; reqDone[bi] = true;
    }
    // the rest are new characters
    for (int i = 0; i < n; i++) {
        if (reqDone[i] || t->count >= GROUND_TRACK_MAX) continue;
        int j = t->count++;
        t->x[j] = req[i].x; t->y[j] = req[i].y; t->rowW[j] = req[i].target;
        t->pendingDir[j] = 0; t->pendingFrames[j] = 0;
        t->age[j] = 0; t->claimed[j] = 1;
    }
}

// one request on its own (tests / single character)
static inline int groundTrackRow(GroundTrack *t, int x, int y, int targetRowW)
{
    int best = -1, bestD = GROUND_TRACK_REACH * GROUND_TRACK_REACH + 1;
    for (int i = 0; i < t->count; i++) {
        if (t->claimed[i]) continue;
        int dx = x - t->x[i], dy = y - t->y[i];
        int d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best >= 0) {
        int r = groundTrackStableSlew(t, best, targetRowW);
        t->rowW[best] = (uint8_t)r; t->x[best] = (int16_t)x; t->y[best] = (int16_t)y;
        t->age[best] = 0; t->claimed[best] = 1;
        return r;
    }
    if (t->count < GROUND_TRACK_MAX) {
        int i = t->count++;
        t->x[i] = (int16_t)x; t->y[i] = (int16_t)y; t->rowW[i] = (uint8_t)targetRowW;
        t->pendingDir[i] = 0; t->pendingFrames[i] = 0;
        t->age[i] = 0; t->claimed[i] = 1;
    }
    return targetRowW;
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

// Persistent logical groups for composite sprites. Geometry is consulted only
// when an OAM slot first appears. Existing groups are never merged or split by
// later animation-box movement; a slot must be absent for two rendered frames
// before it can be assigned anew.
struct GroundStableGroups
{
    uint8_t group[128];
    uint8_t active[128];
    uint8_t missed[128];
};

static inline void groundStableGroupsAssign(GroundStableGroups *s,
                                            const GroundBox *b, const bool *visible,
                                            int n, int expand, uint8_t *group)
{
    if (n > 128) n = 128;
    bool fresh[128];
    for (int i = 0; i < n; i++) {
        fresh[i] = visible[i] && !s->active[i];
        if (visible[i]) {
            if (fresh[i]) s->group[i] = (uint8_t)i;
            s->active[i] = 1;
            s->missed[i] = 0;
        } else if (s->active[i]) {
            if (++s->missed[i] >= 2) {
                s->active[i] = 0;
                s->group[i] = (uint8_t)i;
            }
        }
        group[i] = s->group[i];
    }

    // Form components only among slots that appeared together this frame.
    uint8_t initial[128];
    groundClusterBoxes(b, fresh, n, expand, initial);
    for (int root = 0; root < n; root++) {
        if (!fresh[root] || initial[root] != root) continue;
        int leader = root;
        for (int i = 0; i < n; i++) {
            if (!fresh[i] || initial[i] != root) continue;
            int ai = (b[i].x1 - b[i].x0 + 1) * (b[i].y1 - b[i].y0 + 1);
            int al = (b[leader].x1 - b[leader].x0 + 1) * (b[leader].y1 - b[leader].y0 + 1);
            if (ai > al || (ai == al && b[i].y1 > b[leader].y1)) leader = i;
        }

        // A newly appearing piece may attach to one established group, but
        // can never bridge two established characters together.
        int attach = -1;
        for (int i = 0; i < n && attach < 0; i++) {
            if (!fresh[i] || initial[i] != root) continue;
            for (int j = 0; j < n; j++) {
                if (!visible[j] || fresh[j]) continue;
                if (b[i].x1 + expand + 1 < b[j].x0 || b[j].x1 + expand + 1 < b[i].x0) continue;
                if (b[i].y1 + expand + 1 < b[j].y0 || b[j].y1 + expand + 1 < b[i].y0) continue;
                attach = s->group[j];
                break;
            }
        }
        uint8_t id = (uint8_t)(attach >= 0 ? attach : leader);
        for (int i = 0; i < n; i++) {
            if (!fresh[i] || initial[i] != root) continue;
            s->group[i] = id;
            group[i] = id;
        }
    }
}

// A character in the air sits above its shadow: when a cluster's box has
// a FLAT cluster right below it (a shadow is a few px tall; the dust of a
// drift or of dirt is not, and must not pass for the ground - Jorge:
// "on dirt the same thing happens"), with horizontal overlap and a gap
// <= maxGap, that lower cluster's feet are where the ground is. Returns
// the index of the cluster below, or -1.
#define GROUND_SHADOW_MAX_HEIGHT 8

static inline int groundShadowBelow(const GroundBox *cb, const bool *valid, int n, int i, int maxGap)
{
    int best = -1, bestGap = maxGap + 1;
    for (int j = 0; j < n; j++) {
        if (j == i || !valid[j]) continue;
        if (cb[j].y1 - cb[j].y0 + 1 > GROUND_SHADOW_MAX_HEIGHT) continue;   // not flat: not a shadow
        if (cb[j].x1 < cb[i].x0 || cb[j].x0 > cb[i].x1) continue;      // no horizontal overlap
        int gap = cb[j].y0 - cb[i].y1;
        if (gap < 0 || gap > maxGap) continue;
        if (gap < bestGap) { bestGap = gap; best = j; }
    }
    return best;
}

// Which member of a cluster carries the feet: the LARGEST sprite (the
// kart's body), not the lowest one - a puff of dust joining the cluster a
// few px below the shadow would move the feet every time it appears and
// vanishes. Ties go to the lower one.
static inline bool groundFeetBetter(const GroundBox *cand, const GroundBox *cur)
{
    int ac = (cand->x1 - cand->x0 + 1) * (cand->y1 - cand->y0 + 1);
    int au = (cur->x1 - cur->x0 + 1) * (cur->y1 - cur->y0 + 1);
    if (ac != au) return ac > au;
    return cand->y1 > cur->y1;
}

#endif
