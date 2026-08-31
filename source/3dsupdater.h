//=============================================================================
// Self-updater orchestration (issue #64) - 3DS-only.
//
// Ties the pure core (3dsupdate.h) to the network shell (3dsupdatenet.h)
// and applies the result: a 3dsx build swaps its own file on the SD card
// (the running copy lives in RAM), a CIA build installs over the running
// title via AM and takes effect on the next launch.
//=============================================================================
#ifndef _3DSUPDATER_H_
#define _3DSUPDATER_H_

#include "3dsupdate.h"

struct Update3dsCheck
{
    bool             ok;             // the check itself succeeded
    bool             updateAvailable;
    Update3dsRelease release;
    char             error[64];      // human-readable when !ok
};

// True when running as an installed title (CIA), false for 3dsx homebrew.
bool updater3dsIsCia();

// Short sha this binary was built from (BUILD_GIT_SHA).
const char* updater3dsRunningSha();

// Queries the channel's latest release and compares against the running
// build. Network must be up; failures land in check.error.
void updater3dsCheck(int channel, Update3dsCheck& check);

// Startup flavor: updates the channel the RUNNING build belongs to.
// Whichever channel's latest sha matches the running build is "current"
// (up to date, no offer); a build matching neither is outdated and
// follows 'rememberedChannel' (the user's last manual choice).
void updater3dsAutoCheck(int rememberedChannel, Update3dsCheck& check);

// Downloads, verifies and applies 'release' for the running format.
// progress: return false to cancel. Returns NULL on success or a short
// error message. On 3dsx success the file on SD is already the new
// version; on CIA success the title is installed - both need a restart.
const char* updater3dsApply(const Update3dsRelease& release,
                            bool (*progress)(void* user, unsigned done,
                                             unsigned total),
                            void* user);

#endif
