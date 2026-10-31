# XboxMusicRadio

English | [简体中文](README.zh-CN.md)

A Forza Horizon 2 (Azimuth) mod that turns on the radio's cut **Xbox Music** station and plays your own music on it.

On Xbox One, FH2 could play your Xbox Music playlists in the car. That service shut down years ago, and on PC the
station was never available. This mod unlocks it and plays your music instead, chosen in the game's Audio Options as
you would have chosen an Xbox Music playlist:

- **Your music files**: a folder on your PC, each folder in it a playlist (MP3, FLAC, WAV).
- **Spotify** (Premium), through Spotify's own Spotify Soloist player.
- **YouTube Music**, **NetEase Cloud Music** and **QQ Music**, playing on your PC.

## Install

Extract the release zip into the Azimuth folder (next to `azimuth.exe`):

```
DWrite.dll                       the FH2 Mod Platform
fh2-mods\XboxMusicRadio.dll      the mod
LICENSE*.txt                     licenses
```

Start the game once. It makes an `XboxMusicRadio` folder next to `azimuth.exe` with `XboxMusicRadio.ini`.

Needs Azimuth 1.0.1 or later (it loads the platform as `DWrite.dll`). Spotify and the music apps need Windows 10
version 2004 or later (the mod records their sound).

## Your music files

The station plays the music in your Windows **Music** folder (`C:\Users\<you>\Music`, or wherever you moved it):

```
Music\
    Night Drive\          a playlist called "Night Drive"
        01 - Song.mp3
        Artist - Song.flac
    Rock\                 another playlist (subfolders included)
        Album 1\...
    loose.mp3             files right in Music\ make a playlist called "Music"
```

- Formats: MP3, FLAC and WAV.
- With more than one playlist there's also **All music**, every song together. It's what plays until you choose one.
- Titles and artists come from the files' tags. Without tags, from the file name: `Artist - Title.mp3`.
- To use another folder, set `folder=` in `XboxMusicRadio.ini`, for example `folder=D:\Songs` or
  `folder=%USERPROFILE%\Desktop\Songs`.
- New files and folders show up the next time you open Audio Options; no restart needed.

## Playing

1. In the car, switch the radio to **Xbox Music**.
2. To choose what it plays: Pause > Settings > Audio Options, pick it under Xbox Music, and leave the page. Spotify and the
   music apps come first in the list, then your folders.
3. **F6** skips to the next song. You can change the key, or add a controller chord, on the Mods page.

The game's own **Horizon 2 Mix** entry (the default Xbox Music playlist on Xbox One) plays all your music files.

The radio display shows the song and artist. It can only show plain letters: accents are dropped, and other scripts
(Chinese, Japanese, ...) don't show there; the log has the full names.

## Spotify

With Spotify Premium, the station can play Spotify through **Spotify Soloist**, Spotify's own headless Spotify
Connect player:

1. At [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard), open **Spotify Soloist API Key**
   and make a key. Put it after `api_key=` under `[spotify]` in `XboxMusicRadio.ini`. Keep it private.
