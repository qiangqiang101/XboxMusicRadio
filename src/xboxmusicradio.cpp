// XboxMusicRadio: the radio's "Xbox Music" station, playing your own music files.
//
// Copyright (C) 2026 Bartholomew Ho. GPL-3.0-or-later (see LICENSE). The station
// itself (radio.cpp, decoder.c) is ported from FH2_SpotifyMin, which builds on
// Bullet4fun's spotify-radio mod.
//
// Two halves. radio.cpp unlocks the station and plays the music folder on it.
// This file brings back the game's Xbox Music service as far as the menus need
// it: on Xbox One the game asked music.xboxlive.com (through its web-service
// layer) for the user's subscription, playlists, tracks and stream URLs. The
// mod answers those six calls itself from the music folder (each subfolder a
// playlist), so Audio Options lists your playlists, and the one you choose
// there is the one the station plays. Each call gets a task handle of ours
// that reports "done" once the library scan has the answer; the game's own
// task poller then calls the usual success callbacks.
// docs/re-notes.md has the addresses and layouts.
#include "fh2mod.h"
#include "library.hpp"
#include "radio.hpp"
#include "spotify.hpp"
#include "apps.hpp"
#include "anna.hpp"
#include "i18n.hpp"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>

namespace {

const FH2ModApi* g_api = nullptr;
FH2Mod* g_mod          = nullptr;
uintptr_t g_base       = 0;
bool g_enabled         = true;
bool g_trace           = false;
std::wstring g_ini;
RadioOptions g_radio;

constexpr uintptr_t kImageBase = 0x140000000;

// Game addresses (VAs at the preferred base, forza_durango_release_final.exe of 24 Nov 2015).
constexpr uintptr_t kGetUserPermissions        = 0x141990D30;   // (result*, task**)
constexpr uintptr_t kGetPlaylists              = 0x1419906D0;   // (result*, task**)
constexpr uintptr_t kContinueGetPlaylists      = 0x141990030;   // (token, result*, task**)
constexpr uintptr_t kGetPlaylistTracks         = 0x141990380;   // (playlistId, result*, task**)
constexpr uintptr_t kContinueGetPlaylistTracks = 0x14198FCD0;   // (playlistId, token, result*, task**)
constexpr uintptr_t kGetSongUrl                = 0x1419909D0;   // (songId, clientInstanceId, result*, task**)
constexpr uintptr_t kSongStreamUpdate          = 0x140FE5230;   // XboxMusic::SongStream state machine
constexpr uintptr_t kXboxMusicAvailable        = 0x140FDDF90;   // bool (manager*)
constexpr uintptr_t kTaskFail                  = 0x140F05000;   // (tracker*, hr): the owner's failure callback
constexpr uintptr_t kManager                   = 0x14365AE08;   // the Xbox Music manager (pointer)
constexpr uintptr_t kSetPlaylist               = 0x140FDE220;   // (manager*, std::wstring* id): what Audio Options calls
constexpr uintptr_t kAuthUpdate                = 0x140FE5090;   // (auth*): state 0 sends GetUserPermissions
constexpr uintptr_t kSignedIn                  = 0x140FE3FA0;   // bool (auth*): state > 1, subscribed, region ok
constexpr uintptr_t kRefreshPlaylists          = 0x140FDE1F0;   // (manager*): PlaylistBrowser refresh (GetPlaylists again)
constexpr uintptr_t kLaunchMusicApp            = 0x140FDE000;   // (): opens ms-xbl-18ffc9f4:// (the Audio Options X button)
constexpr uintptr_t kTaskClose                 = 0x140EDB550;   // (handle): Close + Release a task handle

// Manager fields.
constexpr size_t kMgrPlaylist   = 0x6660;   // XboxMusic::Playlist*: the chosen playlist
constexpr size_t kPlaylistId    = 0x98;     // its id, a std::wstring
constexpr size_t kMgrServerFlag = 0x6790;   // enableXboxMusic from GetEconomySettings (or the save)
constexpr size_t kMgrAuth       = 0x6670;   // XboxMusicAuthentication
constexpr size_t kAuthState     = 0x118;    // 0 ask, 1 asking, 3 waiting for the answer (or to retry), 2 signed in
constexpr size_t kAuthRegion    = 0x79;     // IsSubscriptionAvailableForPurchase
constexpr size_t kAuthTracker   = 0x88;     // its task tracker (handle at +0x10, start time +0x70, elapsed +0x88)
constexpr size_t kAuthTask      = kAuthTracker + 0x10;   // the GetUserPermissions task handle

// Reply layouts (from the game's reflection tables).
constexpr size_t kPermSize = 2;   // HasSubscription, IsSubscriptionAvailableForPurchase

constexpr size_t kPlSize = 0x659C, kPlItems = 4, kPlItemSize = 0x3FC, kPlMax = 25;
constexpr size_t kPlItemTrackCount = 0, kPlItemId = 4, kPlItemName = 0x1FA;
constexpr size_t kPlTotal = 0x63A0, kPlTokenFlag = 0x63A4, kPlToken = 0x63A6;

constexpr size_t kTrSize = 0x297C0, kTrCollections = 0, kTrTotal = 4, kTrTracksFlag = 8, kTrCount = 0xC;
constexpr size_t kTrItems = 0x10, kTrItemSize = 0x1A78, kTrMax = 25, kTrTokenFlag = 0x295C8, kTrToken = 0x295CA;
constexpr size_t kSongId = 0, kSongName = 0x1F6, kSongArtistCount = 0x5F8, kSongArtists = 0x5FC, kSongTrackNumber = 0x1A74;
constexpr size_t kArtistName = 0, kArtistRole = 0x402;   // in the first of 5 ArtistItems (0x418 bytes each)

constexpr size_t kUrlSize = 0x103E, kUrlUrl = 0, kUrlContentType = 0x1002;

// Field sizes in characters, terminator included.
constexpr size_t kIdChars = 0xFB, kPlNameChars = 0x101, kSongNameChars = 0x201, kRoleChars = 0xB;
constexpr size_t kUrlChars = 0x801, kContentTypeChars = 0x1E;

const wchar_t kSongPrefix[] = L"xmr-song:";

void logf(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    g_api->log(g_mod, line);
}

void tracef(const char* fmt, ...) {
    if (!g_trace) return;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    g_api->log(g_mod, line);
}

std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

template <typename T>
T game(uintptr_t va) {
    return reinterpret_cast<T>(g_base + (va - kImageBase));
}

// Writes `s` into a fixed WCHAR[chars] field (fields can sit at odd offsets: memcpy, not wchar_t*).
void put(uint8_t* dst, size_t chars, const std::wstring& s) {
    size_t n = std::min(s.size(), chars - 1);
    std::memcpy(dst, s.data(), n * sizeof(wchar_t));
    std::memset(dst + n * sizeof(wchar_t), 0, sizeof(wchar_t));
}

void put32(uint8_t* dst, uint32_t v) { std::memcpy(dst, &v, sizeof v); }

uint32_t token_offset(const std::wstring& token) {
    size_t i = 0;
    while (i < token.size() && !iswdigit(token[i])) i++;   // playlist tokens start with "p"
    return static_cast<uint32_t>(std::wcstoul(token.c_str() + i, nullptr, 10));
}

const wchar_t* content_type(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    std::wstring ext = dot == std::wstring::npos ? L"" : path.substr(dot);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext == L".mp3") return L"audio/mpeg";
    if (ext == L".m4a" || ext == L".aac") return L"audio/mp4";
    if (ext == L".wma") return L"audio/x-ms-wma";
    if (ext == L".wav") return L"audio/wav";
    if (ext == L".flac") return L"audio/flac";
    return L"audio/mpeg";
}

