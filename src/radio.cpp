// The "Xbox Music" radio station, fed from the music folder.
//
// Ported from FH2_SpotifyMin's folder mode (GPL-3.0-or-later), whose game addresses and routing
// technique come from Bullet4fun's spotify-radio mod (FH2_ModsCollection, GPL-3.0). Changes here:
// hooks go through the FH2 Mod Platform, songs come from the library's playlists (the one chosen in
// Audio Options), titles and artists from the tags, and a hotkey skips to the next song.
//
// How the station works on PC: RadioSystem only offers "Xbox Music" when its gate (vtable +0xC0)
// says so, the profile reports the station locked, and the audio engine only built a stub for the
// dead service's stream. So: the gate says yes, the lock says no, and the station's play/stop command
// builds the real FMOD stream object and its Studio event ("event:/Master/Radio/XboxLiveStreaming")
// and starts or stops it. The stream's PCM read callback is replaced: it reads a ring buffer that a
// thread of ours fills with decoded songs, at the pace the game reads.
#include "radio.hpp"

#include "decoder.h"
#include "live.hpp"
#include "i18n.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <vector>

namespace {

constexpr uintptr_t kImageBase = 0x140000000;

// Game addresses (VAs at the preferred base).
constexpr uintptr_t kRadioVtable      = 0x142E76818;   // RadioSystem vtable; slot +0xC0 = "is Xbox Music available"
constexpr uintptr_t kStreamRead       = 0x141014960;   // FMOD PCM read callback of the endless Xbox Music stream
constexpr uintptr_t kStationLocked    = 0x140A65DF0;   // (profile, std::string* name)
constexpr uintptr_t kFeatureUnlocked  = 0x140A67500;   // (unlocks, std::string* name): campaign feature unlocks
constexpr uintptr_t kXboxCommand      = 0x141018420;   // RadioSystem Xbox Music command: 0 play, 1 stop
constexpr uintptr_t kHudRadioEvent    = 0x1407835B0;   // HudStatusRadioManager::OnSystemEvent
constexpr uintptr_t kHudPush          = 0x14078A070;   // pushes the manager's text to the HUD view model
constexpr uintptr_t kStringAssign     = 0x14022C300;   // std::string::assign(const char*, size_t)
constexpr uintptr_t kSnapshotCopy     = 0x141024570;   // mixer snapshot parameter copy
constexpr uintptr_t kSetPosition      = 0x1419ACEC0;   // FMOD Channel::setPosition
constexpr uintptr_t kPlaySound        = 0x14199B0F0;   // FMOD System::playSound
constexpr uintptr_t kXboxStreamVtable = 0x142E76190;
constexpr uintptr_t kAudioEnginePtr   = 0x14365D3D8;
constexpr uintptr_t kStreamFactory    = 0x140FEEEC0;
constexpr uintptr_t kEventStart       = 0x141A9B390;
constexpr uintptr_t kEventStop        = 0x141A9B550;
constexpr uintptr_t kEventGetTimeline = 0x141A98B10;
constexpr uintptr_t kEventSetTimeline = 0x141A9B040;
constexpr uintptr_t kParamSetValue    = 0x141A9B1E0;
constexpr uintptr_t kChGetGroup       = 0x1419ABBD0;
constexpr uintptr_t kGrpGetParent     = 0x1419A7170;
constexpr uintptr_t kCtlSetVolume     = 0x1419A6470;
constexpr uintptr_t kCtlGetDsp        = 0x1419A2860;
constexpr uintptr_t kCtlGetNumDsps    = 0x1419A3230;
constexpr uintptr_t kDspGetInfo       = 0x1419A84C0;
constexpr uintptr_t kGrpGetNumGroups  = 0x1419A7050;
constexpr uintptr_t kGrpGetGroup      = 0x1419A6C90;
constexpr uintptr_t kGrpAddGroup      = 0x1419A6890;
constexpr uintptr_t kDspSetBypass     = 0x1419A9CF0;

const FH2ModApi* g_api = nullptr;
FH2Mod* g_mod          = nullptr;
uintptr_t g_base       = 0;
RadioOptions g_opt;

template <typename T>
T game(uintptr_t va) {
    return reinterpret_cast<T>(g_base + (va - kImageBase));
}

void logf(const char* fmt, ...) {
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

// MSVC std::string: chars inline when capacity < 16, else a pointer; size at +0x10, capacity at +0x18.
bool str_is(const void* s, const char* text) {
    size_t n     = std::strlen(text);
    auto p       = static_cast<const uint8_t*>(s);
    uint64_t sz  = *reinterpret_cast<const uint64_t*>(p + 0x10);
    uint64_t cap = *reinterpret_cast<const uint64_t*>(p + 0x18);
    if (sz != n) return false;
    const char* chars = cap > 15 ? *reinterpret_cast<const char* const*>(p) : reinterpret_cast<const char*>(p);
    return std::memcmp(chars, text, n) == 0;
}

std::string str_text(const void* s) {
    auto p       = static_cast<const uint8_t*>(s);
    uint64_t sz  = *reinterpret_cast<const uint64_t*>(p + 0x10);
    uint64_t cap = *reinterpret_cast<const uint64_t*>(p + 0x18);
    if (sz > 256) return "?";
    const char* chars = cap > 15 ? *reinterpret_cast<const char* const*>(p) : reinterpret_cast<const char*>(p);
    return std::string(chars, sz);
}

// The HUD font draws one glyph per byte: decompose accents and keep printable ASCII.
std::string fold_ascii(const std::wstring& w, size_t max_chars) {
    std::string out;
    for (wchar_t c : w) {
        wchar_t f[8];
        int k = FoldStringW(MAP_COMPOSITE, &c, 1, f, 8);
        if (k <= 0) {
            f[0] = c;
            k    = 1;
        }
        for (int j = 0; j < k; ++j)
            if (f[j] >= 32 && f[j] < 127 && out.size() < max_chars) out += static_cast<char>(f[j]);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// ---- Audio ring: player thread -> ring -> the game's FMOD stream read ------------------------------------

constexpr uint64_t kRingFrames = 128 * 1024;
constexpr uint64_t kTarget     = 44100 / 2;                // keep ~0.5 s banked
constexpr uint64_t kMaxBehind  = kTarget + 44100 * 3 / 2;  // further behind than this: jump to live
Frame g_ring[kRingFrames];
std::atomic<uint64_t> g_wr{0}, g_rd{0}, g_last_pull{0};
std::atomic<void*> g_sound{nullptr}, g_channel{nullptr}, g_instance{nullptr};
std::atomic<bool> g_selected{false};   // the station is tuned in
std::atomic<bool> g_flush{false};      // drop what's banked (another song was chosen)

int stream_read(void* sound, void* data, unsigned len) {
    g_sound = sound;   // FMOD reads once on creation, before Studio plays it: lets playSound find this stream
    g_last_pull.store(GetTickCount64(), std::memory_order_relaxed);
    auto* out        = static_cast<Frame*>(data);
    unsigned n       = len / sizeof(Frame);
    static bool buffering = true;
    uint64_t w = g_wr.load(std::memory_order_acquire), r = g_rd.load(std::memory_order_relaxed);
    if (g_flush.exchange(false)) {
        r         = w;
        buffering = true;
    }
    if (w - r > kMaxBehind) {   // the game wasn't listening: catch up
        r         = w - kTarget;
        buffering = false;
    }
    if (buffering && w - r >= kTarget) buffering = false;
    for (unsigned i = 0; i < n; ++i) {
        if (!buffering && r < w) {
            out[i] = g_ring[r++ % kRingFrames];
        } else {   // underrun: silence until the ring has refilled
            out[i].l = out[i].r = 0;
            buffering = true;
        }
    }
    g_rd.store(r, std::memory_order_release);
    return 0;
}

// The player and Soloist's recording both write; only one at a time is the station's source, but a switch can overlap.
std::mutex g_ring_mx;
void ring_write(const Frame* src, size_t frames) {
    std::lock_guard<std::mutex> lk(g_ring_mx);
    uint64_t w = g_wr.load(std::memory_order_relaxed);
    for (size_t i = 0; i < frames; ++i) g_ring[(w + i) % kRingFrames] = src[i];
    g_wr.store(w + frames, std::memory_order_release);
}

// ---- Routing the stream like any station track ------------------------------------------------------------

bool dsp_named(void* ctl, const char* want, void** found) {
    int count = 0;
    if (game<int (*)(void*, int*)>(kCtlGetNumDsps)(ctl, &count) != 0) return false;
    for (int i = 0; i < count && i < 16; ++i) {
        void* dsp = nullptr;
        char name[33] = {};
        unsigned ver = 0;
        int c = 0, w = 0, h = 0;
        if (game<int (*)(void*, int, void**)>(kCtlGetDsp)(ctl, i, &dsp) == 0 && dsp &&
            game<int (*)(void*, char*, unsigned*, int*, int*, int*)>(kDspGetInfo)(dsp, name, &ver, &c, &w, &h) == 0 &&
            std::strcmp(name, want) == 0) {
            if (found) *found = dsp;
            return true;
        }
    }
    return false;
}

// The Xbox Music event is wired past the radio bus and its Send fader is 0. Re-parent its top group under
// the radio bus (the group with a "Stereo Transceiver" DSP), bypass its Send, raise its fader: it then takes
// the same path as any station track (mixer snapshots, festival speakers, car meets).
void route_through_radio_bus(void* channel) {
    auto num_groups = game<int (*)(void*, int*)>(kGrpGetNumGroups);
    auto child      = game<int (*)(void*, int, void**)>(kGrpGetGroup);
    auto add        = game<int (*)(void*, void*, unsigned char, void*)>(kGrpAddGroup);
    auto bypass     = game<int (*)(void*, unsigned char)>(kDspSetBypass);
    void* group     = nullptr;
    if (game<int (*)(void*, void**)>(kChGetGroup)(channel, &group) != 0) return;
    for (int level = 0; group && level < 8; ++level) {
        void *parent = nullptr, *send = nullptr, *bus = nullptr;
        if (game<int (*)(void*, void**)>(kGrpGetParent)(group, &parent) != 0 || !parent) return;
        if (dsp_named(parent, "Stereo Transceiver", nullptr)) bus = parent;
        int siblings = 0;
        for (int i = 0; !bus && num_groups(parent, &siblings) == 0 && i < siblings && i < 64; ++i) {
            void* s = nullptr;
            if (child(parent, i, &s) == 0 && s && s != group && dsp_named(s, "Stereo Transceiver", nullptr)) bus = s;
        }
        if (bus) {
            int moved = bus == parent ? 0 : add(bus, group, 1, nullptr);
            int byp   = dsp_named(group, "FMOD Send", &send) ? bypass(send, 1) : -1;
            game<int (*)(void*, float)>(kCtlSetVolume)(group, 1.0f);
            logf("routed onto the radio bus (move %d, send bypass %d)", moved, byp);
            return;
        }
        group = parent;
    }
}

int (*g_play_orig)(void*, void*, void*, int, void**) = nullptr;
int play_sound(void* system, void* sound, void* cgroup, int paused, void** channel) {
    if (!sound || sound != g_sound.load()) return g_play_orig(system, sound, cgroup, paused, channel);
    void* local = nullptr;
    void** out  = channel ? channel : &local;
    int res     = g_play_orig(system, sound, cgroup, paused, out);
    if (res == 0 && *out) {
        g_channel = *out;
        route_through_radio_bus(*out);
    }
    return res;
}

// Studio seeks the programmer sound when the event timeline moves; a live stream has no position.
int (*g_setpos_orig)(void*, unsigned, unsigned) = nullptr;
int set_position(void* ch, unsigned pos, unsigned unit) {
    if (ch && ch == g_channel.load()) return 0;
    return g_setpos_orig(ch, pos, unit);
}

// The Studio event's programmer instrument lasts 600 s. Once a second, move the timeline back 240 s when
// it's past 300 s, so it never ends (the stream just keeps reading the ring).
void keep_timeline() {
    static ULONGLONG checked = 0;
    ULONGLONG now = GetTickCount64();
    void* inst    = g_instance.load();
    int pos       = 0;
    if (!g_selected || !inst || now - checked < 1000) return;
    checked = now;
    if (game<int (*)(void*, int*)>(kEventGetTimeline)(inst, &pos) != 0 || pos < 300000) return;
    game<int (*)(void*, int)>(kEventSetTimeline)(inst, pos - 240000);
}

// ---- Station gate, lock and command -------------------------------------------------------------------------

bool (*g_gate_orig)(void*) = nullptr;
std::atomic<long> g_gate_calls{0};
bool gate(void*) {
    if (g_opt.trace && g_gate_calls++ < 20) logf("trace: station gate asked: yes");
    return true;
}

int (*g_locked_orig)(void*, void*) = nullptr;
int station_locked(void* profile, void* name) {
    bool xbox  = str_is(name, "Xbox Music");   // before the original: it frees the string
    int locked = g_locked_orig(profile, name);
    if (xbox && g_opt.trace) {
        static int last = -1;
        if (locked != last) logf("trace: station lock asked for Xbox Music: game says %d, answered 0", locked);
        last = locked;
    }
    return xbox ? 0 : locked;
}

// The campaign's feature unlocks (game+0xA67500(unlocks, std::string* name): one byte per name, "xbox_music" at +0x2E,
// "xbox_music_playlists" at +0x2F). Audio Options greys out its Xbox Music row until the save has unlocked it, so a
// save early in the campaign can't choose a playlist. Answered yes for both; the string isn't freed.
int (*g_unlocked_orig)(void*, void*) = nullptr;
int feature_unlocked(void* unlocks, void* name) {
    if (str_is(name, "xbox_music") || str_is(name, "xbox_music_playlists")) return 1;
    return g_unlocked_orig(unlocks, name);
}

// The radio UI's "next station" (game+0x660E50): walks a std::map of stations at +0x68 (node: left +0, parent +8,
// right +0x10, nil flag +0x19, name at +0x28), skipping those its "available" check (game+0x668810) turns down.
// Traced: the map's names, and each station's answer when it changes.
constexpr uintptr_t kNextStation      = 0x140660E50;
constexpr uintptr_t kStationAvailable = 0x140668810;

std::string station_map_names(void* ui) {
    auto nil = [](const uint8_t* n) { return n[0x19] != 0; };
    const uint8_t* head = *reinterpret_cast<const uint8_t* const*>(static_cast<uint8_t*>(ui) + 0x68);
    if (!head) return "(none)";
    const uint8_t* n = *reinterpret_cast<const uint8_t* const*>(head);   // leftmost
    std::string out;
    for (int i = 0; i < 64 && n && !nil(n); ++i) {
        out += (out.empty() ? "" : ", ") + str_text(n + 0x28);
        const uint8_t* r = *reinterpret_cast<const uint8_t* const*>(n + 0x10);
        if (!nil(r)) {
            n = r;
            while (!nil(*reinterpret_cast<const uint8_t* const*>(n))) n = *reinterpret_cast<const uint8_t* const*>(n);
        } else {
            const uint8_t* p = *reinterpret_cast<const uint8_t* const*>(n + 8);
            while (!nil(p) && n == *reinterpret_cast<const uint8_t* const*>(p + 0x10)) {
                n = p;
                p = *reinterpret_cast<const uint8_t* const*>(p + 8);
            }
            n = p;
        }
    }
    return out;
}

uint64_t (*g_next_station_orig)(void*, int) = nullptr;
uint64_t next_station(void* ui, int dir) {
    static std::string last;
    std::string names = station_map_names(ui);
    if (names != last) logf("trace: the radio's station list: %s", names.c_str());
    last = names;
    return g_next_station_orig(ui, dir);
}

bool (*g_station_available_orig)(void*, void*) = nullptr;
bool station_available(void* ui, void* name) {
    static std::mutex mx;
    static std::vector<std::pair<std::string, bool>> seen;
    std::string n = str_text(name);
    bool ok       = g_station_available_orig(ui, name);
    std::lock_guard<std::mutex> lk(mx);
    auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& e) { return e.first == n; });
    if (it == seen.end() || it->second != ok) logf("trace: station \"%s\" available: %s", n.c_str(), ok ? "yes" : "no");
    if (it == seen.end()) seen.emplace_back(n, ok);
    else it->second = ok;
    return ok;
}

// RadioSystem::SetStation(name): traced, to see which stations the radio tunes to.
uint64_t (*g_set_station_orig)(void*, void*) = nullptr;
uint64_t set_station(void* radio, void* name) {
    if (g_opt.trace) logf("trace: radio tunes to \"%s\"", str_text(name).c_str());
    return g_set_station_orig(radio, name);
}

// The stock game builds only a stub for the dead service. Build the real FMOD stream object (and its Studio
// event) the way the audio engine would, and swap it in.
void* xbox_event(uint8_t* decoder) {
    void* obj = *reinterpret_cast<void**>(decoder + 0x18);
    if (!obj || *static_cast<void**>(obj) != game<void*>(kXboxStreamVtable)) {
        void* engine   = *game<void**>(kAudioEnginePtr);
        void* made[2]  = {nullptr, nullptr};
        if (!engine) return nullptr;
        game<void* (*)(void**, void*, void*, void*)>(kStreamFactory)(made, static_cast<uint8_t*>(engine) + 0x7F0, nullptr, nullptr);
        if (!made[0] || *static_cast<void**>(made[0]) != game<void*>(kXboxStreamVtable)) {
            logf("the station's stream object could not be built");
            return nullptr;
        }
        auto init = reinterpret_cast<unsigned char (*)(void*, void*)>((*static_cast<void***>(made[0]))[0]);
        unsigned char ok = init(made[0], decoder);
        *reinterpret_cast<void**>(decoder + 0x18) = made[0];
        *reinterpret_cast<void**>(decoder + 0x20) = made[1];
        obj = made[0];
        logf("station stream built (init %u)", ok);
    }
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(obj) + 0x30);
}

void (*g_command_orig)(uint8_t*, int) = nullptr;
void command(uint8_t* decoder, int cmd) {
    if (cmd != 0 && cmd != 1) {
        g_command_orig(decoder, cmd);
        return;
    }
    g_selected = cmd == 0;
    void* ev   = xbox_event(decoder);
    g_instance = ev;
    int res    = -1;
    if (ev) res = cmd == 0 ? game<int (*)(void*)>(kEventStart)(ev) : game<int (*)(void*, int)>(kEventStop)(ev, 1);
    logf("station %s (result %d)", cmd == 0 ? "on" : "off", res);
}

// ---- Now playing on the radio HUD ------------------------------------------------------------------------------
// HudStatusRadioManager::OnSystemEvent: event 0x10 ("now playing") copies the station (+0x48), track (+0x08) and
// artist (+0x28) into the manager, clears the error flag (+0x78) and pushes them to the HUD. The banner only shows
// when that text isn't empty, so on Xbox Music we fill it in. Event 0x22 is the dead service's sign-in error.

std::mutex g_np_mx;
std::string g_np_title, g_np_artist;

void (*g_hud_orig)(uint8_t*, int, void*, void*) = nullptr;
void hud(uint8_t* mgr, int category, void* code, void* payload) {
    if (category == 0x22) return;
    g_hud_orig(mgr, category, code, payload);
    // 0x10 arrives about every frame: push only changed text, or the banner restarts every time.
    if (category != 0x10 || !str_is(mgr + 0x48, "Xbox Music")) return;
    std::string title, artist;
    {
        std::lock_guard<std::mutex> lk(g_np_mx);
        title  = g_np_title.empty() ? "Xbox Music Radio" : g_np_title;
        artist = g_np_artist;
    }
    if (str_is(mgr + 0x08, title.c_str()) && str_is(mgr + 0x28, artist.c_str()) && !mgr[0x78]) return;
    auto assign = game<void (*)(void*, const char*, size_t)>(kStringAssign);
    assign(mgr + 0x08, title.c_str(), title.size());
    assign(mgr + 0x28, artist.c_str(), artist.size());
    mgr[0x78] = 0;
    game<void (*)(void*)>(kHudPush)(mgr);
}

// ---- Mixer snapshot: report RADIO_Station 8 (Xbox Music) as 6 (a stock station), so the festival speakers and
// car-meet mix stay on instead of fading the TreatedMusic bus to 0. Replaces the function. ----

void (*g_snap_orig)(uint8_t*) = nullptr;
void snapshot(uint8_t* mgr) {
    auto set_value = game<int (*)(void*, float)>(kParamSetValue);
    for (uint8_t* e = *reinterpret_cast<uint8_t**>(mgr + 0x48); e != *reinterpret_cast<uint8_t**>(mgr + 0x50); e += 0x20) {
        void* inst   = *reinterpret_cast<void**>(e + 8);
        uint8_t* src = *reinterpret_cast<uint8_t**>(e + 0x10);
        if (!inst) {
            *reinterpret_cast<unsigned*>(src + 0x80) = 2;
            continue;
        }
        float v = *reinterpret_cast<float*>(src + 0x14);
        if (v == 8.0f && str_is(src + 0x60, "RADIO_Station")) v = 6.0f;
        set_value(inst, v);
    }
}

// ---- The player --------------------------------------------------------------------------------------------------

std::mutex g_sel_mx;
std::wstring g_sel_id;                 // chosen playlist; empty = All music (or the only one)
std::atomic<uint32_t> g_sel_gen{0};    // bumped when the choice changes
std::atomic<bool> g_skip{false};
std::atomic<bool> g_back{false};   // the song before
std::wstring g_playing_list, g_playing_song;

// The live players (Spotify, music apps), in Audio Options order. While one is the station's source, its recording
// feeds the ring (live_audio).
std::mutex g_players_mx;
std::vector<LivePlayer*> g_players;
std::atomic<const LivePlayer*> g_live{nullptr};

bool listening() { return GetTickCount64() - g_last_pull.load(std::memory_order_relaxed) < 1000; }

LivePlayer* find_player(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(g_players_mx);
    for (LivePlayer* p : g_players)
        if (id == p->id()) return p;
    return nullptr;
}

// The chosen playlist (or live player); if it's gone, All music; with no music files, the first live player playing.
// Only the player thread calls this; with nothing to play it's asked every second, so each fallback is told once per choice.
LivePlayer* pick_source(Playlist& out, bool& folder) {
    static std::wstring warned_id;
    folder = false;
    std::wstring id;
    {
        std::lock_guard<std::mutex> lk(g_sel_mx);
        id = g_sel_id;
    }
    if (LivePlayer* p = find_player(id)) {
        if (p->available() || p->wait_when_missing()) {
            warned_id.clear();
            return p;
        }
        if (warned_id != id) logf("%s was chosen but isn't running: playing all music", narrow(p->name()).c_str());
        warned_id = id;
    } else if (!id.empty()) {
        if (library_playlist(id, out)) {
            warned_id.clear();
            folder = true;
            return nullptr;
        }
        if (warned_id != id) logf("chosen playlist \"%s\" isn't in the music folder any more: playing all music", narrow(id).c_str());
        warned_id = id;
    }
    std::vector<Playlist> lists = library_playlists();
    if (!lists.empty()) {
        out    = lists[0];   // All music when there are several
        folder = true;
        return nullptr;
    }
    std::lock_guard<std::mutex> lk(g_players_mx);
    for (LivePlayer* p : g_players)
        if (p->available() && p->playing()) return p;
    return nullptr;
}

void set_now_playing(const std::wstring& list, const std::wstring& title, const std::wstring& artist, const std::wstring& label) {
    std::lock_guard<std::mutex> lk(g_np_mx);
    g_playing_list = list;
    g_np_title     = fold_ascii(title, 60);
    g_np_artist    = fold_ascii(artist.empty() ? list : artist, 40);
    if (g_np_artist.empty()) g_np_artist = "Xbox Music";   // a name in a script the display can't draw
    g_playing_song = label;
}

// Until another playlist is chosen: the player's recording feeds the ring, this keeps the banner current, skips on F6,
// and pauses the player while the station is off (resuming what it paused).
void play_live(uint32_t gen, LivePlayer& src) {
    std::string name = narrow(src.name());
    logf("playing %s", name.c_str());
    set_now_playing(src.name(), src.name(), L"", L"");
    g_flush = true;   // drop what's banked of the last song
    g_live  = &src;
    bool paused_it = false, told_missing = false;
    ULONGLONG away = 0;
    while (gen == g_sel_gen.load()) {
        keep_timeline();
        if (!src.available()) {
            if (!src.wait_when_missing()) break;
            if (!told_missing) {
                logf("%s: waiting: %s", name.c_str(), src.missing_hint());
                std::wstring waiting = trw(Text::WaitingFor);
                size_t at            = waiting.find(L"{0}");
                if (at != std::wstring::npos) waiting.replace(at, 3, src.name());
                set_now_playing(src.name(), waiting, L"", L"");
            }
            told_missing = true;
            src.set_live(false);
            Sleep(250);
            continue;
        }
        told_missing = false;
        src.set_live(listening());   // its own sound only turned down while the radio plays it
        std::wstring title, artist;
        if (src.now_playing(title, artist)) {
            std::wstring label = artist.empty() ? title : artist + L" - " + title;
            set_now_playing(src.name(), title, artist, label);
            logf("%s: %s", name.c_str(), narrow(label).c_str());
        }
        if (g_skip.exchange(false)) src.next();
        if (g_back.exchange(false)) src.previous();
        if (listening()) {
            away = 0;
            if (paused_it) {
                src.resume();
                paused_it = false;
            }
        } else if (!away) {
            away = GetTickCount64();
        } else if (!paused_it && GetTickCount64() - away > 1500 && src.playing()) {
            src.pause();
            paused_it = true;
        }
        Sleep(50);
    }
    if (gen != g_sel_gen.load() && src.playing()) src.pause();   // another playlist was chosen
    src.set_live(false);
    g_live  = nullptr;
    g_flush = true;
}

DWORD WINAPI player(LPVOID) {
    static Frame buf[FT_MAX_OUT];
    std::mt19937 rng(static_cast<uint32_t>(GetTickCount64()));
    bool told_empty = false;
    for (;;) {
        if (library_done() == 0 || !listening()) {   // decode only while the station is on
            Sleep(50);
            continue;
        }
        Playlist pl;
        uint32_t gen     = g_sel_gen.load();
        bool folder      = false;
        LivePlayer* live = pick_source(pl, folder);
        if (live) {
            told_empty = false;
            play_live(gen, *live);
            continue;
        }
        if (!folder || pl.songs.empty()) {
            if (!told_empty) logf("nothing to play: put mp3, flac or wav files in the music folder");
            told_empty = true;
            Sleep(1000);
            continue;
        }
        told_empty = false;
        {
            std::lock_guard<std::mutex> lk(g_np_mx);
            g_playing_list = pl.name;
        }
        std::vector<uint32_t> order = pl.songs;
        if (g_opt.shuffle) std::shuffle(order.begin(), order.end(), rng);
        logf("playlist \"%s\": %zu song(s)%s", narrow(pl.name).c_str(), order.size(), g_opt.shuffle ? ", shuffled" : "");
        size_t failures = 0;
        for (size_t i = 0; i < order.size() && gen == g_sel_gen.load(); ++i) {
            Song s;
            if (!library_song(order[i], s)) continue;
            FolderTrack* t = ft_open(s.path.c_str());
            if (!t) {
                logf("can't play %s (only mp3, flac and wav)", narrow(s.path).c_str());
                if (++failures >= order.size()) Sleep(1000);
                continue;
            }
            failures = 0;
            std::wstring label = s.artist.empty() ? s.title : s.artist + L" - " + s.title;
            set_now_playing(pl.name, s.title, s.artist, label);
            logf("playing %s", narrow(label).c_str());
            g_skip = false;
            g_back = false;
            for (;;) {
                keep_timeline();
                bool back = g_back.exchange(false);
                if (gen != g_sel_gen.load() || g_skip.exchange(false) || back) {
                    g_flush = true;   // a new choice or a skip: drop what's banked of this song
                    if (back) i = (i == 0 ? 0 : i - 1) - 1;   // the loop's ++i lands on the song before (the first: itself)
                    break;
                }
                uint64_t fill = g_wr.load() - g_rd.load(std::memory_order_acquire);
                if (!listening() || fill >= kTarget) {
                    Sleep(10);
                    continue;
                }
                size_t got = ft_read(t, buf);
                if (!got) break;   // end of the song
                ring_write(buf, got);
            }
            ft_close(t);
        }
    }
}

bool hook_fn(const char* name, uintptr_t va, void* detour, void** orig) {
    if (g_api->hook_function(g_mod, name, game<void*>(va), detour, orig)) return true;
    logf("couldn't hook %s at game+0x%llX", name, static_cast<unsigned long long>(va - kImageBase));
    return false;
}

} // namespace

