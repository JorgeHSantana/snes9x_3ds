#include "doctest.h"
#include "obj_lines.h"
#include <string.h>
#include <stdlib.h>

namespace {
struct Obj { short HPos; uint16_t VPos; uint16_t Name; uint8_t VFlip, HFlip, Priority, Palette, Size; };
struct Line { uint8_t RTOFlags; int16_t Tiles; struct { int8_t Sprite; uint8_t Line; } OBJ[32]; int OBJCount; };
const unsigned LINES = 240;

// The original S9xSetupOBJ normal case, transcribed: the oracle.
bool reference(const Obj* obj, ObjSizes sz, unsigned first, Line* lines, uint8_t* widths, uint8_t* vis)
{
    uint8_t cnt[LINES]; memset(cnt, 0, sizeof(cnt));
    for (unsigned i = 0; i < LINES; i++) { lines[i].RTOFlags = 0; lines[i].Tiles = 34; }
    bool wrap = false; uint8_t S = (uint8_t)first;
    do {
        int W = obj[S].Size ? sz.largeWidth : sz.smallWidth, H = obj[S].Size ? sz.largeHeight : sz.smallHeight;
        widths[S] = W; int HPos = obj[S].HPos; HPos = (HPos == -256) ? 256 : HPos;
        if (HPos > -W && HPos <= 256) {
            int v; if (HPos < 0) v = (W + HPos + 7) >> 3; else if (HPos + W >= 257) v = (257 - HPos + 7) >> 3; else v = W >> 3;
            vis[S] = v; uint8_t startY = obj[S].VPos & 0xff;
            if (startY < LINES && startY + H > 256) wrap = true;
            for (uint8_t line = 0; line < H; line++) {
                uint8_t Y = startY + line; if (Y >= LINES) continue;
                if (cnt[Y] < 32) { lines[Y].Tiles -= v; lines[Y].RTOFlags |= (lines[Y].Tiles < 0) ? 0x80 : 0;
                    lines[Y].OBJ[cnt[Y]].Sprite = S; lines[Y].OBJ[cnt[Y]].Line = obj[S].VFlip ? (line ^ (W - 1)) : line; cnt[Y]++; }
                else lines[Y].RTOFlags |= 0x40;
            }
        }
        S = (S + 1) & 0x7F;
    } while (S != first);
    for (unsigned Y = 0; Y < LINES; Y++) if (cnt[Y] < 32) lines[Y].OBJ[cnt[Y]].Sprite = -1;
    lines[0].OBJCount = cnt[0];
    for (unsigned Y = 1; Y < LINES; Y++) { lines[Y].RTOFlags |= lines[Y-1].RTOFlags; lines[Y].OBJCount = cnt[Y]; }
    return wrap;
}

bool sameLines(const Line* a, const Line* b, unsigned y0, unsigned y1)
{
    for (unsigned y = y0; y <= y1; y++) {
        if (a[y].RTOFlags != b[y].RTOFlags || a[y].Tiles != b[y].Tiles || a[y].OBJCount != b[y].OBJCount) return false;
        for (int i = 0; i < a[y].OBJCount; i++)
            if (a[y].OBJ[i].Sprite != b[y].OBJ[i].Sprite || a[y].OBJ[i].Line != b[y].OBJ[i].Line) return false;
        if (a[y].OBJCount < 32 && a[y].OBJ[a[y].OBJCount].Sprite != -1) return false;
    }
    return true;
}

void randomTable(Obj* obj, unsigned seed, bool crowded)
{
    srand(seed);
    for (int s = 0; s < 128; s++) {
        obj[s].HPos = (short)((rand() % 576) - 320);          // -320..255, covers -256 and off-screen
        obj[s].VPos = crowded ? (uint16_t)(100 + rand() % 8) : (uint16_t)(rand() % 256);
        obj[s].Size = rand() & 1; obj[s].VFlip = rand() & 1;
    }
}
}

TEST_CASE("obj lines: one full range equals the original setup, all size selects") {
    static Obj obj[128]; static Line ref[LINES], got[LINES];
    uint8_t cnt[LINES], wr[128], vr[128], wg[128], vg[128];
    for (unsigned sel = 0; sel < 8; sel++) for (unsigned seed = 1; seed < 4; seed++) {
        randomTable(obj, seed * 10 + sel, false);
        ObjSizes sz = obj_sizes(sel, false);
        bool a = reference(obj, sz, 5, ref, wr, vr);
        memset(got, 0x55, sizeof(got));
        bool b = obj_lines_build_range(obj, sz, 5, 0, LINES - 1, LINES, got, cnt, wg, vg);
        CHECK(a == b);
        CHECK(sameLines(ref, got, 0, LINES - 1));
        CHECK(memcmp(wr, wg, 128) == 0);
    }
}