// ---- One answer to one call ---------------------------------------------------------------------------

enum class Kind { Permissions, Playlists, Tracks, SongUrl };

struct Request {
    Kind kind;
    uint8_t* result;        // the caller's reply buffer, alive until its task completes or closes
    std::wstring id;        // playlist or song id
    uint32_t offset = 0;    // first item of this page
    uint32_t needs_gen = 1; // library scan generation the answer needs
};

const char* kind_name(Kind k) {
    switch (k) {
    case Kind::Permissions: return "GetUserPermissions";
    case Kind::Playlists: return "GetPlaylists";
    case Kind::Tracks: return "GetPlaylistTracks";
    case Kind::SongUrl: return "GetSongURL";
    }
    return "?";
}

HRESULT fill_permissions(const Request& r) {
    std::memset(r.result, 0, kPermSize);
    r.result[0] = 1;   // HasSubscription
    r.result[1] = 1;   // IsSubscriptionAvailableForPurchase (the service works in this region)
    return S_OK;
}

HRESULT fill_playlists(const Request& r) {
    std::vector<Playlist> lists = library_playlists();
    std::vector<Playlist> live;   // first in the list; one "song" each so the game doesn't skip them as empty
    for (LivePlayer* p : live_players())
        if (p->listed()) live.push_back({p->id(), p->name(), {0}});
    lists.insert(lists.begin(), live.begin(), live.end());
    uint8_t* b = r.result;
    std::memset(b, 0, kPlSize);
    uint32_t total = static_cast<uint32_t>(lists.size());
    uint32_t first = std::min(r.offset, total);
    uint32_t n     = std::min<uint32_t>(total - first, kPlMax);
    put32(b, n);
    for (uint32_t i = 0; i < n; i++) {
        const Playlist& p = lists[first + i];
        uint8_t* item     = b + kPlItems + i * kPlItemSize;
        put32(item + kPlItemTrackCount, static_cast<uint32_t>(p.songs.size()));
        put(item + kPlItemId, kIdChars, p.id);
        put(item + kPlItemName, kPlNameChars, p.name);
    }
    put32(b + kPlTotal, total);
    if (first + n < total) {
        b[kPlTokenFlag] = 1;
        put(b + kPlToken, kIdChars, L"p" + std::to_wstring(first + n));
    }
    tracef("GetPlaylists from %u: %u of %u playlist(s)", first, n, total);
    return S_OK;
}