// The radio's station list is built once, early while the game loads, asking the gate: if the gate isn't hooked by
// then, Xbox Music is missing from the radio for the whole session. So these two go first and on right away, before
// the rest (which take a second or two to make while other mods make theirs).
bool radio_install_gate(const FH2ModApi* api, FH2Mod* mod, uintptr_t base, const RadioOptions& opt) {
    g_api  = api;
    g_mod  = mod;
    g_base = base;
    g_opt  = opt;
    FH2Hook* g = g_api->hook_pointer(g_mod, "RadioSystem Xbox Music gate (always available)", game<void**>(kRadioVtable + 0xC0),
                                     reinterpret_cast<void*>(&gate), reinterpret_cast<void**>(&g_gate_orig));
    if (!g || !g_api->enable_hooks(g_mod, g)) {
        logf("couldn't hook the RadioSystem gate");
        return false;
    }
    FH2Hook* l = g_api->hook_function(g_mod, "Xbox Music station lock", game<void*>(kStationLocked), reinterpret_cast<void*>(&station_locked),
                                      reinterpret_cast<void**>(&g_locked_orig));
    if (!l || !g_api->enable_hooks(g_mod, l)) {
        logf("couldn't hook the station lock at game+0x%llX", static_cast<unsigned long long>(kStationLocked - kImageBase));
        return false;
    }
    return true;
}

