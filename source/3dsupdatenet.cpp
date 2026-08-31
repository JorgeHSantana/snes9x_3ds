// Self-updater HTTP shell over curl + mbedtls (issue #64).
//
// The console's own SSL module cannot complete a TLS handshake with
// GitHub any more (its cipher suites predate GitHub's ECDHE-only policy;
// field-confirmed as httpc error d8a0a03c on an Old 3DS), so this links
// its own TLS stack - the same route Universal-Updater takes. curl also
// follows the release-asset CDN redirects for us.

#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "3dsupdatenet.h"
#include "3dslog.h"

#define SOC_BUFFER_SIZE  0x100000     // SOC service requirement: 1MB, 4K-aligned

static bool netReady = false;
static u32* socBuffer = NULL;
static char netLastError[96] = "";

const char* update3dsNetLastError()
{
    return netLastError;
}

static void netFail(const char* stage, CURLcode code)
{
    snprintf(netLastError, sizeof(netLastError), "%s (%d %s)", stage,
             (int)code, curl_easy_strerror(code));
    log3dsWrite("[upd] %s", netLastError);
}

bool update3dsNetInit()
{
    if (netReady)
        return true;

    socBuffer = (u32*)memalign(0x1000, SOC_BUFFER_SIZE);
    if (socBuffer == NULL)
    {
        snprintf(netLastError, sizeof(netLastError), "soc buffer alloc");
        return false;
    }
    Result rc = socInit(socBuffer, SOC_BUFFER_SIZE);
    if (R_FAILED(rc))
    {
        snprintf(netLastError, sizeof(netLastError), "socInit (%08lx)",
                 (unsigned long)rc);
        log3dsWrite("[upd] %s", netLastError);
        free(socBuffer);
        socBuffer = NULL;
        return false;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    netReady = true;
    return true;
}

void update3dsNetExit()
{
    if (!netReady)
        return;
    curl_global_cleanup();
    socExit();
    free(socBuffer);
    socBuffer = NULL;
    netReady = false;
}

// Common transfer setup. The console has no usable CA store, so peer
// verification stays off - integrity is re-checked by the image verify.
static void netSetup(CURL* c, const char* url)
{
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "snes9x_3ds-updater");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 128L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 65536L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
}

struct MemSink
{
    char*  buf;
    size_t cap;
    size_t len;
};

static size_t memWrite(char* data, size_t size, size_t nmemb, void* user)
{
    MemSink* sink = (MemSink*)user;
    size_t n = size * nmemb;
    if (sink->len + n > sink->cap - 1)
        n = sink->cap - 1 - sink->len;          // truncate, keep going
    memcpy(sink->buf + sink->len, data, n);
    sink->len += n;
    return size * nmemb;
}

int update3dsNetFetchApi(const char* path, char* buf, size_t bufSize)
{
    if (!netReady || buf == NULL || bufSize < 2)
        return -1;

    char url[UPDATE3DSNET_URL_BUF];
    snprintf(url, sizeof(url), "https://api.github.com%s", path);

    CURL* c = curl_easy_init();
    if (c == NULL)
        return -1;

    MemSink sink = { buf, bufSize, 0 };
    netSetup(c, url);
    struct curl_slist* headers =
        curl_slist_append(NULL, "Accept: application/vnd.github+json");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, memWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);

    CURLcode code = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);

    if (code != CURLE_OK)
    {
        if (code == CURLE_HTTP_RETURNED_ERROR)
            snprintf(netLastError, sizeof(netLastError), "http status %ld",
                     status);
        else
            netFail("api", code);
        return -2;
    }
    buf[sink.len] = 0;
    return (int)sink.len;
}

struct FileSink
{
    FILE*    f;
    unsigned done;
    bool   (*progress)(void* user, unsigned done, unsigned total);
    void*    user;
    unsigned total;
    bool     writeError;
    bool     cancelled;
};

static size_t fileWrite(char* data, size_t size, size_t nmemb, void* user)
{
    FileSink* sink = (FileSink*)user;
    size_t n = size * nmemb;
    if (fwrite(data, 1, n, sink->f) != n)
    {
        sink->writeError = true;
        return 0;                               // aborts the transfer
    }
    sink->done += n;
    if (sink->progress != NULL &&
        !sink->progress(sink->user, sink->done, sink->total))
    {
        sink->cancelled = true;
        return 0;
    }
    return n;
}

static int xferInfo(void* user, curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t, curl_off_t)
{
    FileSink* sink = (FileSink*)user;
    if (dltotal > 0)
        sink->total = (unsigned)dltotal;
    (void)dlnow;
    return 0;
}

const char* update3dsNetDownload(const char* url, const char* destPath,
                                 bool (*progress)(void* user, unsigned done,
                                                  unsigned total),
                                 void* user)
{
    if (!netReady)
        return "network not initialized";

    FILE* f = fopen(destPath, "wb");
    if (f == NULL)
        return "cannot create file on sd";

    CURL* c = curl_easy_init();
    if (c == NULL)
    {
        fclose(f);
        remove(destPath);
        return "curl init failed";
    }

    FileSink sink = { f, 0, progress, user, 0, false, false };
    netSetup(c, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fileWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferInfo);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);

    CURLcode code = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    fclose(f);

    if (sink.cancelled)
    {
        remove(destPath);
        return "cancelled";
    }
    if (sink.writeError)
    {
        remove(destPath);
        return "sd write failed (card full?)";
    }
    if (code != CURLE_OK)
    {
        remove(destPath);
        if (code == CURLE_HTTP_RETURNED_ERROR)
        {
            snprintf(netLastError, sizeof(netLastError), "http status %ld",
                     status);
            return "unexpected http status";
        }
        netFail("download", code);
        return "download interrupted";
    }
    if (sink.total != 0 && sink.done != sink.total)
    {
        remove(destPath);
        return "download incomplete";
    }
    log3dsWrite("[upd] downloaded %u bytes -> %s", sink.done, destPath);
    return NULL;
}