HRESULT fill_tracks(const Request& r) {
    std::vector<LivePlayer*> live = live_players();
    if (std::any_of(live.begin(), live.end(), [&](LivePlayer* p) { return r.id == p->id(); })) {   // what plays is up to the player
        std::memset(r.result, 0, kTrSize);
        put32(r.result + kTrCollections, 1);
        return S_OK;
    }
    Playlist p;
    if (!library_playlist(r.id, p)) {
        logf("GetPlaylistTracks: no playlist \"%s\" (folder renamed or removed?)", narrow(r.id).c_str());
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    uint8_t* b = r.result;
    std::memset(b, 0, kTrSize);
    uint32_t total = static_cast<uint32_t>(p.songs.size());
    uint32_t first = std::min(r.offset, total);
    uint32_t n     = std::min<uint32_t>(total - first, kTrMax);
    put32(b + kTrCollections, 1);
    put32(b + kTrTotal, total);
    b[kTrTracksFlag] = 1;
    put32(b + kTrCount, n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t index = p.songs[first + i];
        Song s;
        library_song(index, s);
        uint8_t* item = b + kTrItems + i * kTrItemSize;
        put(item + kSongId, kIdChars, kSongPrefix + std::to_wstring(index));
        put(item + kSongName, kSongNameChars, s.title);
        if (!s.artist.empty()) {
            put32(item + kSongArtistCount, 1);
            put(item + kSongArtists + kArtistName, kSongNameChars, s.artist);
            put(item + kSongArtists + kArtistRole, kRoleChars, L"Main");
        }
        put32(item + kSongTrackNumber, first + i + 1);
    }
    if (first + n < total) {
        b[kTrTokenFlag] = 1;
        put(b + kTrToken, kIdChars, std::to_wstring(first + n));
    }
    tracef("GetPlaylistTracks \"%s\" from %u: %u of %u song(s)", narrow(p.name).c_str(), first, n, total);
    return S_OK;
}

HRESULT fill_song_url(const Request& r) {
    Song s;
    size_t plen = std::wcslen(kSongPrefix);
    if (r.id.compare(0, plen, kSongPrefix) != 0 ||
        !library_song(static_cast<uint32_t>(std::wcstoul(r.id.c_str() + plen, nullptr, 10)), s)) {
        logf("GetSongURL: unknown song id \"%s\"", narrow(r.id).c_str());
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    if (s.path.size() >= kUrlChars) {
        logf("GetSongURL: path too long for the game (%zu characters): %s", s.path.size(), narrow(s.path).c_str());
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }
    if (GetFileAttributesW(s.path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        logf("GetSongURL: file is gone: %s", narrow(s.path).c_str());
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    std::memset(r.result, 0, kUrlSize);
    put(r.result + kUrlUrl, kUrlChars, s.path);
    put(r.result + kUrlContentType, kContentTypeChars, content_type(s.path));
    logf("playing %s - %s (%s)", narrow(s.artist).c_str(), narrow(s.title).c_str(), narrow(s.path).c_str());
    return S_OK;
}

HRESULT fill(const Request& r) {
    switch (r.kind) {
    case Kind::Permissions: return fill_permissions(r);
    case Kind::Playlists: return fill_playlists(r);
    case Kind::Tracks: return fill_tracks(r);
    case Kind::SongUrl: return fill_song_url(r);
    }
    return E_UNEXPECTED;
}

// ---- Our task handle ------------------------------------------------------------------------------------
// The game's task handles are objects with a vtable: [0] AddRef(tag), [1] Release(tag), [2] status,
// [4] Close(tag). The game's poller (game+0xF05250) reads the status every frame: 2 and 3 mean "still
// running", anything else is the finished call's HRESULT, and a success then calls the owner's
// success callback, which reads the reply buffer. So the reply is written right before we first
// report success, on the poller's thread, and never after the owner closed the task.

std::mutex g_task_mx;

struct FakeTask {
    const void* const* vtbl;
    volatile LONG refs;
    bool done;
    bool closed;
    HRESULT hr;
    Request req;
};

int64_t task_add_ref(FakeTask* t, const char*) { return InterlockedIncrement(&t->refs); }

int64_t task_release(FakeTask* t, const char*) {
    LONG n = InterlockedDecrement(&t->refs);
    if (n == 0) delete t;
    return n;
}

int32_t task_status(FakeTask* t) {
    std::lock_guard<std::mutex> lk(g_task_mx);
    if (t->done || t->closed) return t->hr;
    if (library_done() < t->req.needs_gen) return 2;   // still scanning the music folder
    t->hr   = fill(t->req);
    t->done = true;
    return t->hr;
}

int64_t task_close(FakeTask* t, const char*) {
    std::lock_guard<std::mutex> lk(g_task_mx);
    t->closed = true;
    if (!t->done) t->hr = E_ABORT;
    return 0;
}

int64_t task_other(FakeTask*) { return 0; }

const void* const kTaskVtbl[] = {
    reinterpret_cast<const void*>(&task_add_ref), reinterpret_cast<const void*>(&task_release),
    reinterpret_cast<const void*>(&task_status),  reinterpret_cast<const void*>(&task_other),
    reinterpret_cast<const void*>(&task_close),   reinterpret_cast<const void*>(&task_other),
    reinterpret_cast<const void*>(&task_other),   reinterpret_cast<const void*>(&task_other),
};

HRESULT start(Request req, void** task) {
    tracef("%s %s (from %u, needs scan %u)", kind_name(req.kind), narrow(req.id).c_str(), req.offset, req.needs_gen);
    if (!task) return E_POINTER;
    auto* t   = new FakeTask{kTaskVtbl, 1, false, false, S_OK, std::move(req)};
    *task     = t;
    return S_OK;
}

// ---- The six calls ---------------------------------------------------------------------------------------

HRESULT get_user_permissions(uint8_t* result, void** task) { return start({Kind::Permissions, result, L"", 0, 0}, task); }

HRESULT get_playlists(uint8_t* result, void** task) {
    // A fresh scan each time the list is opened, so new folders show without a restart.
    return start({Kind::Playlists, result, L"", 0, library_rescan()}, task);
}

HRESULT continue_get_playlists(const wchar_t* token, uint8_t* result, void** task) {
    // The token sits in the reply buffer we'll overwrite: read it now.
    return start({Kind::Playlists, result, L"", token_offset(token ? token : L""), 1}, task);
}

HRESULT get_playlist_tracks(const wchar_t* id, uint8_t* result, void** task) {
    return start({Kind::Tracks, result, id ? id : L"", 0, 1}, task);
}

HRESULT continue_get_playlist_tracks(const wchar_t* id, const wchar_t* token, uint8_t* result, void** task) {
    return start({Kind::Tracks, result, id ? id : L"", token_offset(token ? token : L""), 1}, task);
}

HRESULT get_song_url(const wchar_t* id, const wchar_t*, uint8_t* result, void** task) {
    return start({Kind::SongUrl, result, id ? id : L"", 0, 1}, task);
}

// ---- SongStream::Update ------------------------------------------------------------------------------------
// The game's version builds the clientInstanceId for GetSongURL from
// Windows.Networking.Connectivity.NetworkInformation and throws if any step of that fails, which
// nothing under Azimuth promises to support. We don't need the id, so this is the same state machine
// without it: state 4 waits out the retry timer, state 0 asks for the URL and moves to 1.

const wchar_t* msvc_wstring(uint8_t* s) {
    size_t cap = *reinterpret_cast<size_t*>(s + 0x18);
    return cap >= 8 ? *reinterpret_cast<wchar_t**>(s) : reinterpret_cast<wchar_t*>(s);
}

uint32_t song_stream_update(uint8_t* s) {
    auto& state = *reinterpret_cast<int32_t*>(s + 0x118);
    if (state == 4) {
        // The retry timer at +8: true once the last task is gone and 10 s have passed.
        auto timer   = s + 8;
        auto expired = reinterpret_cast<bool (*)(void*, void*)>((*reinterpret_cast<void***>(timer))[0]);
        if (expired(timer, s + 0x120)) state = 0;
    }
    if (!*game<void**>(kManager) || state != 0) return 8;
    HRESULT hr = get_song_url(msvc_wstring(s + 0x78), L"", s + 0x1B0, reinterpret_cast<void**>(s + 0x130));
    if (FAILED(hr)) {
        game<void (*)(void*, HRESULT)>(kTaskFail)(s + 0x120, hr);
        return 8;
    }
    state = 1;
    return 0;
}

// ---- Availability -------------------------------------------------------------------------------------------
// The game only offers Xbox Music when its sign-in succeeded and the server's enableXboxMusic flag
// is set. The flag comes from GetEconomySettings, which nothing answers without a community server,
// so set it here; the sign-in is ours already (GetUserPermissions).

bool (*g_available_orig)(uint8_t*) = nullptr;
volatile LONG g_last_available     = -1;

// An MSVC std::wstring: the text in place up to 7 characters, else on the heap.
struct MsvcWString {
    union {
        wchar_t buf[8];
        wchar_t* ptr;
    };
    size_t size;
    size_t cap;
};

// set_playlist(manager, id): the game's choice, from the saved setting at startup and from Audio Options.
// The radio plays that playlist; the game's own player never starts (the station's play command is ours).
void (*g_set_playlist_orig)(uint8_t*, MsvcWString*) = nullptr;
std::atomic<bool> g_playlist_seen{false};   // set_playlist has told us the choice
void set_playlist_detour(uint8_t* mgr, MsvcWString* id) {
    if (id) {
        radio_select_playlist(std::wstring(id->cap >= 8 ? id->ptr : id->buf, id->size));   // it frees the string
        g_playlist_seen = true;
    }
    g_set_playlist_orig(mgr, id);
}

// ---- Catching up with what the game did before the hooks were on ----------------------------------------------
// The game reads its Xbox Music settings while it loads the profile, and that may be before our hooks are on (the
// game runs while every mod makes its hooks). It then sends the real GetUserPermissions, which nothing answers any
// more: the sign-in stays at state 3 for good, so Audio Options greys out its Xbox Music row; and the saved playlist
// goes to set_playlist unseen, so the station plays all music instead of the choice (Spotify, say).
// Both are put right from here, on the game's thread (the game calls XboxMusicAvailable from its UI and update).

// Drops the game's own unanswered GetUserPermissions and asks again (answered by us). The handle is closed the way
// the game closes a finished task (game+0xF05050), but without its failure callback: that would raise the
// "couldn't sign in" event.
bool retry_sign_in(uint8_t* mgr) {
    uint8_t* auth = mgr + kMgrAuth;
    auto& state   = *reinterpret_cast<int32_t*>(auth + kAuthState);
    auto& task    = *reinterpret_cast<void**>(auth + kAuthTask);
    if ((state != 1 && state != 3) || !task || *reinterpret_cast<const void* const* const*>(task) == kTaskVtbl) return false;
    void* stuck = task;
    task        = nullptr;
    game<HRESULT (*)(void*)>(kTaskClose)(stuck);
    *reinterpret_cast<uint64_t*>(auth + kAuthTracker + 0x70) = 0;   // its start time and elapsed time, as the game
    *reinterpret_cast<uint64_t*>(auth + kAuthTracker + 0x88) = 0;   // leaves them when a task finishes
    state = 0;
    game<void (*)(uint8_t*)>(kAuthUpdate)(auth);   // state 0: GetUserPermissions, ours now
    logf("Xbox Music sign-in: the game asked the old service before the hooks were on; asked again");
    return true;
}

// The playlist the game chose (Playlist at manager+0x6660, its id at +0x98), when set_playlist went by unseen.
void catch_up_playlist(uint8_t* mgr) {
    if (g_playlist_seen) return;
    uint8_t* pl = *reinterpret_cast<uint8_t**>(mgr + kMgrPlaylist);
    if (!pl) return;   // not chosen yet: set_playlist will tell us
    uint8_t* s  = pl + kPlaylistId;
    size_t size = *reinterpret_cast<size_t*>(s + 0x10), cap = *reinterpret_cast<size_t*>(s + 0x18);
    if (size > cap || size > kIdChars) return;
    g_playlist_seen = true;
    std::wstring id(msvc_wstring(s), size);
    logf("the game chose its playlist before the hooks were on: %s", id.empty() ? "(none)" : narrow(id).c_str());
    radio_select_playlist(id);
}

std::atomic<bool> g_hooks_on{false};
bool g_caught_up = false;   // the game's thread only

bool available_detour(uint8_t* mgr) {
    if (mgr && g_hooks_on && !g_caught_up) {
        g_caught_up = true;
        catch_up_playlist(mgr);
        retry_sign_in(mgr);
    }
    if (mgr) mgr[kMgrServerFlag] = 1;
    bool r = g_available_orig(mgr);
    if (InterlockedExchange(&g_last_available, r ? 1 : 0) != (r ? 1 : 0)) {
        uint8_t* auth = mgr ? mgr + kMgrAuth : nullptr;
        logf("Xbox Music %s (sign-in state %d, region ok %d)", r ? "available" : "not available",
             auth ? *reinterpret_cast<int32_t*>(auth + kAuthState) : -1, auth ? auth[kAuthRegion] : -1);
    }
    return r;
}

// ---- The Audio Options X button ------------------------------------------------------------------------------
// "Open the Xbox Music app" (game+0xFDE000, Audio Options and the Xbox Music intro) launches ms-xbl-18ffc9f4://,
// which goes nowhere on PC. Instead: unstick the sign-in if it's stuck, scan the music folder again and refresh the
// playlist list (the game's own refresh; our GetPlaylists rescans), and if Spotify is the choice, move Spotify
// playback here.
void launch_music_app_detour() {
    uint8_t* mgr = *game<uint8_t**>(kManager);
    logf("X button (Xbox Music app): refreshing");
    if (mgr) {
        catch_up_playlist(mgr);
        retry_sign_in(mgr);
    }
    if (mgr && game<bool (*)(uint8_t*)>(kSignedIn)(mgr + kMgrAuth)) {
        game<void (*)(uint8_t*)>(kRefreshPlaylists)(mgr);
        logf("X button: scanning the music folder and refreshing the playlist list");
    } else {
        library_rescan();   // the list refreshes by itself once signed in (Audio Options asks then)
        logf("X button: scanning the music folder (the playlist list follows once Xbox Music is signed in)");
    }
    if (radio_selected_playlist() == kSpotifyId) {
        if (!spotify_running()) logf("X button: Spotify is chosen but Soloist isn't running");
        else if (spotify_take_over()) logf("X button: Spotify plays on the radio");
    }
}

void* g_unused_orig = nullptr;   // the replaced functions' originals are never called

struct HookSpec {
    const char* name;
    uintptr_t va;
    void* detour;
    void** original;
};

bool install_hooks() {
    if (!radio_install_gate(g_api, g_mod, g_base, g_radio)) return false;
    // The game reads its Xbox Music settings (the saved playlist, then the sign-in) while it loads the profile, a
    // second or two from now: these go on one by one, right away, not with the rest (available_detour catches up
    // on anything that still went by first).
    const HookSpec early[] = {
        {"XboxMusic GetUserPermissions (answered locally)", kGetUserPermissions, reinterpret_cast<void*>(&get_user_permissions), &g_unused_orig},
        {"Xbox Music set playlist (the radio follows it)", kSetPlaylist, reinterpret_cast<void*>(&set_playlist_detour),
         reinterpret_cast<void**>(&g_set_playlist_orig)},
        {"Xbox Music available (sets the server flag)", kXboxMusicAvailable, reinterpret_cast<void*>(&available_detour),
         reinterpret_cast<void**>(&g_available_orig)},
    };
    for (const auto& h : early) {
        FH2Hook* hook = g_api->hook_function(g_mod, h.name, game<void*>(h.va), h.detour, h.original);
        if (!hook || !g_api->enable_hooks(g_mod, hook)) {
            logf("couldn't hook %s at game+0x%llX", h.name, static_cast<unsigned long long>(h.va - kImageBase));
            g_api->disable_hooks(g_mod, nullptr);
            return false;
        }
    }
    g_hooks_on = true;   // available_detour may catch up now: GetUserPermissions and set_playlist are ours
    const HookSpec hooks[] = {
        {"XboxMusic GetPlaylists (music folder)", kGetPlaylists, reinterpret_cast<void*>(&get_playlists), &g_unused_orig},
        {"XboxMusic ContinueGetPlaylists (music folder)", kContinueGetPlaylists, reinterpret_cast<void*>(&continue_get_playlists), &g_unused_orig},
        {"XboxMusic GetPlaylistTracks (music folder)", kGetPlaylistTracks, reinterpret_cast<void*>(&get_playlist_tracks), &g_unused_orig},
        {"XboxMusic ContinueGetPlaylistTracks (music folder)", kContinueGetPlaylistTracks,
         reinterpret_cast<void*>(&continue_get_playlist_tracks), &g_unused_orig},
        {"XboxMusic GetSongURL (local file)", kGetSongUrl, reinterpret_cast<void*>(&get_song_url), &g_unused_orig},
        {"XboxMusic SongStream update (no network id)", kSongStreamUpdate, reinterpret_cast<void*>(&song_stream_update), &g_unused_orig},
    };
    bool ok = true;
    for (const auto& h : hooks) {
        if (!g_api->hook_function(g_mod, h.name, game<void*>(h.va), h.detour, h.original)) {
            logf("couldn't hook %s at game+0x%llX", h.name, static_cast<unsigned long long>(h.va - kImageBase));
            ok = false;
        }
    }
    ok = ok && radio_install();
    if (!ok || !g_api->enable_hooks(g_mod, nullptr)) {
        g_api->disable_hooks(g_mod, nullptr);   // all or nothing: half of these would leave the station broken
        return false;
    }
    // Not needed for the station: without it the X button just does what it did.
    FH2Hook* x = g_api->hook_function(g_mod, "Xbox Music app button (refreshes instead)", game<void*>(kLaunchMusicApp),
                                      reinterpret_cast<void*>(&launch_music_app_detour), &g_unused_orig);
    if (!x || !g_api->enable_hooks(g_mod, x)) logf("couldn't hook the Xbox Music app button: it won't refresh");
    return true;
}

// ---- Settings ----------------------------------------------------------------------------------------------------

std::wstring ini_string(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(section, key, def, buf, 2048, g_ini.c_str());
    wchar_t expanded[2048];
    DWORD n = ExpandEnvironmentStringsW(buf, expanded, 2048);   // %USERPROFILE%\Music
    return n > 0 && n <= 2048 ? std::wstring(expanded) : std::wstring(buf);
}

// The signed-in user's Music folder, wherever it is (moved to another drive, or into OneDrive).
std::wstring windows_music_folder() {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &path)) && path) out = path;
    CoTaskMemFree(path);
    if (out.empty()) {
        wchar_t buf[MAX_PATH];
        DWORD n = ExpandEnvironmentStringsW(L"%USERPROFILE%\\Music", buf, MAX_PATH);
        if (n > 0 && n <= MAX_PATH) out = buf;
    }
    return out;
}

void write_default_ini() {
    if (GetFileAttributesW(g_ini.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    const char text[] =
        "; XboxMusicRadio settings. Changes apply the next time the game starts.\r\n"
        "[radio]\r\n"
        "; 0 turns the mod off (the game's Xbox Music stays unavailable, as without it)\r\n"
        "enabled=1\r\n"
        "\r\n"
        "[music]\r\n"
        "; The folder with your music. Each folder in it is a playlist (with everything in its own\r\n"
        "; subfolders); files right in it make a playlist named after it.\r\n"
        "; Empty: your Windows Music folder. Environment variables work: %USERPROFILE%\\Desktop\\Songs\r\n"
        "folder=\r\n"
        "; 1: song titles and artists from the files' tags; 0: from the file names (\"Artist - Title.mp3\")\r\n"
        "read_tags=1\r\n"
        "; 1: songs in a random order; 0: in folder order\r\n"
        "shuffle=1\r\n"
        "; Next song: a Windows virtual-key code (117 = F6; 0 = none) and a controller chord (XInput bits; 0 = none)\r\n"
        "next_key=117\r\n"
        "next_pad=0\r\n"
        "\r\n"
        "[spotify]\r\n"
        "; Spotify Soloist (Spotify Premium): with soloist.exe in this folder and your Soloist API key below,\r\n"
        "; \"Spotify\" is a playlist in Audio Options. Choose it, then in the Spotify app play on the device below.\r\n"
        "; API key: developer.spotify.com/dashboard > Spotify Soloist API Key. Keep it private. 0 doesn't start Soloist.\r\n"
        "enabled=1\r\n"
        "api_key=\r\n"
        "device=FH2 Radio\r\n"
        "; Soloist's starting volume (0-100, the Spotify app's slider)\r\n"
        "volume=100\r\n"
        "; 1: turn Soloist's own sound down (-80 dB in the volume mixer) so it's only heard on the radio\r\n"
        "quiet_speakers=1\r\n"
        "; 1: when the radio switches to Spotify, move Spotify playback to the device above and play (once Soloist is\r\n"
        ";    signed in: after you've picked the device in the Spotify app once)\r\n"
        "auto_activate=1\r\n"
        "\r\n"
        "; Music apps: each is a playlist in Audio Options. Choose it and play something in the app. While the radio plays\r\n"
        "; it, the app's own sound is turned down (-80 dB in the volume mixer) and put back after (quiet_speakers=0: not).\r\n"
        "; app= names its program (\"chrome.exe\"; empty: the usual one). Apps that don't work with Windows' media keys are\r\n"
        "; driven by their global shortcuts: set them here as they are in the app (\"Ctrl+Alt+Right\").\r\n"
        "[youtube]\r\n"
        "; music.youtube.com in a browser, or installed from the browser as an app\r\n"
        "enabled=1\r\n"
        "app=\r\n"
        "quiet_speakers=1\r\n"
        "\r\n"
        "[netease]\r\n"
        "; NetEase Cloud Music (cloudmusic.exe)\r\n"
        "enabled=1\r\n"
        "app=\r\n"
        "quiet_speakers=1\r\n"
        "play_pause_key=Ctrl+Alt+P\r\n"
        "next_key=Ctrl+Alt+Right\r\n"
        "prev_key=Ctrl+Alt+Left\r\n"
        "\r\n"
        "[qqmusic]\r\n"
        "; QQ Music (QQMusic.exe)\r\n"
        "enabled=1\r\n"
        "app=\r\n"
        "quiet_speakers=1\r\n"
        "play_pause_key=Ctrl+Alt+F5\r\n"
        "next_key=Ctrl+Alt+Right\r\n"
        "prev_key=Ctrl+Alt+Left\r\n"
        "\r\n"
        "[debug]\r\n"
        "; 1: log every request from the game to fh2-mods\\platform.log\r\n"
        "trace=0\r\n";
    HANDLE f = CreateFileW(g_ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(f, text, sizeof text - 1, &w, nullptr);
    CloseHandle(f);
}

// The music apps (apps.hpp), each from its ini section.
void start_music_apps() {
    struct Def {
        const wchar_t* section;
        const wchar_t* id;
        const wchar_t* name;
        std::vector<std::wstring> exes;
        std::vector<std::wstring> app_ids;
        bool browser;
        const wchar_t* install_path;   // environment variables expanded
        const char* hint;
    };
    const Def defs[] = {
        {L"youtube", L"xmr:youtube", L"YouTube Music", {L"msedge.exe", L"chrome.exe", L"brave.exe", L"vivaldi.exe", L"opera.exe", L"firefox.exe"},
         {L"youtube", L"cinhimbnkkaeohfgghhklpknlkffjgod"}, true, L"", "open music.youtube.com (or the YouTube Music app) and play something"},
        {L"netease", L"xmr:netease", L"NetEase Cloud Music", {L"cloudmusic.exe"}, {L"cloudmusic", L"netease"}, false,
         L"%ProgramFiles%\\Netease\\CloudMusic\\cloudmusic.exe", "start NetEase Cloud Music and play something"},
        {L"qqmusic", L"xmr:qqmusic", L"QQ Music", {L"qqmusic.exe"}, {L"qqmusic"}, false,
         L"%ProgramFiles(x86)%\\Tencent\\QQMusic\\QQMusic.exe", "start QQ Music and play something"},
    };
    bool any_quiet = false;
    for (const Def& d : defs) {
        if (!GetPrivateProfileIntW(d.section, L"enabled", 1, g_ini.c_str())) continue;
        AppOptions o;
        o.id      = d.id;
        o.name    = d.name;
        o.section = d.section;
        o.exes    = d.exes;
        std::wstring app = ini_string(d.section, L"app", L"");
        for (auto& c : app) c = static_cast<wchar_t>(towlower(c));
        if (!app.empty()) o.exes = {app};
        o.app_ids        = d.app_ids;
        o.browser        = d.browser;
        o.always_listed  = d.browser;
        wchar_t path[MAX_PATH] = {};
        if (d.install_path[0] && ExpandEnvironmentStringsW(d.install_path, path, MAX_PATH)) o.install_path = path;
        o.play_pause_key = ini_string(d.section, L"play_pause_key", L"");
        o.next_key       = ini_string(d.section, L"next_key", L"");
        o.prev_key       = ini_string(d.section, L"prev_key", L"");
        o.missing_hint   = d.hint;
        o.quiet_speakers = GetPrivateProfileIntW(d.section, L"quiet_speakers", 1, g_ini.c_str()) != 0;
        o.trace          = g_trace;
        o.ini            = g_ini;
        any_quiet        = any_quiet || o.quiet_speakers;
        app_start(o, [](const char* line) { g_api->log(g_mod, line); });
    }
    if (any_quiet) apps_start_restore_watcher(g_ini, [](const char* line) { g_api->log(g_mod, line); });
}

// An ini from an older version: add the keys it lacks (so api_key= is there to fill in).
void add_missing_settings() {
    const wchar_t* const keys[][3] = {
        {L"spotify", L"enabled", L"1"}, {L"spotify", L"api_key", L""},          {L"spotify", L"device", L"FH2 Radio"},
        {L"spotify", L"volume", L"100"}, {L"spotify", L"quiet_speakers", L"1"}, {L"spotify", L"auto_activate", L"1"},
        {L"youtube", L"enabled", L"1"},
        {L"youtube", L"app", L""},       {L"youtube", L"quiet_speakers", L"1"},  {L"netease", L"enabled", L"1"},
        {L"netease", L"app", L""},       {L"netease", L"quiet_speakers", L"1"},  {L"netease", L"play_pause_key", L"Ctrl+Alt+P"},
        {L"netease", L"next_key", L"Ctrl+Alt+Right"}, {L"netease", L"prev_key", L"Ctrl+Alt+Left"}, {L"qqmusic", L"enabled", L"1"}, {L"qqmusic", L"app", L""},
        {L"qqmusic", L"quiet_speakers", L"1"}, {L"qqmusic", L"play_pause_key", L"Ctrl+Alt+F5"},
        {L"qqmusic", L"next_key", L"Ctrl+Alt+Right"}, {L"qqmusic", L"prev_key", L"Ctrl+Alt+Left"}};
    for (const auto& k : keys) {
        wchar_t buf[8];
        if (GetPrivateProfileStringW(k[0], k[1], L"\x01", buf, 8, g_ini.c_str()) == 1 && buf[0] == 1)
            WritePrivateProfileStringW(k[0], k[1], k[2], g_ini.c_str());
    }
}

void on_setting(void*, const char* key, int value) {
    if (!std::strcmp(key, "Enabled")) WritePrivateProfileStringW(L"radio", L"enabled", value ? L"1" : L"0", g_ini.c_str());
    else if (!std::strcmp(key, "ReadTags")) WritePrivateProfileStringW(L"music", L"read_tags", value ? L"1" : L"0", g_ini.c_str());
    else if (!std::strcmp(key, "Shuffle")) WritePrivateProfileStringW(L"music", L"shuffle", value ? L"1" : L"0", g_ini.c_str());
}

void on_next(void*, const char*) { radio_next(); }

void on_next_rebind(void*, const char*, uint32_t vk, uint32_t, uint32_t pad) {
    WritePrivateProfileStringW(L"music", L"next_key", std::to_wstring(vk).c_str(), g_ini.c_str());
    WritePrivateProfileStringW(L"music", L"next_pad", std::to_wstring(pad).c_str(), g_ini.c_str());
}

void add_next_hotkey() {
    if (g_api->api_version < 9) return;
    FH2Hotkey h{};
    h.size      = sizeof h;
    h.key       = "Next";
    h.label     = tr(Text::NextHotkey);
    h.vk        = GetPrivateProfileIntW(L"music", L"next_key", VK_F6, g_ini.c_str());
    h.pad_chord = GetPrivateProfileIntW(L"music", L"next_pad", 0, g_ini.c_str());
    h.on_press  = on_next;
    h.on_rebind = on_next_rebind;
    g_api->add_hotkey(g_mod, &h);
}

void add_settings(bool read_tags) {
    if (g_api->api_version < 4) return;
    FH2Setting e{};
    e.size          = sizeof e;
    e.key           = "Enabled";
    e.label         = "Xbox Music Radio";
    e.description   = tr(Text::EnabledDesc);
    e.value         = g_enabled ? 1 : 0;
    e.needs_restart = 1;
    e.on_change     = on_setting;
    g_api->add_setting(g_mod, &e);

    FH2Setting t{};
    t.size          = sizeof t;
    t.key           = "ReadTags";
    t.label         = tr(Text::TagsLabel);
    t.description   = tr(Text::TagsDesc);
    t.value         = read_tags ? 1 : 0;
    t.needs_restart = 1;
    t.on_change     = on_setting;
    g_api->add_setting(g_mod, &t);

    FH2Setting sh{};
    sh.size          = sizeof sh;
    sh.key           = "Shuffle";
    sh.label         = tr(Text::ShuffleLabel);
    sh.description   = tr(Text::ShuffleDesc);
    sh.value         = g_radio.shuffle ? 1 : 0;
    sh.needs_restart = 1;
    sh.on_change     = on_setting;
    g_api->add_setting(g_mod, &sh);
}

void cmd_status(void*, const char*, const char*, char* reply, size_t size) {
    std::snprintf(reply, size, "%s; Xbox Music %s; %s", narrow(library_summary()).c_str(),
                  g_last_available == 1 ? "available" : g_last_available == 0 ? "not available" : "not checked yet",
                  radio_status().c_str());
}

void cmd_next(void*, const char*, const char*, char* reply, size_t size) {
    radio_next();
    std::snprintf(reply, size, "skipping to the next song");
}

void cmd_rescan(void*, const char*, const char*, char* reply, size_t size) {
    uint32_t gen = library_rescan();
    std::snprintf(reply, size, "rescan %u started", gen);
}

DWORD WINAPI worker(LPVOID) {
    // wait_for_code, not wait_for_game: Extra Options has to patch first.
    void* code = g_api->wait_for_code(0xFFFFFFFFu);
    if (!code) return 0;
    g_base = reinterpret_cast<uintptr_t>(code);
    i18n_watch(g_base, [](const char* line) { g_api->log(g_mod, line); });
    if (install_hooks()) logf("Xbox Music hooks on");
    else logf("Xbox Music hooks FAILED: the radio stays off");
    return 0;
}

} // namespace

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* api) {
    if (api->api_version < 3) return 1;   // hooks
    g_api = api;

    std::string language = i18n_init(std::wstring(api->azimuth_dir) + L"\\XboxMusicRadio\\XboxMusicRadio.ini", api->azimuth_dir,
                                     api->game_dir ? api->game_dir : L"");   // before any text

    FH2ModInfo info{};
    info.size             = sizeof info;
    info.name             = "XboxMusicRadio";
    info.version          = XBOXMUSICRADIO_VERSION;
    info.github_repo      = "qiangqiang101/XboxMusicRadio";
    info.platform_version = FH2MOD_PLATFORM_VERSION;
    info.check_updates    = 1;
    info.description      = tr(Text::ModDescription);
    g_mod = api->register_mod(&info);
    if (!g_mod) return 0;
    logf("text in the game's language: %s", language.c_str());

    std::wstring dir = std::wstring(api->azimuth_dir) + L"\\XboxMusicRadio";
    CreateDirectoryW(dir.c_str(), nullptr);
    g_ini = dir + L"\\XboxMusicRadio.ini";
    write_default_ini();

    g_enabled      = GetPrivateProfileIntW(L"radio", L"enabled", 1, g_ini.c_str()) != 0;
    g_trace        = GetPrivateProfileIntW(L"debug", L"trace", 0, g_ini.c_str()) != 0;
    bool read_tags = GetPrivateProfileIntW(L"music", L"read_tags", 1, g_ini.c_str()) != 0;
    g_radio.shuffle = GetPrivateProfileIntW(L"music", L"shuffle", 1, g_ini.c_str()) != 0;
    g_radio.trace   = g_trace;
    add_settings(read_tags);
    if (!g_enabled) {
        logf("turned off ([radio] enabled=0)");
        return 0;
    }
    // The Audio Options X button's label, "Launch Xbox Music" (HelpButtons.str 0x70C2 IDS_Launch_Xbox_Music): it
    // refreshes now (launch_music_app_detour). set_string would write one text to every language; replacing each
    // language's own text instead gives each table its translation, right from the first start (the game's language
    // isn't known yet here). German and Dutch share theirs: that one goes by the language seen last.
    {
        bool dutch = std::strcmp(tr(Text::RefreshButton), tr_in("NL", Text::RefreshButton)) == 0;
        const struct {
            const char* code;
            const wchar_t* original;
        } labels[] = {
            {"EN", L"Launch Xbox Music"},   {dutch ? "NL" : "DE", L"Xbox Music starten"}, {"FR", L"Lancer Xbox Music"},
            {"ES", L"Iniciar Xbox Music"},  {"IT", L"Avvia Xbox Music"},  {"JP", L"Xbox Music 開始"},
            {"KO", L"Xbox Music 실행"},     {"CHT", L"啟動 Xbox Music"},  {"PL", L"Uruchom Xbox Music"},
            {"RU", L"Включить Xbox Music"}, {"BR", L"Abrir o Xbox Music"},
        };
        for (const auto& l : labels) {
            const char* to = tr_in(l.code, Text::RefreshButton);
            int n          = MultiByteToWideChar(CP_UTF8, 0, to, -1, nullptr, 0);
            std::wstring w(n > 0 ? static_cast<size_t>(n - 1) : 0, L'\0');
            if (n > 1) MultiByteToWideChar(CP_UTF8, 0, to, -1, w.data(), n);
            api->replace_string(g_mod, "HelpButtons.str", l.original, w.c_str());
        }
    }

    LibraryOptions opt;
    opt.folder = ini_string(L"music", L"folder", L"");
    if (opt.folder.empty()) opt.folder = windows_music_folder();
    while (!opt.folder.empty() && (opt.folder.back() == L'\\' || opt.folder.back() == L'/')) opt.folder.pop_back();
    opt.read_tags = read_tags;
    if (GetFileAttributesW(opt.folder.c_str()) == INVALID_FILE_ATTRIBUTES)
        logf("music folder %s doesn't exist: no playlists until it does", narrow(opt.folder).c_str());
    library_start(opt, [](const char* line) { g_api->log(g_mod, line); });

    add_missing_settings();
    if (GetPrivateProfileIntW(L"spotify", L"enabled", 1, g_ini.c_str()) != 0) {
        SpotifyOptions sp;
        sp.dir            = dir;
        sp.api_key        = ini_string(L"spotify", L"api_key", L"");
        sp.device         = ini_string(L"spotify", L"device", L"FH2 Radio");
        sp.volume         = GetPrivateProfileIntW(L"spotify", L"volume", 100, g_ini.c_str());
        sp.quiet_speakers = GetPrivateProfileIntW(L"spotify", L"quiet_speakers", 1, g_ini.c_str()) != 0;
        sp.auto_activate  = GetPrivateProfileIntW(L"spotify", L"auto_activate", 1, g_ini.c_str()) != 0;
        spotify_start(sp, [](const char* line) { g_api->log(g_mod, line); });
    }
    start_music_apps();
    anna_start(api, g_mod);   // after the live players: its Playlist list has them

    if (api->api_version >= 9) {
        api->add_command(g_mod, "status", "playlists, songs and whether the game offers Xbox Music", cmd_status, nullptr);
        api->add_command(g_mod, "rescan", "scan the music folder again", cmd_rescan, nullptr);
        api->add_command(g_mod, "next", "skip to the next song", cmd_next, nullptr);
    }
    add_next_hotkey();
    if (HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(t);
    return 0;
}
