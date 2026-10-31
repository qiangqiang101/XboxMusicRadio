# Reverse-engineering notes

Build: `forza_durango_release_final.exe` (24 Nov 2015). Addresses are RVAs (`game+0x...`); add `0x140000000` for VAs.

## Availability

- `game+0xFDDF90` `bool XboxMusicAvailable(manager)`: true when a global service object exists and isn't blocked
  (`[obj->vt[0x2E0]()]+0x35 == 0`), and either a debug override (`vt+0xC0` of another global) or
  `game+0xFE4130(manager+0x6670)` (sign-in state > 1, `+0x79` set, dev flag `xboxmusicforceinvalidregion` off) **and**
  `manager+0x6790` (the server's `enableXboxMusic`).
- The manager pointer is the global at `game+0x365AE08`.
- `manager+0x6790` is written by `game+0x55D810` (from the saved `ForzaEconomyState.XboxMusicEnabled`, `+0x2D`) and
  `game+0x55E1A0` (the `GetEconomySettings` reply, byte `+0x41`).
- Callers: `0xC68D18` (`IsXboxMusicEnabled` menu binding, `+0x431`), `0xE2A41D`, `0x89B2C5` (UI choices), `0xFDE44C`
  (manager update `0xFDE410`), `0x7836D5` (event `0x22`).
- Dev config keys (parsed at `0xF6DD60`): `xboxmusicenabled` (+0x27B0), `xboxmusicenabledduringonline` (+0x27B1),
  `xboxmusicrevokesubscription` (+0x27B2), `xboxmusicforceinvalidregion` (+0x27B3).

## Choosing a playlist

- In game: the radio station "Xbox Music" (`MyProfile.str` `IDS_RadioStation8_Name`); the playlist is chosen in the
  profile's Audio Options (`IDS_XboxMusicDescription`; the default is "Horizon 2 Mix", `IDS_DefaultPlaylist_Name`).
- `game+0xFDE220` `set_playlist(manager, std::wstring* id)` makes a new `XboxMusic::Playlist` (0x198 bytes, id at
  +0x98) at `manager+0x6660` when the id differs, and frees the string it was given. Called with the saved
  `XboxMusicSettings.DefaultPlaylistId` at startup (`0xE9308C`) and when leaving Audio Options (`AudioOptions`
  vtable slot 180, `0xC7DA50`, if `IsXboxMusicEnabled`).
- The manager update `0xFDE410` only runs while `manager+0x6668` is set (the player's event handler `0xFDE0B0`,
  event `0x22` code 0 = start, 1 = stop) and only loads tracks when the playlist's id isn't empty. The mod doesn't use
  the game's player: it hooks `set_playlist` to learn the choice and plays it on the station itself.

## The station

- `RadioSystem` (vtable `0x2E76818`): `SetStation(name)` (`0x1021450`, slot 8) starts Xbox Music (`0x1022B60`: sets
  `+0x85`, sends command 0 through `0x1018420`) only for the name `"Xbox Music"` and when the gate, vtable slot
  `+0xC0` (`0x1012090`), says yes. The gate reads `AudioEngineFmod+0x548` (`AudioEngineFmod` at `0x365D3D8`,
  `vt+0x2E0` returns `this+0x4D0`, byte `+0x78`), which nothing sets on PC. The same object's `+0x35` must be 0 for
  `XboxMusicAvailable`.
- The rest comes from FH2_SpotifyMin / Bullet4fun's spotify-radio: the station lock `0xA65DF0`, the command
  `0x1018420` (the stock game only has a stub stream; the real one comes from the factory `0xFEEEC0` with the engine's
  `+0x7F0`, vtable `0x2E76190`, its Studio event at `+0x30`), the stream's PCM read callback `0x1014960`, the radio HUD
  `0x7835B0`, the mixer snapshot `0x1024570`, FMOD `Channel::setPosition` `0x19ACEC0` and `System::playSound`
  `0x199B0F0`. Addresses and how each is used: `src/radio.cpp`.
- The station list the player cycles through is built **once, early while the game loads**, asking the gate. If the
  gate isn't hooked by then, "Xbox Music" is missing from the radio for the whole session. So the gate and the lock are
  hooked and switched on first, right when `wait_for_code` returns; the other hooks take a second or two more while
  other mods make theirs.
  - `0x1028540` builds the RadioSystem's station list (asks the gate, appends "Xbox Music" as station 8) and posts it.
  - The radio UI cycles stations in `0x660E50(ui, direction)`: a `std::map` at `ui+0x68` (node: left `+0`, parent `+8`,
    right `+0x10`, nil flag `+0x19`, name `std::string` at `+0x28`), skipping those `0x668810(ui, name)` turns down
    (the lock `0xA65DF0(ui+0x60, name)`, and not in the list at `ui+0xC0..0xC8`). `[debug] trace=1` logs the map.

## Objects

Every XboxMusic object has a vtable at +0 with `[0]` success and `[1]` failure callbacks, a retry timer at +8
(`vt[0]` = `0xFE2550`, true once the task is gone and 10 s passed), and a task tracker (`{owner, ctx, handle@+0x10}`).

| Object | vtable | success | failure | state | tracker |
|---|---|---|---|---|---|
| XboxMusicAuthentication (manager+0x6670) | `0x2E70E08` | `0xFE49A0` | `0xFE4440` | +0x118 | +0x88 |
| SongStream | `0x2E70DA0` | `0xFE48F0` | `0xFE4390` | +0x118 | +0x120 |
| Playlist | `0x2E70DC8` | `0xFE4480` | `0xFE42B0` | +0xD8 | +0xE0 |
| PlaylistBrowser | `0x2E70DF0` | `0xFE4770` | `0xFE4380` | +0xB8 | +0x28 |

- The sign-in starts when the game reads `XboxMusicSettings` (`0xE92BB0` → `0xFDDEB0` → update `0xFE5090`).
  Success copies `HasSubscription` to +0x78 and `IsSubscriptionAvailableForPurchase` to +0x79, state 2.
  Available but not subscribed raises event 5 and retries.
- SongStream update `0xFE5230`: builds `clientInstanceId` from
  `Windows.Networking.Connectivity.NetworkInformation` (`0xFE2A30`, throws on failure), then `GetSongURL(id@+0x78,
  client@+0xB8, result@+0x1B0, task@+0x130)`. Success copies the URL to the std::wstring at +0x98.
- Playback: `0x1000CF0` passes the URL straight to `MFCreateSourceReaderFromURL` and asks for PCM 44.1 kHz 16-bit
  stereo. `ContentType` isn't used.

## Calls

Each is a wrapper around the generic web call `0xEDA580` (service `Forza.WebServices.XboxMusic`). They return an
HRESULT and write a task handle.

| Call | Wrapper | Args | REST path |
|---|---|---|---|
| GetUserPermissions | `0x1990D30` | result, task | `/1/user/music/profile` |
| GetPlaylists | `0x19906D0` | result, task | `/1/content/music/collection/playlists/browse` |
| ContinueGetPlaylists | `0x1990030` | token, result, task | `...browse?continuationToken=%ws` |
| GetPlaylistTracks | `0x1990380` | playlistId, result, task | `/1/content/%ws/lookup?source=collection` |
| ContinueGetPlaylistTracks | `0x198FCD0` | playlistId, token, result, task | `/1/content/%ws/lookup?continuationToken=%ws` |
| GetSongURL | `0x19909D0` | songId, clientInstanceId, result, task | `/1/content/%ws/stream?clientInstanceId=%ws` |

## Task handles

A handle is an object with a vtable: `[0]` AddRef(tag), `[1]` Release(tag), `[2]` status, `[4]` Close(tag)
(`0xEDB5C0` status, `0xEDB550` close + release). The poller `0xF05250` reads each tracker's status every frame: 2 and 3
mean running; anything else completes the task (`0xF05050`): the owner's success callback for a non-negative value,
the failure callback otherwise, then close + release. `0xF05000(tracker, hr)` calls the failure callback directly.

## Reply layouts

From the reflection rows (slots `0x36EAE68`..`0x36EAEB8`). Strings are fixed `WCHAR[n]`.

- GetUserPermissions (2): `HasSubscription` u8 @0, `IsSubscriptionAvailableForPurchase` u8 @1.
- GetSongURL (0x103E): `Url` WCHAR[0x801] @0, `ContentType` WCHAR[0x1E] @0x1002.
- GetPlaylists (0x659C): count u32 @0; 25 × PlaylistItem (0x3FC) @4: `TrackCount` u32 @0, `Id` WCHAR[0xFB] @4,
  `Name` WCHAR[0x101] @0x1FA; `TotalItemCount` u32 @0x63A0; token present u8 @0x63A4, `ContinuationToken`
  WCHAR[0xFB] @0x63A6. The browser skips playlists with `TrackCount` 0.
- GetPlaylistTracks (0x297C0): count u32 @0 (1 is used); TrackCollection @4: `TrackCount` u32 @+0, tracks present
  u8 @+4, Tracks @+8: count u32, 25 × SongItem (0x1A78) @+4, token present u8 @+0x295BC, token @+0x295BE.
  SongItem: `Id` WCHAR[0xFB] @0, `Name` WCHAR[0x201] @0x1F6, artist count u32 @0x5F8, 5 × ArtistItem (0x418) @0x5FC
  (`Name` WCHAR[0x201] @0, `Role` WCHAR[0xB] @0x402), `TrackNumber` u32 @0x1A74.
