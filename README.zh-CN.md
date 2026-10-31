# XboxMusicRadio

[English](README.md) | 简体中文

一个《极限竞速：地平线 2》（Azimuth）模组：打开游戏电台里被砍掉的 **Xbox Music** 电台，在上面播放你自己的音乐。

在 Xbox One 上，FH2 可以在车里播放你的 Xbox Music 歌单。这个服务多年前就停了，PC 上这个电台也从来没开放过。本模组
把它解锁，改为播放你的音乐，在游戏的 Audio Options（音频选项）里选择，就像当年选 Xbox Music 歌单一样：

- **你的音乐文件**：电脑上的一个文件夹，里面每个文件夹就是一个歌单（MP3、FLAC、WAV）。
- **Spotify**（需要 Premium），通过 Spotify 官方的 Spotify Soloist 播放器。
- **YouTube Music**、**网易云音乐**和 **QQ 音乐**，在你的电脑上播放。

## 安装

把发布的 zip 解压到 Azimuth 文件夹（`azimuth.exe` 所在的文件夹）：

```
DWrite.dll                       FH2 Mod Platform（模组平台）
fh2-mods\XboxMusicRadio.dll      本模组
LICENSE*.txt                     许可证
```

先启动一次游戏。它会在 `azimuth.exe` 旁边建一个 `XboxMusicRadio` 文件夹，里面有 `XboxMusicRadio.ini`。

需要 Azimuth 1.0.1 或更新版本（它以 `DWrite.dll` 的形式加载模组平台）。Spotify 和各音乐软件需要 Windows 10 2004 或更新
版本（模组要录下它们的声音）。

## 你的音乐文件

电台播放的是你的 Windows **音乐**文件夹（`C:\Users\<你的用户名>\Music`，或者你把它移到的位置）里的音乐：

```
Music\
    Night Drive\          一个叫 "Night Drive" 的歌单
        01 - Song.mp3
        Artist - Song.flac
    Rock\                 另一个歌单（包括子文件夹）
        Album 1\...
    loose.mp3             直接放在 Music\ 里的文件组成一个叫 "Music" 的歌单
```

- 格式：MP3、FLAC 和 WAV。
- 有多个歌单时，还会多一个 **All music**（全部音乐），包含所有歌曲。没选歌单时就播放它。
- 歌名和歌手取自文件标签；没有标签时取自文件名：`歌手 - 歌名.mp3`。
- 想用别的文件夹，在 `XboxMusicRadio.ini` 里设置 `folder=`，例如 `folder=D:\Songs` 或
  `folder=%USERPROFILE%\Desktop\Songs`。
- 新加的文件和文件夹，下次打开 Audio Options 时就会出现，不用重启游戏。

## 播放

1. 在车里，把电台切到 **Xbox Music**。
2. 选择播放什么：暂停 > 设置 > Audio Options，在 Xbox Music 下选好，然后离开这个页面。Spotify 和各音乐软件排在列表
   最前面，后面是你的文件夹。
3. **F6** 切到下一首。可以在 Mods 页面改按键，或者加一个手柄组合键。

游戏自带的 **Horizon 2 Mix**（Xbox One 上默认的 Xbox Music 歌单）会播放你所有的音乐文件。

电台显示屏会显示歌名和歌手，但只能显示基本的拉丁字母：重音符号会被去掉，中文、日文等文字在那里显示不出来；完整的
名字可以在日志里看到。

## Spotify

有 Spotify Premium 的话，电台可以通过 **Spotify Soloist**（Spotify 官方的无界面 Spotify Connect 播放器）播放 Spotify：

1. 在 [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard) 打开 **Spotify Soloist API Key**，
   生成一个密钥。把它填在 `XboxMusicRadio.ini` 的 `[spotify]` 下的 `api_key=` 后面。请不要泄露这个密钥。