TEST_CASE("obj lines: building in pieces gives the same lists as one pass") {
    static Obj obj[128]; static Line ref[LINES], got[LINES];
    uint8_t cnt[LINES], wr[128], vr[128], wg[128], vg[128];
    randomTable(obj, 77, false);
    ObjSizes sz = obj_sizes(3, false);
    reference(obj, sz, 40, ref, wr, vr);
    memset(got, 0x55, sizeof(got));
    obj_lines_build_range(obj, sz, 40, 0, 23, LINES, got, cnt, wg, vg);
    obj_lines_build_range(obj, sz, 40, 24, 106, LINES, got, cnt, wg, vg);
    obj_lines_build_range(obj, sz, 40, 107, 108, LINES, got, cnt, wg, vg);
    obj_lines_build_range(obj, sz, 40, 109, LINES - 1, LINES, got, cnt, wg, vg);
    CHECK(sameLines(ref, got, 0, LINES - 1));
}

TEST_CASE("obj lines: more than 32 sprites on a line sets range-over and keeps 32") {
    static Obj obj[128]; static Line ref[LINES], got[LINES];
    uint8_t cnt[LINES], wr[128], vr[128], wg[128], vg[128];
    randomTable(obj, 9, true);
    ObjSizes sz = obj_sizes(5, false);
    reference(obj, sz, 0, ref, wr, vr);
    obj_lines_build_range(obj, sz, 0, 90, 130, LINES, got, cnt, wg, vg);
    CHECK(sameLines(ref, got, 90, 130));
    CHECK(got[104].OBJCount == 32);
    CHECK((got[104].RTOFlags & 0x40) != 0);
}

TEST_CASE("obj lines: a range leaves the lines outside it untouched and reports the Y wrap") {
    static Obj obj[128]; static Line got[LINES];
    uint8_t cnt[LINES], wg[128], vg[128];
    memset(obj, 0, sizeof(obj));
    for (int s = 0; s < 128; s++) obj[s].HPos = 300;       // all off-screen
    obj[0].HPos = 10; obj[0].VPos = 230; obj[0].Size = 1;   // 32 rows from 230: 230..255 then 0..5
    memset(got, 0x55, sizeof(got));
    ObjSizes sz = obj_sizes(3, false);
    CHECK(obj_lines_build_range(obj, sz, 0, 100, 120, LINES, got, cnt, wg, vg) == true);    // the wrap is a property of the table, reported by every range
    CHECK(got[99].RTOFlags == 0x55);
    CHECK(got[121].RTOFlags == 0x55);
    CHECK(got[110].OBJCount == 0);
    CHECK(got[110].OBJ[0].Sprite == -1);
    obj_lines_build_range(obj, sz, 0, 0, 30, LINES, got, cnt, wg, vg);
    CHECK(got[5].OBJCount == 1);
    CHECK(got[5].OBJ[0].Line == 31);                         // row 26 lands on line 0, row 31 on line 5
    CHECK(got[6].OBJCount == 0);
}

TEST_CASE("obj lines validity: a mid-frame change rebuilds from its line, and the next frame rebuilds the lines above it") {
    ObjLinesValidity v; v.reset(); int r[2][2];
    // frame 1: change in vblank -> first section builds from 0
    CHECK(v.plan(0, 23, true, r) == 1);  CHECK(r[0][0] == 0);  CHECK(r[0][1] == 23);
    CHECK(v.plan(24, 106, false, r) == 1); CHECK(r[0][0] == 24); CHECK(r[0][1] == 106);
    // OBJ changes at line 107 (Mario Kart's OBSEL for the bottom half)
    CHECK(v.plan(107, 223, true, r) == 1); CHECK(r[0][0] == 107); CHECK(r[0][1] == 223);
    // frame 2, no change in vblank: lines 0..106 still hold the pre-change table -> rebuilt as reached
    CHECK(v.plan(0, 23, false, r) == 1);  CHECK(r[0][0] == 0);  CHECK(r[0][1] == 23);
    CHECK(v.plan(24, 106, false, r) == 1); CHECK(r[0][0] == 24); CHECK(r[0][1] == 106);
    CHECK(v.plan(107, 223, false, r) == 1);   // built last frame, but the run was dropped when the top was rebuilt: once more
    // frame 3, one section for the whole frame
    CHECK(v.plan(0, 223, false, r) == 0);
}

TEST_CASE("obj lines validity: a section straddling the valid range builds both missing sides") {
    ObjLinesValidity v; v.reset(); int r[2][2];
    CHECK(v.plan(50, 100, true, r) == 1);
    CHECK(v.plan(0, 150, false, r) == 2);
    CHECK(r[0][0] == 0);   CHECK(r[0][1] == 49);
    CHECK(r[1][0] == 101); CHECK(r[1][1] == 150);
    CHECK(v.from == 0); CHECK(v.to == 150);
    CHECK(v.plan(151, 223, false, r) == 1); CHECK(r[0][0] == 151);
    v.all(239);
    CHECK(v.plan(0, 223, false, r) == 0);
}
