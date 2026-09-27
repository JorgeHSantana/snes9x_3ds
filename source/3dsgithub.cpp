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
