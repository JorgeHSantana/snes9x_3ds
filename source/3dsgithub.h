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

#endif
