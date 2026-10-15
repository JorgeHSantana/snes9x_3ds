#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "3dsgithub.h"
#include "3dslog.h"
#include "3dssettings.h"
#include "3dsgpu.h"
#include "3dsupdatenet.h"
#include "3dsupdater.h"
#include "github_env.h"
#include "github_report.h"
#include "3dsimpl.h"
#include "png_utils.h"

#define GITHUB_ENV_FILE  "github.env"
#define LUMA_DUMP_DIR    "sdmc:/luma/dumps/arm11"
#define DUMP_RAW_MAX     44000       // base64 of this stays under the log tail cap

static bool readEnv(GithubEnv& env)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", settings3DS.RootDir, GITHUB_ENV_FILE);
    FILE* f = fopen(path, "rb");
    if (f == NULL) return false;
    char text[1024];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    const bool ok = env.parse(text);
    memset(text, 0, sizeof(text));
    return ok;
}

bool github3dsAvailable()
{
    GithubEnv env;
    const bool ok = readEnv(env);
    memset(&env, 0, sizeof(env));
    return ok;
}

static const char* modelName()
{
    switch (GPU3DS.model) {
        case CFG_MODEL_3DS:    return "Old 3DS";
        case CFG_MODEL_3DSXL:  return "Old 3DS XL";
        case CFG_MODEL_N3DS:   return "New 3DS";
        case CFG_MODEL_2DS:    return "2DS";
        case CFG_MODEL_N3DSXL: return "New 3DS XL";
        case CFG_MODEL_N2DSXL: return "New 2DS XL";
        default:               return "unknown model";
    }
}

static const char* clockMode()
{
    if (!settings3DS.isNew3DS) return "268 MHz (Old hardware)";
    return settings3DS.Overclock ? "New mode (804 MHz + L2)" : "Old mode (268 MHz, no L2)";
}

// newest *.dmp by modification time; false when none
static bool newestDump(char* path, size_t pathSize, char* name, size_t nameSize)
{
    DIR* d = opendir(LUMA_DUMP_DIR);
    if (d == NULL) return false;
    time_t best = 0; bool found = false;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcasecmp(e->d_name + n - 4, ".dmp") != 0) continue;
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", LUMA_DUMP_DIR, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!found || st.st_mtime >= best) {
            best = st.st_mtime; found = true;
            snprintf(path, pathSize, "%s", full);
            snprintf(name, nameSize, "%s", e->d_name);
        }
    }
    closedir(d);
    return found;
}

// the comment's web URL out of the reply ("html_url":"...")
static void replyUrl(const char* reply, char* out, size_t outSize)
{
    out[0] = '\0';
    const char* k = strstr(reply, "\"html_url\":\"");
    if (k == NULL) return;
    k += 12;
    const char* end = strchr(k, '"');
    if (end == NULL) return;
    size_t n = (size_t)(end - k);
    if (n >= outSize) n = outSize - 1;
    memcpy(out, k, n); out[n] = '\0';
}

