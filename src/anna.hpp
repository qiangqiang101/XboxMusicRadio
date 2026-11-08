// "Xbox Music Radio" in FH2 ANNA's menu (D-pad down): Next Track, Previous Track and Playlist, greyed out unless
// the radio is on the Xbox Music station. Nothing without FH2 ANNA.
#pragma once

#include "fh2mod.h"

// From fh2mod_init, after the live players are registered: adds the entries from a thread of its own (it waits for
// the other mods to start, and the entries have to be in before the HUD loads to show greyed out).
void anna_start(const FH2ModApi* api, FH2Mod* mod);
