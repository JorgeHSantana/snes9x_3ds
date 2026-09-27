#ifndef GITHUB_REPORT_H
#define GITHUB_REPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The comment body for issue #80's "Send log / Send crash dump": a
// Markdown header, then the payload in a code block, JSON-encoded into
// a fixed buffer. Pure functions, tested on the host.

// GitHub refuses comment bodies over 65536 characters.
#define GITHUB_COMMENT_MAX 65536
// what a log send carries at most (the tail of the file)
#define GITHUB_LOG_TAIL_MAX 60000

// The last `want` bytes of a `len`-byte file, moved forward to the start
// of a line so the cut never splits one. Returns the offset to read from.
static inline size_t github_tail_offset(const char* data, size_t len, size_t want)
{
    if (len <= want) return 0;
    size_t off = len - want;
    while (off < len && data[off - 1] != '\n') off++;
    return off;
}

// Standard base64 with padding. Returns the encoded length (without NUL),
// 0 when `out` cannot hold it.
static inline size_t github_base64(const uint8_t* in, size_t n, char* out, size_t cap)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((n + 2) / 3) * 4;
    if (cap < need + 1) return 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

// Appends `s` (n bytes) to a JSON string being written at out[*pos],
// escaping what JSON requires (control bytes as \u00XX, bytes >= 0x80
// passed through: the log is ASCII, the dump is base64). false = no room.
static inline bool github_json_append(char* out, size_t cap, size_t* pos, const char* s, size_t n)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char* esc = nullptr; char buf[7];
        switch (c) {
            case '"': esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            default:
                if (c < 0x20) { snprintf(buf, sizeof(buf), "\\u00%c%c", HEX[c >> 4], HEX[c & 15]); esc = buf; }
        }
        size_t need = esc ? strlen(esc) : 1;
        if (*pos + need >= cap) return false;
        if (esc) { memcpy(out + *pos, esc, need); *pos += need; }
        else out[(*pos)++] = (char)c;
    }
    out[*pos] = '\0';
    return true;
}

struct GithubHeader
{
    const char* version;    // "v2.3"
    const char* sha;        // "effa713"
    const char* model;      // "New 3DS XL"
    const char* mode;       // "New mode (804 MHz + L2)"
    const char* date;       // "09/26/26 19:54:00"
    const char* fileName;   // "debug_v2.3_session.log"
    size_t      fileSize;   // whole file
    size_t      sentBytes;  // what the payload covers (== fileSize when whole)
    const char* fence;      // "text" or "base64"
};

// The whole request body: {"body":"<markdown>"}. `payload` is the raw
// text (log tail) or base64 (dump). Returns the body length, 0 when it
// would not fit `cap` or the Markdown would pass GitHub's limit.
static inline size_t github_comment_json(const GithubHeader& h, const char* payload, size_t payloadLen,
                                         char* out, size_t cap)
{
    char head[512];
    int n = snprintf(head, sizeof(head), "**snes9x_3ds %s (%s)** \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 %s\n`%s`, %u bytes%s\n\n```%s\n",
        h.version, h.sha, h.model, h.mode, h.date, h.fileName, (unsigned)h.fileSize,
        h.sentBytes < h.fileSize ? " (the tail)" : "", h.fence);
    if (n < 0 || (size_t)n >= sizeof(head)) return 0;
    char tailNote[64]; int t = 0;
    if (h.sentBytes < h.fileSize)
        t = snprintf(tailNote, sizeof(tailNote), "\n... last %u bytes shown", (unsigned)h.sentBytes);
    const char* close = "\n```\n";
    if ((size_t)n + payloadLen + (size_t)t + strlen(close) > GITHUB_COMMENT_MAX) return 0;

    size_t pos = 0;
    const char* open = "{\"body\":\"";
    if (strlen(open) >= cap) return 0;
    memcpy(out, open, strlen(open)); pos = strlen(open);
    if (!github_json_append(out, cap, &pos, head, (size_t)n)) return 0;
    if (!github_json_append(out, cap, &pos, payload, payloadLen)) return 0;
    if (t > 0 && !github_json_append(out, cap, &pos, tailNote, (size_t)t)) return 0;
    if (!github_json_append(out, cap, &pos, close, strlen(close))) return 0;
    if (pos + 3 >= cap) return 0;
    out[pos++] = '"'; out[pos++] = '}'; out[pos] = '\0';
    return pos;
}

#endif