const char* github3dsSend(bool crashDump, char* out, size_t outSize)
{
    out[0] = '\0';
    GithubEnv env;
    if (!readEnv(env)) return "github.env missing or incomplete";

    // one arena for the file, the payload and the JSON body: linear memory
    // is roomy, the heap is not (issue #73)
    const size_t rawCap = GITHUB_LOG_TAIL_MAX + 1, payloadCap = GITHUB_LOG_TAIL_MAX + 1, bodyCap = GITHUB_COMMENT_MAX + 2048;
    char* arena = (char*)linearAlloc(rawCap + payloadCap + bodyCap + 4096);
    if (arena == NULL) return "out of memory";
    char* raw = arena; char* payload = arena + rawCap; char* body = payload + payloadCap; char* reply = body + bodyCap;
    const char* err = NULL;

    GithubHeader h = {};
    h.version = settings3dsGetAppVersion("v");
    h.sha = updater3dsRunningSha();
    h.model = modelName();
    h.mode = clockMode();
    h.date = log3dsGetCurrentDate();
    char path[PATH_MAX], name[NAME_MAX + 1];
    size_t payloadLen = 0;

    if (crashDump) {
        if (!newestDump(path, sizeof(path), name, sizeof(name))) { err = "no crash dump in luma/dumps/arm11"; goto done; }
        FILE* f = fopen(path, "rb");
        if (f == NULL) { err = "cannot read the crash dump"; goto done; }
        fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
        size_t take = size > DUMP_RAW_MAX ? DUMP_RAW_MAX : (size_t)size;
        size_t got = fread(raw, 1, take, f);
        fclose(f);
        h.fileName = name; h.fileSize = (size_t)size; h.sentBytes = got; h.fence = "base64";
        payloadLen = github_base64((const uint8_t*)raw, got, payload, payloadCap);
        if (payloadLen == 0 && got > 0) { err = "dump too large"; goto done; }
    } else {
        log3dsFlush();
        snprintf(name, sizeof(name), "debug_%s_session.log", settings3dsGetAppVersion("v"));
        snprintf(path, sizeof(path), "%s/%s", settings3DS.RootDir, name);
        FILE* f = fopen(path, "rb");
        if (f == NULL) { err = "no session log (enable logging first)"; goto done; }
        fseek(f, 0, SEEK_END); long size = ftell(f);
        size_t want = (size_t)size > GITHUB_LOG_TAIL_MAX ? GITHUB_LOG_TAIL_MAX : (size_t)size;
        fseek(f, (long)((size_t)size - want), SEEK_SET);
        size_t got = fread(raw, 1, want, f);
        fclose(f);
        // a partial read starts mid-line: skip to the first line start
        size_t off = 0;
        if ((size_t)size > want) {
            const char* nl = (const char*)memchr(raw, '\n', got);
            off = nl ? (size_t)(nl - raw) + 1 : got;
        }
        memcpy(payload, raw + off, got - off); payloadLen = got - off; payload[payloadLen] = '\0';
        h.fileName = name; h.fileSize = (size_t)size; h.sentBytes = payloadLen; h.fence = "text";
    }

    {
        size_t bodyLen = github_comment_json(h, payload, payloadLen, body, bodyCap);
        if (bodyLen == 0) { err = "comment would exceed GitHub's limit"; goto done; }
        if (!update3dsNetInit()) { snprintf(out, outSize, "%s", update3dsNetLastError()); err = "network unavailable"; goto done; }
        int status = update3dsNetPostJson(env.url, env.token, body, reply, 4096);
        log3dsWrite("[github] %s: %u bytes posted, http %d", crashDump ? "crash dump" : "log", (unsigned)bodyLen, status);
        if (status == 201) replyUrl(reply, out, outSize);
        else if (status < 0) { snprintf(out, outSize, "%s", update3dsNetLastError()); err = "request failed"; }
        else { snprintf(out, outSize, "http %d", status); err = status == 401 || status == 403 ? "token refused (check github.env)" : status == 404 ? "issue not found (check the URL)" : "GitHub refused the comment"; }
    }
done:
    memset(&env, 0, sizeof(env));
    linearFree(arena);
    return err;
}


// ---------------------------------------------------------------------
// Screenshot send (issue #77)

struct ScreenPng { uint8_t* data; size_t len; };
static ScreenPng s_shot[2];   // left, right
static int s_shotCount = 0;

struct PngMem { uint8_t* buf; size_t len, cap; bool overflow; };
static void pngMemWrite(png_structp png, png_bytep data, png_size_t n)
{
    PngMem* m = (PngMem*)png_get_io_ptr(png);
    if (m->len + n > m->cap) { m->overflow = true; return; }
    memcpy(m->buf + m->len, data, n); m->len += n;
}
static void pngMemFlush(png_structp) {}