2. Download Soloist for Windows from
   [Downloads and updates](https://developer.spotify.com/documentation/soloist/reference/downloads-and-updates)
   and put `soloist.exe` in the `XboxMusicRadio` folder. Spotify doesn't allow passing it on, so it isn't in the
   mod's zip. Soloist builds expire after 90 days; the log says when it's time for a new one.
3. Start the game. Pause > Settings > Audio Options: choose **Spotify** under Xbox Music.
4. In the Spotify app (phone or PC, same network), pick the device **FH2 Radio** and play anything.

The mod runs Soloist hidden while the game runs and plays its sound on the station. Choose playlists and songs in the
Spotify app. Soloist's own sound is turned down to -80 dB in the Windows volume mixer, so you only hear it on the
radio. **F6** skips, and Spotify pauses while you're on another station and carries on when you come back.
Soloist's login, cache and log (`soloist.log`) are in `XboxMusicRadio\soloist`.

## Music apps: YouTube Music, NetEase Cloud Music, QQ Music

The station can also play what a music app on your PC plays. Each one is a playlist in Audio Options:

| Playlist | The app |
|---|---|
| **YouTube Music** | [music.youtube.com](https://music.youtube.com) in a browser (Edge, Chrome, Brave, Vivaldi, Opera, Firefox), or installed from the browser as an app |
| **NetEase Cloud Music** | the NetEase Cloud Music desktop app (`cloudmusic.exe`) |
| **QQ Music** | the QQ Music desktop app (`QQMusic.exe`) |

1. Start the app and play something.
2. Pause > Settings > Audio Options: choose it under Xbox Music. NetEase Cloud Music and QQ Music are listed once
   they're installed or running.

The mod records the app's sound for the station. While the radio plays it, the app's own sound is turned down to
-80 dB in the Windows volume mixer, so you only hear it on the radio. It's put back as it was when you choose another
playlist or close the game. **F6** skips, and the app pauses while you're on another station and carries on when you
come back.

- **YouTube Music** records the whole browser, so everything it plays is on the radio: keep other tabs quiet, or use a
  browser just for it (`app=chrome.exe` under `[youtube]` picks one).
- **YouTube Music** and **QQ Music** work with Windows' media controls (the ones the media keys use), which give the
  song and take the commands.
- **NetEase Cloud Music** doesn't, so the mod reads the song from its window title and drives it with its global
  shortcuts. Keep those on in NetEase's settings. If you changed them, set `play_pause_key=` and `next_key=` under
  `[netease]` to match, like `next_key=Ctrl+Alt+Right`.

## Settings

Pause > Settings > MODS has **Xbox Music Radio** (on/off), **Song names from tags** and **Shuffle songs**, plus the
next-song key. The on/off and the two music switches apply after a restart.

Everything is also in `XboxMusicRadio\XboxMusicRadio.ini` (changes apply the next time the game starts):

| Section | What's in it |
|---|---|
| `[radio]` | `enabled`: the mod on or off |
| `[music]` | `folder`, `read_tags`, `shuffle`, and the next-song key (`next_key`, `next_pad`) |
| `[spotify]` | `enabled`, `api_key`, `device` (the name in the Spotify app), `volume`, `quiet_speakers` |
| `[youtube]`, `[netease]`, `[qqmusic]` | `enabled`, `app` (the program, if not the usual one), `quiet_speakers`, and for NetEase and QQ Music the shortcuts `play_pause_key`, `next_key` |
| `[debug]` | `trace=1`: log every Xbox Music request, the radio's stations and the media sessions Windows lists |

`quiet_speakers=0` leaves a player's own sound alone (you'll hear it twice: on the radio and from the player).

With the platform's `fh2mod.exe`: `fh2mod XboxMusicRadio status`, `next` and `rescan`.

## Troubleshooting

The mod logs to `fh2-mods\platform.log` (lines starting with `[XboxMusicRadio]`): the music scan, the station turning
on and off, what's chosen and each song it plays.

- **Xbox Music isn't among the radio stations.** The log should say `Xbox Music hooks on`. If it says the hooks failed,
  another mod changed the same game code; the log names it.
- **Silence on the station.** Check the log for `playing ...` lines. For Spotify and the apps, make sure the player is
  actually playing; for Spotify, that the Spotify app plays on **FH2 Radio**.
- **Spotify stopped working.** `Soloist: this soloist.exe has expired` in the log: download the new one.
- **An app stays silent after the game.** The mod puts its sound back when the game closes (a small helper waits for
  the game to end), or else the next time the game starts. To fix it by hand: Windows Settings > System > Sound >
  Volume mixer, and turn the app back up.

## Credits and license

The station itself (unlocking it, building its FMOD stream, routing it through the radio bus, the radio display and
the decoders) is ported from **FH2_SpotifyMin**, which builds on **Bullet4fun's spotify-radio** mod
(FH2_ModsCollection). Both are GPL-3.0, so this mod is too: GPL-3.0-or-later, see [LICENSE](LICENSE).

Decoders: [dr_mp3, dr_flac and dr_wav](https://github.com/mackron/dr_libs) by David Reid (public domain / MIT-0).
The FH2 Mod Platform is MIT-licensed, with MinHook (BSD 2-Clause) built in.

Spotify Soloist is Spotify's, downloaded from Spotify and run as a separate program; it isn't part of this mod.
Spotify, YouTube Music, NetEase Cloud Music and QQ Music are trademarks of their owners; this mod isn't affiliated with
them.

## Building

llvm-mingw (or Visual Studio) with CMake and Ninja:

```
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target xboxmusicradio
```

From Linux, cross-compile with llvm-mingw and the platform's toolchain file:
`-DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64-x86_64.cmake`.

GitHub Actions ([.github/workflows/build.yml](.github/workflows/build.yml)) builds the release zip: run it by hand
(Actions > Build > Run workflow) for a download, or publish a release (tag `v` + the `VERSION` in `CMakeLists.txt`)
and the zip is attached to it.

How it works, with the game addresses and reply layouts: [docs/re-notes.md](docs/re-notes.md).
