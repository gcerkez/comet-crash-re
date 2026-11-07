/*
 * CometCrashRecomp -- game-specific PRX and HLE override hooks.
 *
 * Comet Crash (NPUB30061) statically links PSGL and MultiStream and uses HLE
 * handlers for all 20 imported PS3 firmware libraries (195 functions), so no
 * LLE system PRX modules need to be loaded here.
 */

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

extern "C" void ps3_load_prx_modules(void)
{
}
