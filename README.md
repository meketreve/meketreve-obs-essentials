# Meketreve OBS Essentials

A toolkit of small OBS Studio filters/tools bundled in a single native plugin.
Site: <https://meketreve.github.io/meketreve-obs-essentials/>
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
| **Texuguito** | Dock + overlay | Chat bot with a pixel-art parade of the viewers, channel points, soundboard, TTS, raffles and chat-made commands, for every Unified Chat platform. |
| **Alerts** | Dock + overlay + web editor | Follows, subs, gift subs, bits, Super Chats, raids and TikTok/Kick gifts from every chat as on-screen alerts, set up in an editor that opens in your browser. |
| **Outputs** | Dock | Stream to more platforms at once (Twitch, YouTube, Kick, TikTok, any RTMP(S) or SRT server), reusing the main stream's encoder or with its own. |
| **Vertical canvas** | Docks | A second 9:16 canvas with its own scenes, sources, preview, recording, backtrack and streaming. Port of Aitum Vertical Canvas. Needs OBS 32+. |
| **Layout Tabs** | Toolbar | Tabs under the menu that switch the whole dock layout: **Live**, **Build** and your own. Needs OBS 32+. |

### Layout Tabs

A tab bar sits under the menu. Each tab remembers where every dock is:

- **Live** shows the preview, Unified Chat with Texuguito and Alerts under it, the audio mixer and the controls.
- **Build** shows the preview, scenes, sources, transitions and the mixer.
- **+** makes a new tab from the current layout. Right-click a tab to rename or
  remove it, or to bring Live/Build back to their default layout.

OBS opens on **Live** the first time. The *My layout* tab that older versions
created from your layout before the plugin can be removed like any other tab.

Layouts save on their own when you switch tabs and when OBS closes, and each
profile keeps its own tabs. The preview becomes a dock (**Preview**) so tabs
can place it anywhere. Hotkeys for *next tab*, *previous tab* and *go to tab
1–9* are in **Settings → Hotkeys**. To turn the tabs off, uncheck
**Tools → Meketreve: Layout tabs**: your original layout comes back, and after
a restart the preview is back in the center.

### Texuguito (chat bot and parade overlay)

