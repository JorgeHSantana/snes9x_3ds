#ifndef FLUSH_SPAN_H
#define FLUSH_SPAN_H

// What a CPU-written buffer still owes the GPU.
//
// The GPU reads vertex data from RAM, so what the CPU wrote must leave the
// CPU cache (GSPGPU_FlushDataCache) before the command list that draws it
// is submitted. Each flush is a call into the GSP service: about 40 us in
// Old 3DS mode whatever the size (console, 2026-11-12: 13 fewer calls a
// drawn frame took 0.5 ms off it). The frame used to flush every list
// whole at each submission and again when its half was swapped away: 21
// calls a drawn frame, 12 on a frame that drew nothing.
//
// A list is written front to back. This keeps how far it was flushed and
// whether a draw read it since it was last emptied, so a flush call goes
// out only for bytes a submitted draw can read and that no earlier call
// covered.

struct FlushSpan {
    unsigned int flushed;   // bytes from the list's start already flushed
    bool drawn;             // a draw read the list since flush_span_reset
};

// the list was emptied or moved to other memory
static inline void flush_span_reset(FlushSpan *s)
{
    s->flushed = 0;
    s->drawn = false;
}

static inline void flush_span_drawn(FlushSpan *s) { s->drawn = true; }

// Before a submission, with `written` bytes in the list. True when a flush
// call is due, for *bytes bytes from offset *from. A list no draw read owes
// nothing yet and keeps its bytes for later. A list shorter than what was
// flushed was wound back: the next writes land on flushed bytes, so the
// mark follows it down.
static inline bool flush_span_take(FlushSpan *s, unsigned int written, unsigned int *from, unsigned int *bytes)
{
    if (written < s->flushed)
        s->flushed = written;
    if (!s->drawn || written == s->flushed)
        return false;
    *from = s->flushed;
    *bytes = written - s->flushed;
    s->flushed = written;
    return true;
}

#endif