bool radio_install() {
    void* unused = nullptr;
    bool ok      = true;
    if (g_opt.trace && !g_api->hook_pointer(g_mod, "RadioSystem SetStation (trace)", game<void**>(kRadioVtable + 0x40),
                                            reinterpret_cast<void*>(&set_station), reinterpret_cast<void**>(&g_set_station_orig)))
        logf("couldn't hook SetStation for the trace");
    if (g_opt.trace) {
        hook_fn("radio next station (trace)", kNextStation, reinterpret_cast<void*>(&next_station), reinterpret_cast<void**>(&g_next_station_orig));
        hook_fn("radio station available (trace)", kStationAvailable, reinterpret_cast<void*>(&station_available),
                reinterpret_cast<void**>(&g_station_available_orig));
    }
    ok &= hook_fn("Xbox Music feature unlock (Audio Options row)", kFeatureUnlocked, reinterpret_cast<void*>(&feature_unlocked),
                  reinterpret_cast<void**>(&g_unlocked_orig));
    ok &= hook_fn("Xbox Music stream read (music folder)", kStreamRead, reinterpret_cast<void*>(&stream_read), &unused);
    ok &= hook_fn("Xbox Music play/stop", kXboxCommand, reinterpret_cast<void*>(&command), reinterpret_cast<void**>(&g_command_orig));
    ok &= hook_fn("radio HUD (now playing)", kHudRadioEvent, reinterpret_cast<void*>(&hud), reinterpret_cast<void**>(&g_hud_orig));
    ok &= hook_fn("mixer snapshot (station 8 as 6)", kSnapshotCopy, reinterpret_cast<void*>(&snapshot), reinterpret_cast<void**>(&g_snap_orig));
    ok &= hook_fn("FMOD Channel::setPosition (live stream)", kSetPosition, reinterpret_cast<void*>(&set_position), reinterpret_cast<void**>(&g_setpos_orig));
    ok &= hook_fn("FMOD System::playSound (radio bus)", kPlaySound, reinterpret_cast<void*>(&play_sound), reinterpret_cast<void**>(&g_play_orig));
    if (!ok) return false;
    if (HANDLE t = CreateThread(nullptr, 0, player, nullptr, 0, nullptr)) CloseHandle(t);
    return true;
}

