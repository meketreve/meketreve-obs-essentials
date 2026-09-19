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

The plugin is plain C against `libobs` with no platform-specific code, so it
builds and runs natively on Linux.

**From the `.deb`** (Debian / Ubuntu / Mint), attached to each release:

```bash
sudo apt install ./meketreve-obs-essentials-1.0.0-x86_64-linux-gnu.deb
```

**From source**, which also installs into your user plugin directory:

```bash
sudo apt install libobs-dev cmake build-essential
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

1. Create `src/tools/<tool>.c` + `.h` exposing `void <tool>_register(void);`.
2. Add the `.c` to `target_sources(...)` in `CMakeLists.txt`.
3. Call `<tool>_register();` in `obs_module_load()` (`src/plugin-main.c`).
4. Add any shader to `data/effects/` and locale strings to **both**
   `data/locale/en-US.ini` and `data/locale/pt-BR.ini` (keep the keys in sync).

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
