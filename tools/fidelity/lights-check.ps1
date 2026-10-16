# One-arm confirmation that a build's RT-era lights are live (Integration 2026-10-16; for exes before Systems' one-shot
# "RT-era lights (first Realistic frame)" line): one lockstep-ab arm (match boot, 31 v 32 + the bot player, Realistic) with
# WFC_MUZZLELOG=1 and WFC_LOGEVERY at its default (the arm's empty value removes the harness's 0); counts "FX LIGHT" spawn lines
# (effect lights) and the periodic "HEADLIGHTS N spots" lines. Untimed (shared GPU), one exe.
#   .\tools\fidelity\lights-check.ps1 -Root <tree with build*\bin\wfc_rebuild.exe> -OutDir <dir> [-Map 508] [-Env "WFC_FXLIGHTS=1;WFC_HEADLIGHTS=1"]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string]$Map = "508",
      [string]$Env = "WFC_FXLIGHTS=1;WFC_HEADLIGHTS=1", [int]$Steps = 3000)
$env:WFC_GATE_GPU_POLICY = "shared"; $env:WFC_GATE_GPU_MAX_OTHERS = "1"; $env:WFC_GATE_GPU_GRACE_MIN = "0"
& (Join-Path $PSScriptRoot "lockstep-ab.ps1") -OutDir $OutDir -Map $Map -Arms "lights|$Root|WFC_RT_MODE=2;WFC_MUZZLELOG=1;WFC_LOGEVERY=;$Env" `
    -Steps $Steps -From 600 -RenderSize 1280x720 -PlayerBot 1 -Reps 1 -Seed 1234 *> (Join-Path $OutDir "..\lights-check_$Map.out")
$lg = Join-Path $OutDir "lights-r1\wfc.log"
if (-not (Test-Path $lg)) { "LIGHTS CHECK ${Map}: no log ($lg)"; exit 1 }
$fx = @(Select-String -Path $lg -Pattern 'FX LIGHT' -SimpleMatch).Count
$hl = @(Select-String -Path $lg -Pattern 'HEADLIGHTS (\d+) spots' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
$rt = @(Select-String -Path $lg -Pattern 'renderer: Ray Tracing [A-Za-z]+' | Select-Object -Last 1 | ForEach-Object { $_.Matches[0].Value })
"LIGHTS CHECK {0}: {1}; FX LIGHT spawn lines {2}; HEADLIGHTS lines {3} (spots max {4}); env {5}" -f $Map, ($rt -join ''), $fx, $hl.Count, $(if ($hl.Count) { ($hl | Measure-Object -Maximum).Maximum } else { "-" }), $Env