The [texuguito-seu-bot-amigo](https://github.com/meketreve/texuguito-seu-bot-amigo)
bot now runs inside OBS: no Python, no separate window. Open **Docks →
Texuguito** and click **Add overlay to scene** (or copy the URL
`http://localhost:8901/overlay` into a Browser Source).

- **Parade:** everyone chatting walks along the bottom of the stream with an
  LPC pixel-art avatar they customize with `!cor`, `!chapeu`, `!acessorio`,
  `!apelido` and `!dança`. Bits, Super Chats and TikTok gifts make them cheer.
- **Points:** a point per minute in chat, spent on `!tocar <clip>` (clips in
  the audio folder, in subfolders named after their price, e.g.
  `audios/50/horn.mp3`) and `!falar <text>` (Google TTS, 200 points). Each
  price has its own wait between sounds, set in the dock's **Settings**
  (defaults: up to 20 pts 10 s, up to 100 pts 30 s, up to 200 pts 60 s,
  above that 120 s); a cheap sound can play right after an expensive one.
- **More:** `!pontos`, `!audios`, `!parar`, `!sorteio <points> <minutes>` /
  `!entrar`, `!comando add|edit|del <name> <reply>` for mods, `!comandos` for
  the link to the [command list](https://meketreve.github.io/meketreve-obs-essentials/commands.html)
  (`!comandos` opens it in Portuguese, `!commands` in English). Every command
  also has an English alias (`!color`, `!play`…).
- **Language:** the bot answers in the language OBS is set to (Portuguese or
  English; any other language gets English). The voice goes by the command:
  `!falar` speaks Portuguese, `!speak` English and `!tts` the language of OBS.

It reads every platform set up in Unified Chat. Replies go back to the
platform the command came from when you are logged in there (Twitch, YouTube
and Kick, see [Chat login](#chat-login)); TikTok viewers can use the
commands, but the bot cannot answer them. With a Twitch login as the
broadcaster or a moderator, quiet viewers show up too (Twitch viewer list);
elsewhere a viewer stays in the parade for 10 minutes after their last
message. Coming from the Python bot? **Import old bot** copies its `data/`
and `audios/` folders. Clips and TTS play through the Browser Source, so they
show up in the OBS mixer.

### Alerts

Open **Docks → Alerts**. **Add to scene** creates a Browser Source the size of
your canvas (`http://localhost:8902/alertas`); **Customize** opens the editor in
your web browser. It runs on your own computer, and changes are saved and reach
OBS as you make them.

- **Events:** follow, sub, resub, gift sub (a gift of many shows once), bits,
  Super Chat / Super Sticker, raid / host, YouTube member, TikTok and Kick
  gifts, TikTok shares and likes (the last two are off by default). Each one
  has its own on/off switch and a minimum (bits, months, viewers, amount paid,
  TikTok diamonds…).
- **Look:** text with `{name}` `{amount}` `{message}` `{detail}` `{platform}`
  (or `{nome}` `{quantidade}` `{mensagem}` `{detalhe}` `{plataforma}`), any
  Google Fonts font, colors, size, layout, entrance and exit animations, time
  on screen and position on screen. The viewer's message can be read aloud
  (Google TTS).
- **Images and sounds:** a default kit of animated drawings and synthesized
  sounds that works offline, your own files (GIF, PNG, JPG, WebP, WebM video,
  MP3, WAV, OGG; kept in the plugin's config folder), a link, or a **GIPHY /
  Tenor** search (paste your own free API key in the editor; it stays on your
  computer).
- Alerts wait in line and show one at a time. The dock can **Test** any type
  and **Skip** the one on screen. Alert sounds play through the Browser Source,
  so they show up in the OBS mixer.
- The editor's API only answers the link the dock opens (a token after `#`),
  so other websites open in your browser cannot change your alerts.
- **Share configuration** takes the alert setup along (without API keys; your
  own files have to be copied by hand).

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

### Vertical canvas

The toolkit includes a port of [Aitum Vertical Canvas](https://github.com/Aitum/obs-vertical-canvas)
(GPL-2.0): a vertical canvas with its own scene list (**Vertical Scenes**),
sources, transitions and a preview dock with stream, record, backtrack and
virtual camera buttons. Its settings (gear button in the Vertical dock) cover
resolution, streaming servers and recording. In the **Outputs** dock you can
also pick the vertical canvas as the source of an extra output (it always uses
its own encoder).

The canvas is called **Meketreve Vertical** (pick it as the extra canvas for
multitrack video in OBS, or in the Outputs dock). Its obs-websocket vendor is
`meketreve-vertical-canvas` and its procedures are `meketreve_vertical_*`, so
tools written for Aitum's plugin (Aitum Multistream, vendor requests to
`aitum-vertical-canvas`) do not drive this copy.

If Aitum's own Vertical Canvas (or Aitum Stream Suite) is installed, the
toolkit's copy stays off and Aitum's keeps working as before. When you remove
Aitum's plugin, the copy takes over its settings, scenes and dock positions
on the next start.

### Share configuration

**Tools → Meketreve: Export configuration** turns your setup into one line of
text that starts with `MOE1:`. Tick what goes in (tabs and layouts, Unified
Chat channels, extra outputs), copy it and paste it into **Tools → Meketreve:
Import configuration** on another computer, or send it to a friend. The import
dialog shows what the text contains and lets you pick what to apply; it also
lists the built-in presets (*Chat only*, *Simple live*, *Multistream*).

Stream keys, passwords and login tokens are never part of the text. Imported
tabs are added next to yours (none of your own tabs is overwritten); Live
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

#### Stream info

**Docks → Stream info** sets the stream **title** and **category/game** on
every logged-in platform at once (Twitch, YouTube and Kick; TikTok does not
allow it through its API). On YouTube it applies to the live broadcast (or
the next scheduled one) and the category is the video category: YouTube's
game title cannot be set through the API. It opens with what is live now; search a
category, pick it, and the same name is looked up on each platform (a
platform without it keeps its category). Untick a platform to leave it out.
It needs the Twitch `channel:manage:broadcast` and Kick `channel:write`
permissions: log out and in again once after updating.

#### Chat login

Logging in to Twitch, YouTube and/or Kick adds a message box under the chat (send to
one platform or all) and a menu on each author's name: timeout 1 or 10 minutes,
ban, unban / lift timeout, delete message. On Twitch it also brings follows
into Activity (you must be the broadcaster or a moderator), and the **Viewers**
button lists who is in chat (broadcaster or moderator) and who is banned
(broadcaster only), with timeout, ban and unban right there; Kick, YouTube and
TikTok do not offer these lists. On YouTube, sending and moderation go to your
own live broadcast's chat and each costs 50 of the 10,000 daily API units
(about 200 actions a day); reading the chat costs nothing. YouTube lifts a
ban by its id, so unban works for bans made from OBS since it started.
TikTok has no sending.

- **Twitch:** nothing to set up. Click **Log in** in Unified Chat → Settings →
  Twitch; OBS shows a code and opens twitch.tv/activate, approve it there. The
  plugin ships its own public app (a public app has no secret to hide).
  - **Want your own app?** At [dev.twitch.tv/console/apps](https://dev.twitch.tv/console/apps)
    → *Register Your Application* (Category *Chat Bot*, **Client Type:
    Public**, OAuth Redirect URL `http://localhost`) and paste its *Client ID*.
  - **Already have a Confidential app** (for example the one the old
    texuguito bot used)? Add `http://localhost:17563` to its OAuth Redirect
    URLs, paste its *Client ID* and *Client Secret* (the Secret field is
    optional on Twitch) and click **Log in**: the browser opens Twitch's
    login and comes back to OBS, no code to type.
- **YouTube:** nothing to set up. Click **Log in** in Unified Chat → Settings
  → YouTube; the browser opens Google's login (it may warn that Google has
  not verified the app yet: *Advanced → Go to…*) and comes back to OBS. The
  plugin's Google app keeps its secret on the same server as Kick's.
- **Kick:** nothing to set up either. Click **Log in** in Unified Chat → Settings
  → Kick; the browser opens Kick's login and comes back to OBS. Kick has no
  public apps (every token request needs the app's secret), so the plugin's
  Kick app keeps its secret on the project's server
  ([`server/kick-oauth`](server/kick-oauth)): the plugin logs in with PKCE and
  sends only the one-time code (or the refresh token) there, the server adds
  the secret, asks Kick and returns the answer. It logs nothing it receives.
  - **Want your own Kick app instead?** At
    [kick.com/settings/developer](https://kick.com/settings/developer) create
    an app with Redirect URL `http://localhost:53682/callback` and the scopes
    *user:read, channel:read, chat:write, moderation:ban,
    moderation:chat_message:manage*; paste its *Client ID* and *Client
    Secret* and the plugin talks to Kick directly.

Tokens are kept as plain text in the plugin's config folder
(`chat-accounts.json`, readable only by your user). Use **Log out** to revoke
them.
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

## Updates

When OBS starts, the plugin checks GitHub for a newer release and shows what
changed, with **Update**, **Later** and **Skip this version**. **Update**
downloads the package, checks it against the SHA-256 published with the
release and installs it **after you close OBS** (never mid-stream):

- **Windows:** runs the installer silently (plugin in `%ProgramData%\obs-studio\plugins`).
- **Linux:** unpacks the `.deb` into the plugin folder in your home; a
  system-wide install (`/usr`) asks for your password through `pkexec`.
  Flatpak OBS only gets a link to the release page.
- **macOS:** opens the `.pkg` in Installer.

Check by hand or turn the startup check off in **Tools → Meketreve: Check for updates**.

## Language

The UI ships with **English (en-US)** and **Portuguese (pt-BR)** strings, and
follows the language configured in OBS. Other languages fall back to English.

## Build / Release (CI)

Everything builds in GitHub Actions:

- **Any push** to `main` → builds all platforms and uploads artifacts:
  Windows `.zip` + installer `.exe`, Linux `.deb` / `.ddeb` / `.tar.xz`, macOS `.pkg`.
- **Releases** come from the **Release** workflow, run by hand once `main`
  has been tested in OBS (Actions → Release → Run workflow, or
  `gh workflow run release.yaml`; tick *dry run* to preview). It stops unless
  the last CI run on `main` passed, and only releases when there is a
  `feat:` (minor bump) or `fix:` (patch bump) commit since the last version
  tag (`feat!:` / `fix!:` bump the major version); `refactor:` and `perf:`
  commits are listed under *Other changes* but never trigger a release alone. It updates
  `buildspec.json`, tags with notes built from those commits and builds the
  tag, which publishes the GitHub Release the updater picks up.
  Preview locally: `python3 .github/scripts/next_release.py notes.md`.

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