void live_register(LivePlayer* player) {
    std::lock_guard<std::mutex> lk(g_players_mx);
    g_players.push_back(player);
}

std::vector<LivePlayer*> live_players() {
    std::lock_guard<std::mutex> lk(g_players_mx);
    return g_players;
}

// A recording. It runs on the sound card's clock and the game reads on its own, so keep at most ~1 s banked
// (past that, drop: a tiny skip now and then instead of a growing delay).
void live_audio(const LivePlayer* from, const int16_t* samples, size_t frames) {
    if (g_live != from) return;
    if (g_wr.load() - g_rd.load(std::memory_order_acquire) > kTarget + 44100 / 2) return;
    if (samples) {
        ring_write(reinterpret_cast<const Frame*>(samples), frames);
        return;
    }
    static const Frame silence[1024] = {};
    while (frames) {
        size_t n = std::min<size_t>(frames, 1024);
        ring_write(silence, n);
        frames -= n;
    }
}

void radio_select_playlist(const std::wstring& id) {
    {
        std::lock_guard<std::mutex> lk(g_sel_mx);
        if (id == g_sel_id) return;
        g_sel_id = id;
    }
    g_sel_gen++;
    logf("playlist chosen: %s", id.empty() ? "(none: all music)" : narrow(id).c_str());
}

std::wstring radio_selected_playlist() {
    std::lock_guard<std::mutex> lk(g_sel_mx);
    return g_sel_id;
}

void radio_next() { g_skip = true; }
void radio_previous() { g_back = true; }
bool radio_on_air() { return g_selected; }

std::string players_status() {
    std::string out;
    for (LivePlayer* p : live_players())
        out += "; " + narrow(p->name()) + (p->available() ? (p->playing() ? " playing" : " open") : " not open");
    return out;
}

std::string radio_status() {
    std::lock_guard<std::mutex> lk(g_np_mx);
    return std::string("station ") + (g_selected ? "on" : "off") + (listening() ? ", game reading" : ", game not reading") +
           "; playlist \"" + narrow(g_playing_list) + "\"; song: " + narrow(g_playing_song) +
           players_status();
}
