# Drop the built DLLs into a portable PotPlayer + SVP tree.
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File install-portable.ps1 -PotPlayerDir "D:\Portable\PotPlayer"
#   install.cmd "D:\Portable\PotPlayer"

param(
    [Parameter(Mandatory = $true, Position = 0)][string]$PotPlayerDir,
    [string]$AviSynthPlugins,
    [string]$VapourSynthPlugins,
    [string]$BinDir
)

$ErrorActionPreference = "Stop"

function Find-First([string]$root, [string[]]$names) {
    foreach ($n in $names) {
        $p = Get-ChildItem -Path $root -Recurse -Filter $n -ErrorAction SilentlyContinue |
            Where-Object { -not $_.PSIsContainer } |
            Select-Object -First 1
        if ($p) { return $p.DirectoryName }
    }
    return $null
}

function Find-PluginDir([string]$startDir, [string]$dllName) {
    $dllDir = Find-First $startDir @($dllName)
    if (-not $dllDir) { return $null }
    foreach ($sub in @("plugins64+", "plugins64", "plugins")) {
        $cand = Join-Path $dllDir $sub
        if (Test-Path $cand) { return $cand }
    }
    $parent = Split-Path $dllDir -Parent
    foreach ($sub in @("plugins64+", "plugins64", "plugins")) {
        $cand = Join-Path $parent $sub
        if (Test-Path $cand) { return $cand }
    }
    return $dllDir
}

if (-not (Test-Path -LiteralPath $PotPlayerDir)) {
    throw "PotPlayerDir not found: $PotPlayerDir"
}

if (-not $BinDir) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot "DLSS5NR.dll")) {
        $BinDir = $PSScriptRoot
    } elseif (Test-Path -LiteralPath (Join-Path $PSScriptRoot "..\build\Release\DLSS5NR.dll")) {
        $BinDir = Join-Path $PSScriptRoot "..\build\Release"
    } elseif (Test-Path -LiteralPath (Join-Path $PSScriptRoot "..\build\DLSS5NR.dll")) {
        $BinDir = Join-Path $PSScriptRoot "..\build"
    } else {
        $BinDir = $PSScriptRoot
    }
}

$searchRoots = @($PotPlayerDir)
$parent = Split-Path -LiteralPath $PotPlayerDir -Parent
if ($parent) { $searchRoots += $parent }

if (-not $AviSynthPlugins) {
    foreach ($root in $searchRoots) {
        $AviSynthPlugins = Find-PluginDir $root "AviSynth.dll"
        if (-not $AviSynthPlugins) { $AviSynthPlugins = Find-PluginDir $root "avisynth.dll" }
        if ($AviSynthPlugins) { break }
    }
}

if (-not $VapourSynthPlugins) {
    foreach ($root in $searchRoots) {
        $VapourSynthPlugins = Find-PluginDir $root "vapoursynth.dll"
        if (-not $VapourSynthPlugins) { $VapourSynthPlugins = Find-PluginDir $root "VSScript.dll" }
        if ($VapourSynthPlugins) { break }
    }
}

$runtime = Join-Path $env:LOCALAPPDATA "potplayer-dlss5-nr\runtime"
New-Item -ItemType Directory -Force -Path $runtime | Out-Null

function Copy-Built([string]$name, [string]$dest) {
    $src = Join-Path $BinDir $name
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Warning "missing $src"
        return $false
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item -LiteralPath $src -Destination $dest -Force
    Write-Host "copied $name -> $dest"
    return $true
}

$copied = $false

if ($AviSynthPlugins) {
    if (Copy-Built "DLSS5NR.dll" $AviSynthPlugins) { $copied = $true }
    Copy-Built "nvngx.dll_pot.dll" $AviSynthPlugins | Out-Null
} else {
    Write-Warning "AviSynth plugins folder not found. Re-run with -AviSynthPlugins `"D:\path\plugins64`""
}

if ($VapourSynthPlugins) {
    if (Copy-Built "vsdlss5nr.dll" $VapourSynthPlugins) { $copied = $true }
    Copy-Built "nvngx.dll_pot.dll" $VapourSynthPlugins | Out-Null
}

$scriptDest = Join-Path $PotPlayerDir "dlss5-nr"
New-Item -ItemType Directory -Force -Path $scriptDest | Out-Null
foreach ($f in @("after_svp.avs", "after_svp.vpy")) {
    $src = Join-Path $PSScriptRoot $f
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination $scriptDest -Force
        Write-Host "copied $f -> $scriptDest"
    }
}

$nr = Join-Path $runtime "nvngx_dlssnr.dll"
Write-Host ""
Write-Host "Runtime folder: $runtime"
if (Test-Path -LiteralPath $nr) {
    Write-Host "nvngx_dlssnr.dll is already there."
} else {
    Write-Host "PUT nvngx_dlssnr.dll HERE (copy from a game that ships DLSS 5 NR)."
    Write-Host "This repo never ships NVIDIA files."
}

Write-Host ""
if (-not $copied) {
    Write-Host "No plugin DLL was copied. Pass -AviSynthPlugins or -VapourSynthPlugins."
    exit 1
}

Write-Host "Next:"
Write-Host "  1. PotPlayer renderer = Built-in Direct3D 11 or madVR. Turn OFF D3D11 GPU Super Resolution (that is VSR, not DLSS 5)."
Write-Host "  2. Keep your SVP AviSynth / VapourSynth Filter as it is."
Write-Host "  3. Append the two lines from after_svp.avs AFTER SVSmoothFps."
Write-Host "     SVP 4 tray -> All settings -> search extra / profile additional script."
Write-Host "  4. Play a 1080p file. If the picture is unchanged, the runtime DLL is missing or the GPU was rejected."