// one top-screen framebuffer (400x240, column-major, rotated) to a PNG in `out`
static bool encodeEye(gfx3dSide_t side, PngMem* out, char* err, size_t errSize)
{
    u16 fbW = 0, fbH = 0;
    const u8* fb = gfxGetFramebuffer(GFX_TOP, side, &fbW, &fbH);
    if (fb == NULL) { snprintf(err, errSize, "no framebuffer"); return false; }
    int bpp = 2; bool bgr8 = false;
    switch (gfxGetScreenFormat(GFX_TOP)) {
        case GSP_RGBA8_OES: bpp = 4; break;
        case GSP_BGR8_OES:  bpp = 3; bgr8 = true; break;
        default:            bpp = 2; break;
    }
    const int W = 400, H = 240;
    uint8_t* row = (uint8_t*)malloc(W * 3);
    if (row == NULL) { snprintf(err, errSize, "out of memory"); return false; }
    PngWriteHandle png;
    if (!png.isValid() || !png.createInfo()) { free(row); snprintf(err, errSize, "libpng init failed"); return false; }
    if (setjmp(png_jmpbuf(png.getPng()))) { free(row); snprintf(err, errSize, "libpng error"); return false; }
    png_set_write_fn(png.getPng(), out, pngMemWrite, pngMemFlush);
    png_set_compression_level(png.getPng(), 4);
    png_set_IHDR(png.getPng(), png.getInfo(), W, H, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png.getPng(), png.getInfo());
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const u8* p = fb + ((size_t)x * 240 + (239 - y)) * bpp;
            uint8_t r, g, b;
            if (bpp == 4)      { r = p[3]; g = p[2]; b = p[1]; }
            else if (bgr8)     { b = p[0]; g = p[1]; r = p[2]; }
            else { const u16 v = (u16)(p[0] | (p[1] << 8)); r = (uint8_t)(((v >> 11) & 31) * 255 / 31); g = (uint8_t)(((v >> 5) & 63) * 255 / 63); b = (uint8_t)((v & 31) * 255 / 31); }
            row[x * 3] = r; row[x * 3 + 1] = g; row[x * 3 + 2] = b;
        }
        png_write_row(png.getPng(), row);
    }
    png_write_end(png.getPng(), NULL);
    free(row);
    if (out->overflow) { snprintf(err, errSize, "png too large"); return false; }
    return true;
}

bool github3dsCaptureScreens(char* err, size_t errSize)
{
    err[0] = '\0';
    for (int i = 0; i < 2; i++) { if (s_shot[i].data) { linearFree(s_shot[i].data); s_shot[i].data = NULL; } s_shot[i].len = 0; }
    s_shotCount = 0;
    if (!settings3DS.isRomLoaded) { snprintf(err, errSize, "no game loaded"); return false; }
    // the paused frame, undimmed, into both framebuffers of both eyes (the
    // 3D editor's preview path); then the CPU reads what the GPU wrote
    impl3dsStereoPreviewFrame(-1, -1, false);
    const bool stereo = gpu3dsIs3DAvailable() && osGet3DSliderState() != 0.0f && gpu3dsGetIOD() != 0.0f;
    impl3dsInvalidateScreen(GFX_TOP, stereo, false);
    const int eyes = stereo ? 2 : 1;
    for (int i = 0; i < eyes; i++) {
        PngMem m = { (uint8_t*)linearAlloc(400 * 240 * 3), 0, 400 * 240 * 3, false };
        if (m.buf == NULL) { snprintf(err, errSize, "out of memory"); return false; }
        if (!encodeEye(i == 0 ? GFX_LEFT : GFX_RIGHT, &m, err, errSize)) { linearFree(m.buf); return false; }
        s_shot[i].data = m.buf; s_shot[i].len = m.len; s_shotCount = i + 1;
        log3dsWrite("[github] screenshot %s eye: %u bytes png", i == 0 ? "left" : "right", (unsigned)m.len);
    }
    return true;
}

