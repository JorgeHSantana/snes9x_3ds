#ifndef OBJ_LINES_H
#define OBJ_LINES_H

#include <stdint.h>

// Per-scanline sprite lists (GFX.OBJLines), built from the OBJ table in
// priority order: which sprites touch each line, which sprite row each
// shows, the 34-tile budget and the range/time-over flags (cumulative
// down the frame). S9xSetupOBJ built all 240 lines on every OBJ change;
// Mario Kart changes the table four times a frame (OBSEL and the OAM
// upload, once per split-screen half) and paid four full passes, 40% of
// its section-render time. The renderer only ever reads the lines of the
// section it draws, so the lists are now built per line range on demand:
// a change invalidates from the current line down, and each section
// builds the lines it is about to draw (see S9xUpdateScreenHardware).
//
// Template on the core's types so the host tests drive it with plain
// structs: ObjT has HPos (signed), VPos, Size, VFlip; LineT has RTOFlags,
// Tiles, OBJ[32]{Sprite, Line}, OBJCount.
//
// Lines [y0, y1] are rebuilt (y1 < screenLines); lines outside keep what
// they hold. Line y0's RTO flags continue the chain from line y0-1 as it
// stands, which for a mid-frame change is the state those lines were
// drawn with. Returns whether any sprite's visible rows wrap past
// scanline 255 (the caller then takes the per-scanline draw path).

struct ObjSizes { int smallWidth, smallHeight, largeWidth, largeHeight; };

static inline ObjSizes obj_sizes(unsigned int sizeSelect, bool interlaceSprites)
{
    static const int table[8][4] = {
        {8, 8, 16, 16}, {8, 8, 32, 32}, {8, 8, 64, 64}, {16, 16, 32, 32},
        {16, 16, 64, 64}, {32, 32, 64, 64}, {16, 32, 32, 64}, {16, 32, 32, 32} };
    const int index = sizeSelect > 7 ? 5 : (int)sizeSelect;
    ObjSizes s = { table[index][0], table[index][1], table[index][2], table[index][3] };
    if (interlaceSprites) { s.smallHeight >>= 1; s.largeHeight >>= 1; }
    return s;
}

template <typename ObjT, typename LineT>
static inline bool obj_lines_build_range(const ObjT* obj, ObjSizes sz, unsigned int firstSprite,
    unsigned int y0, unsigned int y1, unsigned int screenLines,
    LineT* lines, uint8_t* lineCount, uint8_t* widths, uint8_t* visibleTiles)
{
    if (y1 >= screenLines) y1 = screenLines - 1;
    if (y0 > y1) return false;

    for (unsigned int y = y0; y <= y1; y++) {
        lineCount[y] = 0;
        lines[y].RTOFlags = 0;
        lines[y].Tiles = 34;
    }

    bool yWrap = false;
    unsigned int s = firstSprite & 0x7F;
    do {
        const int width  = obj[s].Size ? sz.largeWidth  : sz.smallWidth;
        const int height = obj[s].Size ? sz.largeHeight : sz.smallHeight;
        widths[s] = (uint8_t)width;

        int hpos = obj[s].HPos;
        hpos = (hpos == -256) ? 256 : hpos;

        if (hpos > -width && hpos <= 256) {
            int visible;
            if (hpos < 0) visible = (width + hpos + 7) >> 3;
            else if (hpos + width >= 257) visible = (257 - hpos + 7) >> 3;
            else visible = width >> 3;
            visibleTiles[s] = (uint8_t)visible;

            const unsigned int startY = obj[s].VPos & 0xff;
            if (startY < screenLines && startY + height > 256) yWrap = true;

            // Rows land on y = (startY + row) & 0xFF: one ascending run from
            // startY, and past scanline 255 a second run from line 0. Only
            // the rows whose line falls inside [y0, y1] are visited.
            const int firstRun = 256 - (int) startY;        // rows before the wrap
            int runs[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };  // {rowBase, yLo, yHi}
            int nRuns = 0;
            {
                const int lo = (int) y0 > (int) startY ? (int) y0 : (int) startY;
                const int hiRows = height < firstRun ? height : firstRun;
                const int hi = (int) y1 < (int) startY + hiRows - 1 ? (int) y1 : (int) startY + hiRows - 1;
                if (lo <= hi) { runs[nRuns][0] = -(int) startY; runs[nRuns][1] = lo; runs[nRuns][2] = hi; nRuns++; }
            }
            if (height > firstRun) {
                const int lo = (int) y0;
                const int hi = (int) y1 < height - firstRun - 1 ? (int) y1 : height - firstRun - 1;
                if (lo <= hi) { runs[nRuns][0] = firstRun; runs[nRuns][1] = lo; runs[nRuns][2] = hi; nRuns++; }
            }
            for (int r = 0; r < nRuns; r++) {
                for (int y = runs[r][1]; y <= runs[r][2]; y++) {
                    const int line = y + runs[r][0];
                    if (lineCount[y] < 32) {
                        lines[y].Tiles -= visible;
                        lines[y].RTOFlags |= (lines[y].Tiles < 0) ? 0x80 : 0;
                        const int n = lineCount[y];
                        lines[y].OBJ[n].Sprite = (int8_t)s;
                        // Yes, Width not Height: sprites with H=2*W flip as two WxW sprites.
                        lines[y].OBJ[n].Line = (uint8_t)(obj[s].VFlip ? (line ^ (width - 1)) : line);
                        lineCount[y]++;
                    } else {
                        lines[y].RTOFlags |= 0x40;
                    }
                }
            }
        }
        s = (s + 1) & 0x7F;
    } while (s != (firstSprite & 0x7F));

    for (unsigned int y = y0; y <= y1; y++) {
        if (lineCount[y] < 32) lines[y].OBJ[lineCount[y]].Sprite = -1;
        lines[y].OBJCount = lineCount[y];
        if (y > 0) lines[y].RTOFlags |= lines[y - 1].RTOFlags;
    }
    return yWrap;
}

// Which lines to (re)build before a section draws [s, e]. Lines
// [from, to] hold lists for the current OBJ table; a change at line s
// makes everything else stale (the lines above s were drawn with the old
// table and stay as they are until the next frame reaches them). Sections
// arrive in order and frames restart at 0, so at most two ranges are
// missing: above `from` and below `to`.
struct ObjLinesValidity {
    int from, to;          // inclusive; from > to means nothing valid
    void reset() { from = 1; to = 0; }
    void all(int lastLine) { from = 0; to = lastLine; }
    // Returns the number of ranges to build (0..2) in r[i][0..1]; updates the state.
    // The valid range stays one contiguous run: a section that does not
    // touch it replaces it (the run it leaves behind is rebuilt when the
    // frame reaches it again - one redundant build, never a stale line).
    int plan(int s, int e, bool objChanged, int r[2][2]) {
        int n = 0;
        if (objChanged || from > to || e < from - 1 || s > to + 1) { r[0][0] = s; r[0][1] = e; from = s; to = e; return 1; }
        if (s < from) { r[n][0] = s; r[n][1] = from - 1; n++; from = s; }
        if (e > to)   { r[n][0] = to + 1; r[n][1] = e; n++; to = e; }
        return n;
    }
};

#endif
