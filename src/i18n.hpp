// The mod's text in the game's language: the Mods page rows, the Audio Options X button, the "All music" playlist,
// the radio display's "Waiting for ..." and FH2 ANNA's menu. As FH2ANNA does it (see i18n.cpp).
#pragma once

#include <cstdint>
#include <string>

enum class Text {
    ModDescription,   // MODS > Installed mods
    EnabledDesc,      // the "Xbox Music Radio" row
    TagsLabel,
    TagsDesc,
    ShuffleLabel,
    ShuffleDesc,
    NextHotkey,       // the "next song" hotkey row
    RefreshButton,    // the Audio Options X button's hint
    AllMusic,         // the playlist with every song
    WaitingFor,       // the radio display: "Waiting for {0}" (plain ASCII only: the display can't show more)
    NextTrack,        // FH2 ANNA's menu (anna.cpp)
    PreviousTrack,
    Playlist,
    DefaultPlaylist,  // the game's own name for its default playlist (MyProfile.str IDS_DefaultPlaylist_Name)
    Count
};

// From fh2mod_init, before any text: the language seen last (kept in `ini`), else Azimuth's for the game. Returns it,
// for the log.
std::string i18n_init(const std::wstring& ini, const std::wstring& azimuth_dir, const std::wstring& game_dir);
// Once the game's code can be read (wait_for_code): watches for the game's text language, keeps it for next time,
// and from then on tr() follows it (the rows written at startup follow from the next start).
void i18n_watch(uintptr_t base, void (*log)(const char*));

const char* tr(Text t);       // UTF-8, for the platform's rows
std::wstring trw(Text t);     // UTF-16, for game text
// `t` in the language with the game's code `code` ("DE", "CHT"...), whatever the game is in now.
const char* tr_in(const char* code, Text t);
