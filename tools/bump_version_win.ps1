# bump_version_win.ps1 -- the Windows half of build.sh's version bookkeeping.
#
# build.sh does two things on every Linux build that the Windows build did not do
# at all:
#   (A) bump the fork version (BUDDYDOOM_VERSION, shown in the window title) by
#       +0.0.1 per recompile;
#   (B) fingerprint the structs p_saveg.c memcpy's, and bump VERSION_NUM when that
#       fingerprint changes, so stale saves are REJECTED rather than loaded into a
#       struct that is no longer the same size.
#
# (B) is the one that matters: a struct grown while building on Windows used to
# leave VERSION_NUM untouched, and an old save would then be accepted by the
# version check and memcpy'd into the new, larger layout.
#
# The fingerprint is kept in its OWN file (buddydoom_saveg_win.sig) rather than
# shared with build.sh's, because Windows is LLP64 and Linux LP64 -- if the two
# ever disagree on a struct size, a shared file would make each platform see the
# other's value as "changed" and bump forever.  The cost is that a struct change
# bumps once per platform; harmless, since all the version needs to be is
# different from what the old saves carry.
#
# Run from build_all_win.bat AFTER vcvars, so cl.exe is on PATH.

param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$src  = Join-Path $Root 'files'
$verf = Join-Path $src  'buddydoom_version.h'
$sigf = Join-Path $src  'buddydoom_saveg_win.sig'
$ddef = Join-Path $src  'doomdef.h'

# --- (A) fork version: +0.0.1 per build --------------------------------------
$fork = '0.2.0'
if (Test-Path -LiteralPath $verf) {
  $m = Select-String -LiteralPath $verf -Pattern 'BUDDYDOOM_VERSION\s+"([0-9.]+)"'
  if ($m) { $fork = $m.Matches[0].Groups[1].Value }
}
$p = $fork.Split('.')
if ($p.Count -eq 3) {
  $fork = '{0}.{1}.{2}' -f $p[0], $p[1], ([int]$p[2] + 1)
  $txt = @"
// BuddyDoom fork version.  AUTO-MANAGED by build.sh / tools\bump_version_win.ps1 --
// the patch field is bumped +0.0.1 on every recompile.  Bump the major/minor by hand
// when you cut a release tag (and the patch keeps counting builds from there).
#ifndef __BUDDYDOOM_VERSION__
#define __BUDDYDOOM_VERSION__
#define BUDDYDOOM_VERSION "$fork"
#endif
"@
  Set-Content -LiteralPath $verf -Value $txt -Encoding ascii
  Write-Host "[build] fork version $fork"
}

# --- (B) savegame layout fingerprint -> VERSION_NUM ---------------------------
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  Write-Host '[build] (warning: cl.exe not on PATH; savegame-layout check skipped)'
  exit 0
}

$tmp   = Join-Path $env:TEMP ('bd_sgprobe_' + $PID)
$null  = New-Item -ItemType Directory -Force -Path $tmp
$probe = Join-Path $PSScriptRoot 'saveg_probe.c'	# tools/, not files/ -- see the file
$exe   = Join-Path $tmp 'saveg_probe.exe'
$sdl   = Join-Path (Split-Path -Parent $Root) 'SDL3\include'

Push-Location $src
# $src is on the include path explicitly: the probe lives in tools\ but includes
# the engine's headers, and cl resolves "quoted" includes relative to the SOURCE
# file's directory first.
$cc = & cl.exe /nologo /I"$src" /I"$sdl" /DSDL_MAIN_HANDLED /D_CRT_SECURE_NO_WARNINGS `
               /Fo:"$tmp\" /Fe:"$exe" "$probe" 2>&1
$built = $LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $exe)
Pop-Location

if (-not $built) {
  Write-Host '[build] (warning: savegame-layout probe did not compile; engine version unchanged)'
  Remove-Item -Recurse -Force -LiteralPath $tmp -ErrorAction SilentlyContinue
  exit 0
}

$sig = (& $exe | Select-Object -First 1).Trim()
Remove-Item -Recurse -Force -LiteralPath $tmp -ErrorAction SilentlyContinue

if (-not $sig) { exit 0 }

$old = ''
if (Test-Path -LiteralPath $sigf) { $old = (Get-Content -LiteralPath $sigf -Raw).Trim() }

if ($old -and $old -ne $sig) {
  $dd = Get-Content -LiteralPath $ddef -Raw
  if ($dd -match 'VERSION_NUM\s*=\s*(\d+)') {
    $ev = [int]$Matches[1] + 1
    $dd = $dd -replace 'VERSION_NUM\s*=\s*\d+', ("VERSION_NUM =  $ev")
    Set-Content -LiteralPath $ddef -Value $dd -Encoding ascii -NoNewline
    $shown = '{0}.{1:D2}' -f [int]($ev / 100), ($ev % 100)
    Write-Host "[build] savegame layout changed ($old -> $sig) -> engine version bumped to $shown (old saves now rejected, not crashed)"
  }
}
Set-Content -LiteralPath $sigf -Value $sig -Encoding ascii
