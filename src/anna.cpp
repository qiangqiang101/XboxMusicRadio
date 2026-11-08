// FH2 ANNA's menu (include/fh2anna.h, interface version 4):
//
//   Xbox Music Radio
//   ├─ Next Track          (ANNA stays open: press again to skip more)
//   ├─ Previous Track
//   └─ Playlist
//        ├─ Horizon 2 Mix  (the game's default playlist: all music)
//        ├─ Spotify, YouTube Music, NetEase Cloud Music, QQ Music (those Audio Options lists)
//        └─ the music folder's playlists (All music, then each folder)
//
// The same list as Audio Options, in its order. While the radio isn't on the Xbox Music station, Next and Previous are
// greyed out and the playlists hidden, so the whole list is greyed out. Only those two can be greyed out: ANNA puts two
// triggers in its row template for each label that can, and every row checks them all each time a list is shown, so
// one per playlist slot made switching lists drop frames. ANNA calls everything here on the game's thread.
#include "anna.hpp"

#include "fh2anna.h"
#include "i18n.hpp"
#include "library.hpp"
#include "live.hpp"
#include "radio.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

const FH2ModApi* g_api = nullptr;
FH2Mod* g_mod          = nullptr;

constexpr int kFolderSlots = 64;   // the music folder's playlists ANNA can show (added up front: entries stay)

void logf(const char* text) { g_api->log(g_mod, text); }

std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

int on_air(void*, const char*) { return radio_on_air() ? 1 : 0; }

// ---- Next and previous ----

void choose_next(void*, const char*) { radio_next(); }
void choose_previous(void*, const char*) { radio_previous(); }
const char* next_label(void*, const char*) { return tr(Text::NextTrack); }
const char* previous_label(void*, const char*) { return tr(Text::PreviousTrack); }
const char* playlist_label(void*, const char*) { return tr(Text::Playlist); }

// ---- The playlists ----

void choose_default(void*, const char*) { radio_select_playlist(L""); }   // what Audio Options sends for it
const char* default_label(void*, const char*) { return tr(Text::DefaultPlaylist); }

// What the Playlist list needs to know, worked out on a thread of its own once a second: ANNA asks every entry each
// time a list opens (and every second for its voice commands), on the game's thread, and a music app's answer looks
// through the running programs, the music folder's comes from under the library's lock. Those would hold up a frame;
// the entries only read what's here.
struct Folder {
    std::wstring id;
    std::string name;
};
struct State {
    std::vector<char> live_listed;   // by g_live index: Audio Options offers it
    std::vector<Folder> folders;     // the library's newest scan
};
std::vector<std::pair<LivePlayer*, std::string>> g_live;   // made before the entries, then only read
std::mutex g_state_mx;
std::shared_ptr<const State> g_state;   // swapped whole, under g_state_mx
std::shared_ptr<const State> g_held;    // the game's thread: keeps the text a label points into until ANNA copies it

const State& state() {
    std::lock_guard<std::mutex> lk(g_state_mx);
    g_held = g_state;
    return *g_held;
}

void refresh(uint32_t& scan) {
    auto next = std::make_shared<State>();
    for (const auto& l : g_live) next->live_listed.push_back(l.first->listed() ? 1 : 0);
    uint32_t done = library_done();
    if (done != scan || !g_state) {
        scan = done;
        for (const Playlist& p : library_playlists()) next->folders.push_back({p.id, utf8(p.name)});
        static bool told = false;
        if (!told && next->folders.size() > kFolderSlots) {
            told = true;
            char line[160];
            std::snprintf(line, sizeof line, "FH2 ANNA's menu shows the first %d of the music folder's %zu playlists (Audio Options has them all)",
                          kFolderSlots, next->folders.size());
            logf(line);
        }
    } else {
        next->folders = g_state->folders;   // only this thread swaps g_state
    }
    std::lock_guard<std::mutex> lk(g_state_mx);
    g_state = std::move(next);
}

int live_shown(void* user, const char*) {
    if (!radio_on_air()) return 0;
    const State& s = state();
    size_t i       = reinterpret_cast<uintptr_t>(user);
    return i < s.live_listed.size() && s.live_listed[i] ? 1 : 0;
}
void choose_live(void* user, const char*) { radio_select_playlist(g_live[reinterpret_cast<uintptr_t>(user)].first->id()); }

