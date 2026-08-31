//=============================================================================
// Self-updater HTTP shell (issue #64) - 3DS-only, no policy here.
//
// GitHub asset downloads answer with a redirect to the CDN, so both entry
// points follow Location: hops themselves (httpc does not).
//=============================================================================
#ifndef _3DSUPDATENET_H_
#define _3DSUPDATENET_H_

#include <stddef.h>

// CDN redirect targets carry signed query strings well past 512 chars
#define UPDATE3DSNET_URL_BUF 1024

bool update3dsNetInit();
void update3dsNetExit();

// Detail of the last failure (stage + service result code), for error
// dialogs - field reports then carry the real cause, not a guess.
const char* update3dsNetLastError();

// GET https://api.github.com<path> into buf (NUL-terminated).
// Returns payload length, or a negative value on any failure.
int update3dsNetFetchApi(const char* path, char* buf, size_t bufSize);

// Stream 'url' into 'destPath'. 'progress' (optional) is called as bytes
// arrive; returning false cancels. total==0 when the server sent no size.
// Returns NULL on success or a short static error description.
const char* update3dsNetDownload(const char* url, const char* destPath,
                                 bool (*progress)(void* user, unsigned done,
                                                  unsigned total),
                                 void* user);

#endif
