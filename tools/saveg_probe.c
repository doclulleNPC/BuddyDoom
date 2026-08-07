// Savegame-layout probe.  Build-time only -- NOT part of buddydoom.exe.
//
// p_saveg.c memcpy's these structs straight into the save file, so if any of them
// changes size the old saves become unreadable.  Both build paths compile this,
// print the summed size, and bump VERSION_NUM in doomdef.h when the number moves,
// so stale saves are cleanly REJECTED ("bad version") instead of being loaded into
// a struct that no longer matches and crashing.
//
// LIVES IN tools/, NOT files/: build.sh compiles files/*.c into the engine, so a
// second main() there would collide with i_main.c and break the Linux build.
//
// build.sh generates an identical probe inline for the Linux build; this file
// exists so tools/bump_version_win.ps1 can do the same on Windows, where nothing
// used to run the check at all (a struct grown while building on Windows left
// VERSION_NUM untouched -- exactly what happened when mobj_t.strafecount landed).

#include "i_system.h"
#include "z_zone.h"
#include "p_local.h"
#include "doomstat.h"
#include "r_state.h"
#include <stdio.h>

int main (void)
{
    printf ("%lu\n", (unsigned long)(
	sizeof(player_t)     + sizeof(mobj_t)      + sizeof(ceiling_t) +
	sizeof(vldoor_t)     + sizeof(floormove_t) + sizeof(plat_t)    +
	sizeof(lightflash_t) + sizeof(strobe_t)    + sizeof(glow_t)));
    return 0;
}
