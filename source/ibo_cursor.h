#ifndef IBO_CURSOR_H
#define IBO_CURSOR_H

// The tiled layers' index buffer, one private range a pass.
//
// A draw does not copy its indices: the GPU reads them when the command
// list runs, at the frame's end or at a frame split. A layer is drawn up
// to six times a frame (each eye: the base pass and two blur ghosts), and
// the occlusion and window culls write a different index list for each
// (they depend on the pass's parallax). Sharing one range, every draw of
// the layer read whatever the LAST pass wrote, with its own count.
//
// So each pass takes its range from a cursor that only moves forward
// inside the frame. Returns the first entry of a range of `need` entries
// and advances the cursor, or -1 when the buffer cannot hold it: the
// caller then draws the layer's fixed range unculled, whose contents are
// the same in every pass.
static inline int ibo_cursor_take(unsigned int *cursor, unsigned int need, unsigned int capacity)
{
    if (*cursor > capacity || need > capacity - *cursor)
        return -1;
    const int at = (int)*cursor;
    *cursor += need;
    return at;
}

// the cursor never starts inside the fixed ranges (entries 0..fixedEnd-1)
static inline void ibo_cursor_floor(unsigned int *cursor, unsigned int fixedEnd)
{
    if (*cursor < fixedEnd) *cursor = fixedEnd;
}

#endif
