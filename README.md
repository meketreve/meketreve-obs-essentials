# Meketreve OBS Essentials

A toolkit of small OBS Studio filters/tools bundled in a single native plugin.
Built on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate),
so Windows / macOS / Linux binaries **and a Windows installer** are produced by
GitHub Actions — no local OBS dev dependencies required.

## Tools

| Tool | Type | What it does |
|------|------|--------------|
| **Bass Shake** | Video filter | Random camera/source shake driven by the bass energy of a chosen audio source (mic, desktop audio, …). |

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

## Build / Release (CI)

Everything builds in GitHub Actions:

- **Any push** to `main` → builds all platforms, uploads artifacts (zip + Windows installer `.exe`).
- **Tag** like `1.0.0` → also creates a draft GitHub Release with the installer attached.

```bash
git tag 1.0.0
git push origin 1.0.0
```

The Windows installer drops the plugin into
`%ProgramData%\obs-studio\plugins\meketreve-obs-essentials\`, which OBS 30+
loads automatically. No admin rights needed.

## Adding a new tool

1. Create `src/tools/<tool>.c` + `.h` exposing `void <tool>_register(void);`.
2. Add the `.c` to `target_sources(...)` in `CMakeLists.txt`.
3. Call `<tool>_register();` in `obs_module_load()` (`src/plugin-main.c`).
4. Add any shader to `data/effects/` and locale strings to `data/locale/en-US.ini`.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
