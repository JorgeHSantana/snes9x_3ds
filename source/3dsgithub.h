#ifndef _3DSGITHUB_H_
#define _3DSGITHUB_H_

#include <stddef.h>

// Send the session log or the newest Luma crash dump as a comment on a
// GitHub issue (issue #80). The token and the issue's comments URL live
// in sd:/3ds/snes9x_3ds/github.env; without that file nothing is offered.

// re-reads the file; true when both keys are present
bool github3dsAvailable();

// Blocking: compose and post. Runs on a worker thread from the menu.
// Returns NULL on success (out = the comment's web URL) or a short
// error (out = detail, may be empty).
const char* github3dsSend(bool crashDump, char* out, size_t outSize);

// Screenshot of the top screen, both eyes (issue #77, Jorge): the menu's
// main thread captures first (it redraws the retained frame undimmed
// and encodes each eye to PNG in memory), the worker then posts one
// comment per eye, split into parts when the base64 passes GitHub's
// limit. Returns NULL on success or a short error.
bool github3dsCaptureScreens(char* err, size_t errSize);   // main thread
const char* github3dsSendScreens(char* out, size_t outSize);   // worker

#endif
