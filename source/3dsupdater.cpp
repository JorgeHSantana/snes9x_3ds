#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "3dsupdater.h"
#include "3dsupdatenet.h"
#include "3dslog.h"
#include "3dsbuildsha.h"
#include <errno.h>

#define API_BUF_SIZE     (64 * 1024)
#define CIA_TEMP_PATH    "sdmc:/3ds/snes9x_3ds/update.cia"
#define CIA_CHUNK_SIZE   (64 * 1024)

bool updater3dsIsCia()
{
    return !envIsHomebrew();
}

const char* updater3dsRunningSha()
{
    return BUILD_GIT_SHA;
}

void updater3dsCheck(int channel, Update3dsCheck& check)
{
    memset(&check, 0, sizeof(check));

    static char json[API_BUF_SIZE];
    char path[128];
    update3dsApiPath(channel, path, sizeof(path));

    int len = update3dsNetFetchApi(path, json, sizeof(json));
    if (len <= 0)
    {
        const char* detail = update3dsNetLastError();
        snprintf(check.error, sizeof(check.error),
                 "github unreachable: %s",
                 detail[0] != 0 ? detail : "unknown");
        return;
    }
    if (!update3dsParseRelease(json, (size_t)len, check.release))
    {
        snprintf(check.error, sizeof(check.error),
                 "unexpected release data");
        return;
    }
    check.ok = true;
    check.updateAvailable =
        update3dsIsNewer(updater3dsRunningSha(), check.release);
    log3dsWrite("[upd] check ch=%d: running %s, release %s (%s)", channel,
                updater3dsRunningSha(), check.release.sha,
                check.updateAvailable ? "update" : "up to date");
}

void updater3dsAutoCheck(int rememberedChannel, Update3dsCheck& check)
{
    // On the latest stable? Then stable is our channel and we are done.
    Update3dsCheck stable;
    updater3dsCheck(UPDATE3DS_CHANNEL_STABLE, stable);
    if (stable.ok && !stable.updateAvailable)
    {
        check = stable;
        return;
    }

    // On the latest nightly? Same - a nightly build stays a nightly.
    Update3dsCheck nightly;
    updater3dsCheck(UPDATE3DS_CHANNEL_NIGHTLY, nightly);
    if (nightly.ok && !nightly.updateAvailable)
    {
        check = nightly;
        return;
    }

    // Outdated on both: the last manual channel choice breaks the tie.
    check = (rememberedChannel == UPDATE3DS_CHANNEL_NIGHTLY) ? nightly
                                                             : stable;
}

// argv[0] from the homebrew loader ("sdmc:/3ds/.../snes9x_3ds.3dsx").
// Empty when the environment passed no argument list.
static void own3dsxPath(char* out, size_t outSize)
{
    out[0] = 0;
    const char* args = (const char*)envGetSystemArgList();
    if (args == NULL)
        return;
    u32 argc = *(const u32*)args;
    const char* argv0 = args + sizeof(u32);
    if (argc < 1 || argv0[0] == 0)
        return;
    // strip the device prefix: newlib's rename() chokes on "sdmc:" paths
    // (field-confirmed "could not stage" on hardware); the sd card is the
    // default device, so plain absolute paths work everywhere
    if (strncmp(argv0, "sdmc:", 5) == 0)
        argv0 += 5;
    if (argv0[0] == '/')
        snprintf(out, outSize, "%s", argv0);
}

// Reads the head and size of a downloaded file for update3dsVerifyImage.
static bool verifyDownloaded(const char* path, bool isCia)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return false;
    unsigned char head[8] = {0};
    size_t headLen = fread(head, 1, sizeof(head), f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size > 0 &&
           update3dsVerifyImage(head, headLen, (size_t)size, isCia);
}

// The running copy is in RAM, so the file swap is safe; the .old backup
// covers the moment between the two renames on a FAT card.
static const char* apply3dsx(const char* url,
                             bool (*progress)(void*, unsigned, unsigned),
                             void* user)
{
    char self[512], temp[528], old[528];
    own3dsxPath(self, sizeof(self));
    if (self[0] == 0)
        return "cannot locate the running .3dsx";
    snprintf(temp, sizeof(temp), "%s.upd", self);
    snprintf(old, sizeof(old), "%s.old", self);

    const char* err = update3dsNetDownload(url, temp, progress, user);
    if (err != NULL)
        return err;
    if (!verifyDownloaded(temp, false))
    {
        remove(temp);
        return "downloaded file failed verification";
    }

    remove(old);
    if (rename(self, old) != 0)
    {
        static char stageErr[64];
        snprintf(stageErr, sizeof(stageErr),
                 "could not stage the old version (errno %d)", errno);
        remove(temp);
        return stageErr;
    }
    if (rename(temp, self) != 0)
    {
        rename(old, self);      // roll back, keep a working emulator
        remove(temp);
        return "could not replace the .3dsx";
    }
    remove(old);
    log3dsWrite("[upd] replaced %s", self);
    return NULL;
}

static const char* applyCia(const char* url,
                            bool (*progress)(void*, unsigned, unsigned),
                            void* user)
{
    const char* err = update3dsNetDownload(url, CIA_TEMP_PATH, progress, user);
    if (err != NULL)
        return err;
    if (!verifyDownloaded(CIA_TEMP_PATH, true))
    {
        remove(CIA_TEMP_PATH);
        return "downloaded file failed verification";
    }

    if (R_FAILED(amInit()))
    {
        remove(CIA_TEMP_PATH);
        return "am service unavailable";
    }

    const char* result = NULL;
    Handle cia = 0;
    FILE* f = fopen(CIA_TEMP_PATH, "rb");
    if (f == NULL)
        result = "cannot reopen the download";
    else if (R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &cia)))
        result = "cia install rejected";
    else
    {
        static u8 chunk[CIA_CHUNK_SIZE];
        u64 offset = 0;
        size_t got;
        while (result == NULL &&
               (got = fread(chunk, 1, sizeof(chunk), f)) > 0)
        {
            u32 written = 0;
            if (R_FAILED(FSFILE_Write(cia, &written, offset, chunk,
                                      (u32)got, 0)) || written != got)
                result = "cia install write failed";
            offset += written;
        }
        if (result == NULL)
        {
            if (R_FAILED(AM_FinishCiaInstall(cia)))
                result = "cia install finalize failed";
            else
                log3dsWrite("[upd] cia installed (%llu bytes)",
                            (unsigned long long)offset);
        }
        else
        {
            AM_CancelCIAInstall(cia);
        }
    }
    if (f != NULL)
        fclose(f);
    amExit();
    remove(CIA_TEMP_PATH);
    return result;
}

const char* updater3dsApply(const Update3dsRelease& release,
                            bool (*progress)(void* user, unsigned done,
                                             unsigned total),
                            void* user)
{
    bool isCia = updater3dsIsCia();
    const char* url = update3dsAssetUrl(release, isCia);
    if (url == NULL)
        return "release has no file for this format";
    return isCia ? applyCia(url, progress, user)
                 : apply3dsx(url, progress, user);
}
