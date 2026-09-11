# make_release_zip.ps1 -- assemble the Windows release archive uploaded to a GitHub release.
#
# Until now the release ZIP was put together by hand, which is how it kept drifting (a tool
# added to run\ was easy to forget, and launcher.ini was never in it at all).  This script is
# the list, in one place.
#
# It PACKAGES what is already in run\ -- it does not build.  Build first:
#     .\build_all_win.bat            (MSVC, x64)   or   .\build_win.ps1   (MinGW)
# then:
#     .\tools\make_release_zip.ps1 -Tag v0.12.3
#     gh release create v0.12.3 .\BuddyDoom-v0.12.3-windows-x64.zip --title "..." --notes "..."
#
# WHY launcher.ini ships: it carries the launcher's IWAD fingerprint cache (`iwadcache` lines
# -- MD5, byte size and map list per IWAD).  With it the launcher skips hashing every IWAD it
# finds on first run, which on a full collection is a few seconds of reading a few hundred MB.
# The stock IWAD checksums in it are the same public values every source port ships in its
# identification table.  Entries that do not match the user's own files are dropped on their
# first start (see icache_prune in tools/launcher.c), so a shipped cache can never mislead.
#
# NOTE: only the two PWADs that are OURS are packaged out of run\ID0\.  IWADs are id Software
# data -- never put doom.wad & co. in a release.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Tag,            # e.g. v0.12.3
    [string] $Out = $(Join-Path $PSScriptRoot ".."),        # where the .zip lands
    [string] $Arch = "windows-x64"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$run  = Join-Path $root "run"
$Out  = (Resolve-Path $Out).Path

# Required: the release is broken without these.
$required = @(
    "buddydoom.exe", "buddydoom_config.exe", "director.exe", "extractor.exe",
    "gpumon.exe", "launcher.exe", "mybuddy.exe", "SDL3.dll", "README.md"
)
# Optional: shipped when present, not worth failing a release over.
$optional = @("llm_player.py", "launcher.ini")
# Our own PWADs, kept under ID0\ so the game finds them with no -file argument.
$id0 = @("buddydoom.wad", "e1-arenas.wad")

$name  = "BuddyDoom-$Tag-$Arch"
$stage = Join-Path ([System.IO.Path]::GetTempPath()) "buddydoom-release\$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $stage "ID0") -Force | Out-Null

$missing = @()
foreach ($f in $required) {
    $src = Join-Path $run $f
    if (Test-Path $src) { Copy-Item $src $stage } else { $missing += $f }
}
if ($missing.Count) { throw "run\ is missing: $($missing -join ', ') -- build first." }

foreach ($f in $optional) {
    $src = Join-Path $run $f
    if (Test-Path $src) { Copy-Item $src $stage } else { Write-Warning "skipping absent $f" }
}
foreach ($f in $id0) {
    $src = Join-Path $run "ID0\$f"
    if (Test-Path $src) { Copy-Item $src (Join-Path $stage "ID0") } else { Write-Warning "skipping absent ID0\$f" }
}

# THIRD-PARTY.txt (SDL3 / stb_vorbis / Freedoom attribution) lives at the repo root when it
# has been written; earlier releases carried one, so warn rather than silently drop it.
$tp = Join-Path $root "THIRD-PARTY.txt"
if (Test-Path $tp) { Copy-Item $tp $stage } else { Write-Warning "no THIRD-PARTY.txt at the repo root -- release will ship without attribution notices" }

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

Write-Host ""
Write-Host "[release] $zip"
foreach ($e in [IO.Compression.ZipFile]::OpenRead($zip).Entries | Sort-Object FullName) {
    "{0,10}  {1}" -f $e.Length, $e.FullName
}
