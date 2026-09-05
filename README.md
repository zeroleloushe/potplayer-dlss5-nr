# potplayer-dlss5-nr

DLSS 5 Neural Rendering (NGX feature 18) as an **AviSynth+ / VapourSynth** plugin for a **portable PotPlayer + SVP** stack.

Unofficial. Not NVIDIA, not Daum, not SVP. Windows x64 + RTX only.

## What this is

SVP already interpolates frames (and has motion vectors internally). This plugin sits **after** SVP and runs the same NR pass as the [OBS plugin](https://github.com/Saganaki22/obs-dlss5-nr):

```
Decoder (DXVA copy-back)
  → AviSynth / VapourSynth Filter   ← SVP interpolates here
  → DLSS5NR()                       ← this plugin, BGRA → D3D12 feature 18 → BGRA
  → renderer (D3D11 / madVR)
```

v0.1 is the OBS-stable path: **per-frame, no motion vectors yet**, CPU-staged BGRA8. Fail-open: if NGX dies, you see the original frame.

Do **not** turn on PotPlayer’s “D3D11 GPU Super Resolution”. That is RTX VSR, a different thing.

## You must supply one NVIDIA file

This repo never contains `nvngx_dlssnr.dll` (~158 MB, v310.8+). Copy it yourself from a game that ships DLSS 5 NR (NBA 2K27 is the usual source) into:

```
%LOCALAPPDATA%\potplayer-dlss5-nr\runtime\nvngx_dlssnr.dll
```

`_nvngx.dll` is loaded from the installed NVIDIA driver. Driver **616.56+**.

## Build (Windows)

Visual Studio 2022 C++ + CMake:

```bat
build.bat
```

Artifacts: `build/Release/DLSS5NR.dll`, `vsdlss5nr.dll`, `nvngx.dll_pot.dll`.

The shim **must** keep the filename `nvngx.dll_pot.dll`. The NR runtime rejects callers whose path does not contain the substring `nvngx.dll` (`0xBAD00002`).

## Install into a portable pack

```bat
install.cmd "C:\Users\you\PotPlayer"
```

or:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File install-portable.ps1 -PotPlayerDir "C:\Users\you\PotPlayer"
```

The script finds AviSynth `plugins64` / VapourSynth `plugins` under that tree and copies the DLLs. Then put `nvngx_dlssnr.dll` in the runtime folder it prints.

## Hook it into SVP

### AviSynth Filter (classic SVP)

Keep your current SVP setup (AviSynth Filter = Prefer, DXVA copy-back, remote control on). Do **not** switch `main.setup.potplayer.native`.

Add **after** `SVSmoothFps` / `MBlockFps`:

```avs
ConvertToRGB32()
DLSS5NR(style=1, intensity=1.0, tone=1.0, structure=1.5, skin=2.0, automask=true)
```

Where to paste it:

- SVP tray → **All settings** → search for extra / additional / post script for the current video profile.
- Or, if your portable pack uses a static `.avs`, append the two lines at the end.

`DLSS5NR` is `MT_SERIALIZED` — do not expect it to scale across `Prefetch` threads. SVP can keep its own threads; NR runs under a D3D12 mutex.

### VapourSynth Filter (SVP RIFE)

```python
clip = core.resize.Bicubic(clip, format=vs.RGB24, matrix_in_s="709")
clip = core.dlss5.NR(clip, style=1, intensity=1.0, structure=1.5, skin=2.0, automask=1)
clip = core.resize.Bicubic(clip, format=vs.YUV420P8, matrix_s="709")
```

Put `vsdlss5nr.dll` + `nvngx.dll_pot.dll` in the VapourSynth plugins folder. SVP → Utilities → Set environment variables for VapourSynth, then restart PotPlayer.

## Parameters

| name | default | meaning |
|---|---|---|
| style | 1 | 0 default, 1 natural, 2 cinematic |
| preset | 0 | 0–3 (often inert on current 310.8) |
| intensity | 1.0 | 0..2, below 1 blends toward source |
| tone | 1.0 | local tone |
| structure | 1.5 | local structure |
| skin | 2.0 | skin structure (with automask) |
| automask | true | auto mask |
| gpu | 0 | index among NVIDIA adapters |
| runtime | `%LOCALAPPDATA%\potplayer-dlss5-nr\runtime` | folder with `nvngx_dlssnr.dll` |

Seek / scene cuts set `Reset=1` automatically when the frame number is not `last+1`.

## Why not a PotPlayer renderer plugin

PotPlayer has no public vout SDK comparable to OBS filters. The portable SVP graph already has a hole after interpolation (AviSynth / VS). That is the right place: NR sees the frames you actually watch, including interpolated ones.

ReShade + RenoDX on the built-in D3D11 renderer is a zero-compile fallback if you only want to *try* NR today, but it fights madVR and does not give you these parameters.

## Limits (v0.1)

- No motion vectors from SVP yet (planned). Fast pans can smear; same as current OBS plugin.
- 8-bit BGRA. 10-bit / HDR is not wired.
- CPU upload/readback. 1080p60 is the target. 4K60 will hurt; throttle SVP or drop NR on 4K.
- RTX 50-series verified by the community NR runtime; 40/30/20 “reported” with 310.8. `0xBAD00001` = this runtime build rejected your GPU.
- Frame generation is out of scope.

## License

GPL-2.0. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
