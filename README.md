# Meketreve OBS Essentials

A toolkit of small OBS Studio filters/tools bundled in a single native plugin.
Built on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate),
so Windows / macOS / Linux binaries — a Windows installer and a Linux `.deb`
included — are produced by GitHub Actions. No local OBS dev dependencies
required, though building locally on Linux is a one-liner (see
[Install](#install)).

## Tools

| Tool | Type | What it does |
|------|------|--------------|
| **Bass Shake** | Video filter | Random camera/source shake driven by the bass energy of a chosen audio source (mic, desktop audio, …). |
| **Voice FX Mixer** | Audio filter | Voicemod-style voice changer: a toggleable chain of Pitch, Telephone, Distortion, Ring Mod, Bitcrusher, Tremolo and Echo. |
| **Unified Chat** | Dock | Twitch, YouTube, Kick and TikTok chat merged into one panel inside OBS. |
| **Share configuration** | Tools menu | Export tabs, chat channels and outputs as one line of text (`MOE1:...`) and import it elsewhere, or start from a built-in preset. |
| **Outputs** | Dock | Stream to more platforms at once (Twitch, YouTube, Kick, TikTok, any RTMP(S) or SRT server), reusing the main stream's encoder or with its own. |
| **Layout Tabs** | Toolbar | Tabs under the menu that switch the whole dock layout: **Live**, **Build** and your own. Needs OBS 32+. |

### Layout Tabs

A tab bar sits under the menu. Each tab remembers where every dock is:

- **My layout** is the layout you had before installing the plugin, untouched.
- **Live** shows the preview, Unified Chat, the audio mixer and the controls.
- **Build** shows the preview, scenes, sources, transitions and the mixer.
- **+** makes a new tab from the current layout. Right-click a tab to rename or
  remove it, or to bring Live/Build back to their default layout.

Layouts save on their own when you switch tabs and when OBS closes, and each
profile keeps its own tabs. The preview becomes a dock (**Preview**) so tabs
can place it anywhere. Hotkeys for *next tab*, *previous tab* and *go to tab
1–9* are in **Settings → Hotkeys**. To turn the tabs off, uncheck
**Tools → Meketreve: Layout tabs**: your original layout comes back, and after
a restart the preview is back in the center.

### Outputs (multistream)

Open **Docks → Outputs** and click **+** for each extra destination: pick the
platform (the server is filled in), paste the stream key and choose the
encoder:

- **Reuse the main stream's encoder** (default): no extra CPU/GPU cost; the
  output uses the main stream's resolution and bitrate and can only run while
  the main stream is live.
- **Own encoder**: any video encoder OBS offers (x264, NVENC, …) with its own
  bitrate, so it can run on its own or send a lower bitrate to one platform.

Outputs set to *start together with the main stream* start and stop with it;
**Start all** / **Stop all** and each row's button control them by hand. The
dot shows the state (hover it for errors) and a timer counts the time live.
A warning appears when the bitrate is above what the platform accepts. Stream
keys stay in the profile folder on this computer and are never exported.

Based on the idea of [Aitum Multistream](https://github.com/Aitum/obs-aitum-multistream) (GPL-2.0).

### Share configuration

**Tools → Meketreve: Export configuration** turns your setup into one line of
text that starts with `MOE1:`. Tick what goes in (tabs and layouts, Unified
Chat channels, extra outputs), copy it and paste it into **Tools → Meketreve:
Import configuration** on another computer, or send it to a friend. The import
dialog shows what the text contains and lets you pick what to apply; it also
lists the built-in presets (*Chat only*, *Simple live*, *Multistream*).

Stream keys, passwords and login tokens are never part of the text. Imported
tabs are added next to yours (your own *My layout* is never overwritten); Live
and Build are replaced.

The idea comes from [Aitum Stream Suite](https://github.com/Aitum/obs-aitum-stream-suite)
(GPL-2.0).

### Unified Chat

Open it from **Docks → Unified Chat**, click **Settings** and fill in the
channels you stream to. Each field takes a plain name or a link:

| Platform | Example | How it connects |
|----------|---------|-----------------|
| Twitch | `xqc` or `twitch.tv/xqc` | Anonymous read-only IRC over WebSocket. |
| YouTube | `@handle`, channel link or live/video link | The same InnerTube endpoint the popout chat uses, so no API key and no daily quota. |
| Kick | `westcol` or `kick.com/westcol` | Kick's public Pusher channel. If Kick's API is blocked, type the numeric chatroom id instead. |
| TikTok | `@user` or `tiktok.com/@user` | TikTok signs its chat WebSocket URL, so the signed URL comes from [Euler Stream](https://www.eulerstream.com) (the sign server TikTok-Live-Connector uses); after that the connection goes straight to TikTok. |

Leave a field empty to turn that platform off. A channel that is not live is
checked again every minute, so the chat connects on its own when the stream
starts, and dropped connections retry with backoff. Hover the colored dots at
the top to see each platform's status.

**Activity** (Docks → Activity) lists subs, gifted subs, raids, bits,
follows, Super Chats/stickers, memberships and TikTok gifts, follows and
shares from every platform; they are also highlighted in the chat (turn that
off in Settings). TikTok likes are off by default. Twitch follows need a login
(see below); Kick follows arrive on their own.

Without logging in the chat is read-only and nothing is sent to any platform.
YouTube and TikTok use unofficial endpoints, so a change on their side can
break those two until the plugin is updated.

### Voice FX Mixer

Add it as a filter on a microphone / audio input (Edit → Advanced Audio, or the
source's **Filters**). Each module is a checkbox section you can enable and chain:

- **Input/Output Gain (dB)** — level trim before/after the chain.
- **Pitch Shift** — ±12 semitones. Down = deep/monster, up = chipmunk.
- **Telephone / Radio** — 300–3400 Hz band-pass for a phone/radio voice.
- **Distortion** — tanh drive for a gritty/aggressive tone.
- **Ring Mod** — multiplies by a sine carrier → robot/alien (try 80–300 Hz).
- **Bitcrusher** — bit + sample-rate reduction for lo-fi/glitch.
- **Tremolo** — amplitude LFO (rate + depth).
- **Echo / Delay** — time, feedback, wet mix.

> Pitch is a real-time granular shifter (time-domain), so extreme settings add
> some artifacts — expected for live use without latency.

### Bass Shake

Add it as a filter on the source you want to shake:

1. Right-click the source → **Filters** → **+** → **Bass Shake (Audio Reactive)**.
2. **Audio Source (mic)** — pick the audio input that drives the shake.
3. Tune:
   - **Shake Intensity (px)** — max displacement at full level.
   - **Sensitivity** — how hard quiet/loud audio maps to movement.
   - **Noise Gate / Threshold** — ignore audio below this level (kills idle jitter).
   - **Smoothing (s)** — higher = softer, lower = snappier.
   - **Bass Cutoff (Hz)** — low-pass cutoff isolating the bass that drives the shake (~80–150 Hz for kick/bass).

> The shake displaces the rendered source; edges reveal transparency. Oversize
> the source slightly (or use a source bigger than the canvas) to hide the gaps.

## Install

### Windows

Download the installer (`.exe`) from the [latest release](https://github.com/meketreve/meketreve-obs-essentials/releases)
and run it. It drops the plugin into
`%ProgramData%\obs-studio\plugins\meketreve-obs-essentials\`, which OBS 30+
loads automatically. No admin rights needed.

### Linux

The plugin is portable C/C++ against `libobs`, `obs-frontend-api` and Qt 6 with
no platform-specific code, so it builds and runs natively on Linux.

**From the `.deb`** (Debian / Ubuntu / Mint), attached to each release:

```bash
sudo apt install ./meketreve-obs-essentials-1.0.0-x86_64-linux-gnu.deb
```

**From source**, which also installs into your user plugin directory:

```bash
sudo apt install libobs-dev qt6-base-dev cmake build-essential
./build-aux/install-linux.sh
```

Then restart OBS. The script installs to
`~/.config/obs-studio/plugins/meketreve-obs-essentials/`. Use `--flatpak` if you
run the Flatpak build of OBS, which reads plugins from
`~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/` instead:

```bash
./build-aux/install-linux.sh --flatpak
```

> The bundled CMake presets (`cmake --preset ubuntu-x86_64`) require **Ninja**.
> If you do not have it (`sudo apt install ninja-build`), the install script
> falls back to Unix Makefiles automatically.

Confirm it loaded by checking the OBS log:

```
[meketreve-obs-essentials] Meketreve OBS Essentials loaded (version 1.0.0)
```

### macOS

CI produces a `.pkg`; it is built and uploaded but not regularly tested.

## Language

The UI ships with **English (en-US)** and **Portuguese (pt-BR)** strings, and
follows the language configured in OBS. Other languages fall back to English.

## Build / Release (CI)

Everything builds in GitHub Actions:

- **Any push** to `main` → builds all platforms and uploads artifacts:
  Windows `.zip` + installer `.exe`, Linux `.deb` / `.ddeb` / `.tar.xz`, macOS `.pkg`.
- **Tag** like `1.0.0` → also creates a draft GitHub Release with those attached.

```bash
git tag 1.0.0
git push origin 1.0.0
```

## Adding a new tool

1. Create `src/tools/<tool>.c` + `.h` exposing `void <tool>_register(void);`
   (C++/Qt tools: put the sources in `src/tools/<tool>/` and expose the
   register function through an `extern "C"` header, like `unified-chat.h`).
2. Add the sources to `target_sources(...)` in `CMakeLists.txt`.
3. Call `<tool>_register();` in `obs_module_load()` (`src/plugin-main.c`).
4. Add any shader to `data/effects/` and locale strings to **both**
   `data/locale/en-US.ini` and `data/locale/pt-BR.ini` (keep the keys in sync).

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
