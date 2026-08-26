
#include "memmap.h"
#include "3dsgpu.h"
#include "3dslcd.h"
#include "3dssound.h"
#include "3dsinput.h"
#include "3dsmenu.h"
#include "3dsexit.h"
#include "3dsmsu.h"

aptHookCookie hookCookie;

void handleAptHook(APT_HookType hook, void* param)
{
    switch (hook) {
        case APTHOOK_ONEXIT:
            lcd3dsRestoreDefaultRate();
            GPU3DS.emulatorState = EMUSTATE_END;
            break;
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            msu3dsOnEvent(Msu1Event::AptSuspend);
            snd3dsRestoreCpuLimit();
            if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
                snd3dsStopPlaying(); // avoid hanging looped sample while HOME menu is open
                lcd3dsRestoreDefaultRate();
                if (settings3DS.ForceSRAMWriteOnPause || CPU.SRAMModified || CPU.AutoSaveTimer) {
                    S9xAutoSaveSRAM();
                }

                // HOME parks in the pause menu; closing the lid resumes
                // play seamlessly on wake instead (issue #45) - waking
                // into the dimmed menu backdrop read as a dark screen.
                // SRAM is saved either way, just above.
                if (hook == APTHOOK_ONSUSPEND) {
                    GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
                    input3dsRefreshTurboMode(false);
                }
            }

            break;
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            msu3dsOnEvent(Msu1Event::AptResume);
            snd3dsApplyCpuLimit();
            GPU3DS.gameScreenBufferDesync = true;
            menu3dsSetScreenDirty(true, true);
            if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
                // lid wake with no menu trip: restore what sleep tore down
                lcd3dsSetEmulationRate(settings3DS.TicksPerFrame);
                snd3dsStartPlaying();
            }
            break;
        default:
            break;
    }
}

void enableAptHooks() {
    aptHook(&hookCookie, handleAptHook, NULL);
}

void disableAptHooks() {
    aptUnhook(&hookCookie);
}