int folder_shown(void* user, const char*) {
    return radio_on_air() && reinterpret_cast<uintptr_t>(user) < state().folders.size() ? 1 : 0;
}
const char* folder_label(void* user, const char*) {
    const auto& f = state().folders;
    size_t i      = reinterpret_cast<uintptr_t>(user);
    return i < f.size() ? f[i].name.c_str() : "";
}
void choose_folder(void* user, const char*) {
    const auto& f = state().folders;
    size_t i      = reinterpret_cast<uintptr_t>(user);
    if (i < f.size()) radio_select_playlist(f[i].id);
}

// ---- Adding them ----

DWORD WINAPI add_entries(LPVOID) {
    g_api->wait_for_mods(30000);
    auto* anna = static_cast<const FH2AnnaApi*>(g_api->get_interface(FH2ANNA_INTERFACE, 4));
    if (!anna) {
        logf("FH2 ANNA (1.0 or later) isn't installed: no Xbox Music Radio in its menu");
        return 0;
    }
    int failed = 0;
    // Labels in the language seen last (the game hasn't one yet: a greyed-out entry shows this one), and label_now
    // in the game's, each time.
    auto add = [&](const char* key, const char* parent, const char* label, FH2AnnaChooseFn choose, FH2AnnaLabelFn now = nullptr,
                   FH2AnnaShownFn shown = nullptr, void* user = nullptr, int stays_open = 0) {
        FH2AnnaEntry e{};
        e.size       = sizeof e;
        e.key        = key;
        e.parent     = parent;
        e.label      = label;
        e.on_choose  = choose;
        e.label_now  = now;
        e.shown      = shown;
        e.user       = user;
        e.stays_open = stays_open;
        e.enabled    = stays_open ? on_air : nullptr;   // Next and Previous only (see the top); a list greys out with them
        if (!anna->add_entry(&e)) failed++;
    };
    add("xboxmusicradio", nullptr, "Xbox Music Radio", nullptr);
    add("xboxmusicradio.next", "xboxmusicradio", tr(Text::NextTrack), choose_next, next_label, nullptr, nullptr, 1);
    add("xboxmusicradio.previous", "xboxmusicradio", tr(Text::PreviousTrack), choose_previous, previous_label, nullptr, nullptr, 1);
    add("xboxmusicradio.playlist", "xboxmusicradio", tr(Text::Playlist), nullptr, playlist_label);
    add("xboxmusicradio.playlist.default", "xboxmusicradio.playlist", tr(Text::DefaultPlaylist), choose_default, default_label, on_air);
    for (LivePlayer* p : live_players()) g_live.push_back({p, utf8(p->name())});
    uint32_t scan = 0;
    refresh(scan);
    for (size_t i = 0; i < g_live.size(); ++i) {
        std::string key = "xboxmusicradio.playlist.live" + std::to_string(i);
        add(key.c_str(), "xboxmusicradio.playlist", g_live[i].second.c_str(), choose_live, nullptr, live_shown, reinterpret_cast<void*>(i));
    }
    for (int i = 0; i < kFolderSlots; ++i) {
        std::string key   = "xboxmusicradio.playlist.folder" + std::to_string(i);
        std::string label = tr(Text::Playlist) + std::string(" ") + std::to_string(i + 1);   // only until the list opens
        add(key.c_str(), "xboxmusicradio.playlist", label.c_str(), choose_folder, folder_label, folder_shown,
            reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
    logf(failed ? "couldn't add all of Xbox Music Radio to FH2 ANNA's menu" : "Xbox Music Radio is in FH2 ANNA's menu");
    for (;;) {
        Sleep(1000);
        refresh(scan);
    }
}

} // namespace

void anna_start(const FH2ModApi* api, FH2Mod* mod) {
    g_api = api;
    g_mod = mod;
    if (api->api_version < 2) return;   // get_interface
    if (HANDLE t = CreateThread(nullptr, 0, add_entries, nullptr, 0, nullptr)) CloseHandle(t);
}
