// The "Xbox Music" radio station: unlocks it and feeds it the music folder.
#pragma once

#include "fh2mod.h"
#include "library.hpp"
#include "live.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RadioOptions {
    bool shuffle = true;
    bool trace   = false;   // log station changes and the station's availability checks
};

// Hooks and switches on what puts the station in the radio's list (the gate and the lock). Call it first, as soon as
// the game's code is ready: the list is built once, early.
bool radio_install_gate(const FH2ModApi* api, FH2Mod* mod, uintptr_t base, const RadioOptions& opt);
// Hooks the rest of the station (switched on with the mod's other hooks) and starts the player thread.
// False if a hook couldn't be made.
bool radio_install();
// The playlist the game chose (Audio Options, or the saved one at startup); empty = All music.
void radio_select_playlist(const std::wstring& id);
std::wstring radio_selected_playlist();   // empty: none chosen (all music)
// Skip to the next song, or back to the one before.
void radio_next();
void radio_previous();
// The radio is tuned to the Xbox Music station.
bool radio_on_air();
std::string radio_status();
// The live players registered (live.hpp), in Audio Options order.
std::vector<LivePlayer*> live_players();