2. 从 [Downloads and updates](https://developer.spotify.com/documentation/soloist/reference/downloads-and-updates)
   下载 Windows 版 Soloist，把 `soloist.exe` 放进 `XboxMusicRadio` 文件夹。Spotify 不允许转发它，所以模组的 zip 里
   没有。Soloist 每个版本 90 天后过期；到时候日志会提醒你换新的。
3. 启动游戏。暂停 > 设置 > Audio Options：在 Xbox Music 下选 **Spotify**。
4. 在 Spotify 应用里（手机或电脑，同一个网络），选择设备 **FH2 Radio**，随便播放点什么。

游戏运行时，模组在后台隐藏运行 Soloist，把它的声音放到电台上。歌单和歌曲在 Spotify 应用里选。Soloist 本身的声音会在
Windows 音量合成器里调低到 -80 dB，所以你只会在电台里听到。**F6** 切歌；切到别的电台时 Spotify 会暂停，切回来时
继续播放。Soloist 的登录信息、缓存和日志（`soloist.log`）都在 `XboxMusicRadio\soloist` 里。

## 音乐软件：YouTube Music、网易云音乐、QQ 音乐

电台还可以播放你电脑上音乐软件正在放的内容。每个软件都是 Audio Options 里的一个歌单：

| 歌单 | 对应软件 |
|---|---|
| **YouTube Music** | 浏览器里的 [music.youtube.com](https://music.youtube.com)（Edge、Chrome、Brave、Vivaldi、Opera、Firefox），或从浏览器安装成应用 |
| **NetEase Cloud Music** | 网易云音乐电脑版（`cloudmusic.exe`） |
| **QQ Music** | QQ 音乐电脑版（`QQMusic.exe`） |

1. 打开软件，播放点什么。
2. 暂停 > 设置 > Audio Options：在 Xbox Music 下选它。网易云音乐和 QQ 音乐在已安装或正在运行时才会出现在列表里。

模组会录下软件的声音放到电台上。电台播放它的时候，软件本身的声音会在 Windows 音量合成器里调低到 -80 dB，所以你只会
在电台里听到；选别的歌单或关掉游戏时会恢复原样。**F6** 切歌；切到别的电台时软件会暂停，切回来时继续播放。

- **YouTube Music** 录的是整个浏览器，所以浏览器里放的所有声音都会上电台：别的标签页请保持安静，或者专门用一个浏览器
  放它（在 `[youtube]` 下设置 `app=chrome.exe` 来指定）。
- **YouTube Music** 和 **QQ 音乐**支持 Windows 的媒体控制（就是媒体键用的那套），歌曲信息和控制都走它。
- **网易云音乐**不支持，所以模组从它的窗口标题读歌名，并用它的全局快捷键来控制。请在网易云音乐的设置里保持全局快捷键
  开启。如果你改过快捷键，请在 `[netease]` 下把 `play_pause_key=` 和 `next_key=` 设成一样的，例如
  `next_key=Ctrl+Alt+Right`。

## 设置

暂停 > Settings > MODS 里有 **Xbox Music Radio**（开/关）、**Song names from tags**（从标签读歌名）和
**Shuffle songs**（随机播放），还有切歌按键。开/关和这两个音乐开关重启后生效。

所有设置也都在 `XboxMusicRadio\XboxMusicRadio.ini` 里（下次启动游戏时生效）：

| 分节 | 内容 |
|---|---|
| `[radio]` | `enabled`：模组开或关 |
| `[music]` | `folder`、`read_tags`、`shuffle`，以及切歌按键（`next_key`、`next_pad`） |
| `[spotify]` | `enabled`、`api_key`、`device`（在 Spotify 应用里显示的设备名）、`volume`、`quiet_speakers` |
| `[youtube]`、`[netease]`、`[qqmusic]` | `enabled`、`app`（程序名，如果不是默认的那个）、`quiet_speakers`；网易云音乐和 QQ 音乐还有快捷键 `play_pause_key`、`next_key` |
| `[debug]` | `trace=1`：记录每个 Xbox Music 请求、游戏的电台列表，以及 Windows 列出的媒体会话 |

`quiet_speakers=0` 表示不动播放器本身的声音（这样你会听到两遍：电台里一遍，播放器本身一遍）。

配合模组平台的 `fh2mod.exe`：`fh2mod XboxMusicRadio status`、`next` 和 `rescan`。

## 常见问题

模组的日志写在 `fh2-mods\platform.log`（以 `[XboxMusicRadio]` 开头的行）：音乐扫描结果、电台开关、选了什么、播放的
每首歌。

- **电台里没有 Xbox Music。** 日志里应该有 `Xbox Music hooks on`。如果显示 hook 失败，说明有别的模组改了同一处游戏
  代码；日志里会写是哪个。
- **电台没有声音。** 看日志里有没有 `playing ...` 的行。Spotify 和音乐软件要确认播放器确实在放；Spotify 还要确认
  Spotify 应用是在 **FH2 Radio** 上播放。
- **Spotify 不能用了。** 日志里有 `Soloist: this soloist.exe has expired`：下载新版本。
- **关掉游戏后某个软件没声音。** 游戏关闭时模组会把它的声音恢复（有个小助手程序会等游戏结束），否则下次启动游戏时恢复。
  手动修复：Windows 设置 > 系统 > 声音 > 音量合成器，把这个软件的音量调回来。

## 致谢与许可证

电台本身（解锁电台、建立 FMOD 音频流、接入电台总线、电台显示屏和解码器）移植自 **FH2_SpotifyMin**，后者基于
**Bullet4fun 的 spotify-radio** 模组（FH2_ModsCollection）。两者都是 GPL-3.0，所以本模组也是：GPL-3.0-or-later，
见 [LICENSE](LICENSE)。

解码器：[dr_mp3、dr_flac 和 dr_wav](https://github.com/mackron/dr_libs)，作者 David Reid（公有领域 / MIT-0）。
FH2 Mod Platform 采用 MIT 许可证，内置 MinHook（BSD 2-Clause）。

Spotify Soloist 属于 Spotify，从 Spotify 下载，作为独立程序运行；它不是本模组的一部分。Spotify、YouTube Music、
网易云音乐和 QQ 音乐是其各自所有者的商标；本模组与它们没有关联。

## 构建

使用 llvm-mingw（或 Visual Studio）、CMake 和 Ninja：

```
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target xboxmusicradio
```

在 Linux 上，用 llvm-mingw 和模组平台的工具链文件交叉编译：
`-DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64-x86_64.cmake`。

GitHub Actions（[.github/workflows/build.yml](.github/workflows/build.yml)）会构建发布用的 zip：手动运行
（Actions > Build > Run workflow）可以直接下载；或者发布一个 release（标签为 `v` + `CMakeLists.txt` 里的 `VERSION`），
zip 会自动附加到这个 release 上。

工作原理，包括游戏地址和回复数据结构：[docs/re-notes.md](docs/re-notes.md)（英文）。