const char* github3dsSendScreens(char* out, size_t outSize)
{
    out[0] = '\0';
    if (s_shotCount == 0) return "nothing captured";
    GithubEnv env;
    if (!readEnv(env)) return "github.env missing or incomplete";
    const size_t partChars = 57000;   // base64 per comment, under GitHub's 65536 with the header
    const size_t b64Cap = 400 * 240 * 3 * 4 / 3 + 64, bodyCap = GITHUB_COMMENT_MAX + 2048;
    char* arena = (char*)linearAlloc(b64Cap + bodyCap + 4096);
    if (arena == NULL) { memset(&env, 0, sizeof(env)); return "out of memory"; }
    char* b64 = arena; char* body = arena + b64Cap; char* reply = body + bodyCap;
    const char* err = NULL;
    if (!update3dsNetInit()) { snprintf(out, outSize, "%s", update3dsNetLastError()); err = "network unavailable"; goto done; }
    for (int i = 0; i < s_shotCount && err == NULL; i++) {
        const size_t b64Len = github_base64(s_shot[i].data, s_shot[i].len, b64, b64Cap);
        if (b64Len == 0) { err = "png too large"; break; }
        const int parts = (int)((b64Len + partChars - 1) / partChars);
        for (int p = 0; p < parts && err == NULL; p++) {
            // the Effects settings in force ride along: what a screenshot is
            // usually about (issue #77)
            char name[200];
            char fx[128];
            snprintf(fx, sizeof(fx), " | shadow rows=%03x q=%d soft=%d | glow src=%d rows=%03x pal=%02x th=%d st=%d sp=%d halo=%d pers=%d | color %d/%d/%d",
                GPU3DS.stereoShadowRows, GPU3DS.stereoShadowQuality, GPU3DS.stereoShadowSoft,
                GPU3DS.stereoGlowSource, GPU3DS.stereoGlowRows, GPU3DS.stereoGlowPal & 0xFF, GPU3DS.stereoGlowThresh, GPU3DS.stereoGlowStrength,
                GPU3DS.stereoGlowSpread, GPU3DS.stereoGlowSoft, GPU3DS.stereoGlowPersist,
                settings3DS.ColorSat, settings3DS.ColorCon, settings3DS.ColorBri);
            if (parts > 1) snprintf(name, sizeof(name), "screen_%s.png (part %d/%d)%s", i == 0 ? "left" : "right", p + 1, parts, fx);
            else           snprintf(name, sizeof(name), "screen_%s.png%s", i == 0 ? "left" : "right", fx);
            GithubHeader h = {};
            h.version = settings3dsGetAppVersion("v"); h.sha = updater3dsRunningSha();
            h.model = modelName(); h.mode = clockMode(); h.date = log3dsGetCurrentDate();
            h.fileName = name; h.fileSize = s_shot[i].len; h.sentBytes = s_shot[i].len; h.fence = "base64";
            const size_t off = (size_t)p * partChars, take = b64Len - off > partChars ? partChars : b64Len - off;
            const size_t bodyLen = github_comment_json(h, b64 + off, take, body, bodyCap);
            if (bodyLen == 0) { err = "comment would exceed GitHub's limit"; break; }
            const int status = update3dsNetPostJson(env.url, env.token, body, reply, 4096);
            log3dsWrite("[github] screenshot %s part %d/%d: %u bytes posted, http %d", i == 0 ? "left" : "right", p + 1, parts, (unsigned)bodyLen, status);
            if (status == 201) replyUrl(reply, out, outSize);
            else if (status < 0) { snprintf(out, outSize, "%s", update3dsNetLastError()); err = "request failed"; }
            else { snprintf(out, outSize, "http %d", status); err = status == 401 || status == 403 ? "token refused (check github.env)" : status == 404 ? "issue not found (check the URL)" : "GitHub refused the comment"; }
        }
    }
done:
    memset(&env, 0, sizeof(env));
    linearFree(arena);
    for (int i = 0; i < 2; i++) { if (s_shot[i].data) { linearFree(s_shot[i].data); s_shot[i].data = NULL; } s_shot[i].len = 0; }
    s_shotCount = 0;
    return err;
}
