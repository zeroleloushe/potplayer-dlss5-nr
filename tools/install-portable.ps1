# Drop the built DLLs into a portable PotPlayer + SVP tree.
# Usage:
#   powershell -ExecutionPolicy Bypass -File install-portable.ps1 -PotPlayerDir "D:\Portable\PotPlayer"
#   powershell -ExecutionPolicy Bypass -File install-portable.ps1 -PotPlayerDir "D:\Portable\PotPlayer" -AviSynthPlugins "D:\Portable\AviSynth+\plugins64"

param(
    [Parameter(Mandatory = $true)][string]$PotPlayerDir,
    [string]$AviSynthPlugins,
    [string]$VapourSynthPlugins,
    [string]$BinDir
)

$ErrorActionPreference = "Stop"
if (-not $BinDir) {
    $BinDir = Join-Path $PSScriptRoot "..\build\Release"
    if (-not (Test-Path $BinDir)) { $BinDir = Join-Path $PSScriptRoot "..\build" }
}

function Find-First($root, $names) {
    foreach ($n in $names) {
        $p = Get-ChildItem -Path $root -Recurse -Filter $n -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($p) { return $p.DirectoryName }
    }
    return $null
}

if (-not (Test-Path $PotPlayerDir)) { throw "PotPlayerDir not found: $PotPlayerDir" }

if (-not $AviSynthPlugins) {
    $AviSynthPlugins = Find-First $PotPlayerDir @("AviSynth.dll", "avisynth.dll")
    if ($AviSynthPlugins) {
        $cand = Join-Path $AviSynthPlugins "plugins64"
        if (Test-Path $cand) { $AviSynthPlugins = $cand }
        else {
            $cand = Join-Path $AviSynthPlugins "plugins"
            if (Test-Path $cand) { $AviSynthPlugins = $cand }
        }
    }
}

if (-not $VapourSynthPlugins) {
    $vs = Find-First $PotPlayerDir @("vapoursynth.dll", "VSScript.dll")
    if ($vs) {
        $cand = Join-Path $vs "vapoursynth64\plugins"
        if (Test-Path $cand) { $VapourSynthPlugins = $cand }
        else {
            $cand = Join-Path $vs "plugins"
            if (Test-Path $cand) { $VapourSynthPlugins = $cand }
        }
    }
}

$runtime = Join-Path $env:LOCALAPPDATA "potplayer-dlss5-nr\runtime"
New-Item -ItemType Directory -Force -Path $runtime | Out-Null

function Copy-Built($name, $dest) {
    $src = Join-Path $BinDir $name
    if (-not (Test-Path $src)) { Write-Warning "missing $src — build the project first"; return }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item $src $dest -Force
    Write-Host "copied $name -> $dest"
}

if ($AviSynthPlugins) {
    Copy-Built "DLSS5NR.dll" $AviSynthPlugins
    Copy-Built "nvngx.dll_pot.dll" $AviSynthPlugins
} else {
    Write-Warning "AviSynth plugins folder not found. Pass -AviSynthPlugins."
}

if ($VapourSynthPlugins) {
    Copy-Built "dlss5nr.dll" $VapourSynthPlugins
    Copy-Built "nvngx.dll_pot.dll" $VapourSynthPlugins
}

$nr = Join-Path $runtime "nvngx_dlssnr.dll"
Write-Host ""
Write-Host "Runtime folder: $runtime"
if (Test-Path $nr) {
    Write-Host "nvngx_dlssnr.dll is already there."
} else {
    Write-Host "PUT nvngx_dlssnr.dll HERE (copy from a game that ships DLSS 5 NR, e.g. NBA 2K27)."
    Write-Host "This repo never ships NVIDIA's file."
}

Write-Host ""
Write-Host "Next:"
Write-Host "  1. PotPlayer renderer = Built-in Direct3D 11 (or madVR). Turn OFF D3D11 GPU Super Resolution — that is VSR, not DLSS 5."
Write-Host "  2. Keep your SVP AviSynth / VapourSynth Filter as it is."
Write-Host "  3. Append the line from scripts/after_svp.avs (or .vpy) AFTER SVSmoothFps."
Write-Host "     SVP 4: tray -> All settings -> search 'extra' / profile additional script."
Write-Host "  4. Play a 1080p file. Seek should not crash. If the picture is unchanged, NR failed open — check last_error via info=true."
