#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "3dsupdatenet.h"
#include "3dslog.h"

#define NET_MAX_REDIRECTS   8
#define NET_CHUNK_SIZE      (64 * 1024)

static bool netReady = false;
static char netLastError[96] = "";

const char* update3dsNetLastError()
{
    return netLastError;
}

static void netFail(const char* stage, Result rc)
{
    snprintf(netLastError, sizeof(netLastError), "%s (%08lx)", stage,
             (unsigned long)rc);
    log3dsWrite("[upd] %s", netLastError);
}

bool update3dsNetInit()
{
    if (netReady)
        return true;
    // 0 = default shared-memory size for the service's buffers
    netReady = R_SUCCEEDED(httpcInit(0));
    if (!netReady)
        log3dsWrite("[upd] httpcInit failed");
    return netReady;
}

void update3dsNetExit()
{
    if (netReady)
        httpcExit();
    netReady = false;
}

// Opens 'url' following redirects; on success the context is live and
// ready to download from. Returns NULL or a static error string.
static const char* netOpen(httpcContext* ctx, const char* url)
{
    char current[UPDATE3DSNET_URL_BUF];
    snprintf(current, sizeof(current), "%s", url);

    for (int hop = 0; hop < NET_MAX_REDIRECTS; hop++)
    {
        Result rc = httpcOpenContext(ctx, HTTPC_METHOD_GET, current, 1);
        if (R_FAILED(rc))
        {
            netFail("open", rc);
            return "connection open failed";
        }

        // GitHub's cert chain is not in the console's store; standard
        // homebrew practice. Integrity is re-checked by the image verify.
        httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify);
        httpcSetKeepAlive(ctx, HTTPC_KEEPALIVE_DISABLED);
        httpcAddRequestHeaderField(ctx, "User-Agent", "snes9x_3ds-updater");
        httpcAddRequestHeaderField(ctx, "Accept",
                                   "application/vnd.github+json, */*");

        rc = httpcBeginRequest(ctx);
        if (R_FAILED(rc))
        {
            netFail("request", rc);
            httpcCloseContext(ctx);
            return "request failed";
        }

        u32 status = 0;
        rc = httpcGetResponseStatusCode(ctx, &status);
        if (R_FAILED(rc))
        {
            netFail("response", rc);
            httpcCloseContext(ctx);
            return "no response";
        }

        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308)
        {
            char loc[UPDATE3DSNET_URL_BUF] = "";
            rc = httpcGetResponseHeader(ctx, "Location", loc, sizeof(loc));
            httpcCloseContext(ctx);
            if (R_FAILED(rc) || loc[0] == 0)
                return "redirect without location";
            snprintf(current, sizeof(current), "%s", loc);
            continue;
        }

        if (status != 200)
        {
            httpcCloseContext(ctx);
            snprintf(netLastError, sizeof(netLastError), "http status %lu",
                     (unsigned long)status);
            log3dsWrite("[upd] http status %lu for %.60s",
                        (unsigned long)status, current);
            return (status == 403 || status == 429) ? "rate limited, try later"
                 : (status == 404) ? "release not found"
                                   : "unexpected http status";
        }
        return NULL;
    }
    return "too many redirects";
}

int update3dsNetFetchApi(const char* path, char* buf, size_t bufSize)
{
    if (!netReady || buf == NULL || bufSize < 2)
        return -1;

    char url[UPDATE3DSNET_URL_BUF];
    snprintf(url, sizeof(url), "https://api.github.com%s", path);

    httpcContext ctx;
    const char* err = netOpen(&ctx, url);
    if (err != NULL)
    {
        log3dsWrite("[upd] api fetch: %s", err);
        return -2;
    }

    u32 total = 0;
    Result rc;
    do
    {
        u32 got = 0;
        rc = httpcDownloadData(&ctx, (u8*)buf + total,
                               (u32)(bufSize - 1 - total), &got);
        total += got;
        if (total >= bufSize - 1)
            break;                       // payload larger than our buffer
    } while (rc == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    httpcCloseContext(&ctx);
    if (R_FAILED(rc) && rc != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING)
    {
        netFail("api read", rc);
        return -3;
    }
    buf[total] = 0;
    return (int)total;
}

const char* update3dsNetDownload(const char* url, const char* destPath,
                                 bool (*progress)(void* user, unsigned done,
                                                  unsigned total),
                                 void* user)
{
    if (!netReady)
        return "network not initialized";

    httpcContext ctx;
    const char* err = netOpen(&ctx, url);
    if (err != NULL)
        return err;

    u32 total = 0;
    httpcGetDownloadSizeState(&ctx, NULL, &total);

    FILE* f = fopen(destPath, "wb");
    if (f == NULL)
    {
        httpcCloseContext(&ctx);
        return "cannot create file on sd";
    }

    static u8 chunk[NET_CHUNK_SIZE];
    u32 done = 0;
    Result rc;
    do
    {
        u32 got = 0;
        rc = httpcDownloadData(&ctx, chunk, sizeof(chunk), &got);
        if (got > 0 && fwrite(chunk, 1, got, f) != got)
        {
            fclose(f);
            httpcCloseContext(&ctx);
            remove(destPath);
            return "sd write failed (card full?)";
        }
        done += got;
        if (progress != NULL && !progress(user, done, total))
        {
            fclose(f);
            httpcCloseContext(&ctx);
            remove(destPath);
            return "cancelled";
        }
    } while (rc == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    fclose(f);
    httpcCloseContext(&ctx);

    if (R_FAILED(rc))
    {
        remove(destPath);
        log3dsWrite("[upd] download failed: %08lx", (unsigned long)rc);
        return "download interrupted";
    }
    if (total != 0 && done != total)
    {
        remove(destPath);
        return "download incomplete";
    }
    log3dsWrite("[upd] downloaded %lu bytes -> %s", (unsigned long)done,
                destPath);
    return NULL;
}
