# WEAPON CROSS-TALK (P0-2, user playtest 2026-10-13: "the local gun visually fires with no input and no ammo use; seems tied
# to nearby bots firing; reload prompt with full ammo"). Tier TARGETED. One exe per run, direct-boot TDM with bots, windowed,
# RT off, HD off, frontend flow to a private TDM (map id -MapId), the local player idle (no fire input, mouse off), a deterministic 60 Hz display (WFC_RENDERHZ=60).
# Root cause found by code read (a17309b7): World's ONE WeaponFx fx_ is re-anchored every step to the LOCAL player's MuzzleFlash
# socket (World.cpp fx_.tick) and the muzzle templates' local-space emitters draw at that anchor, so every bot's muzzle flash
# (World.cpp:187, fx_.spawnMuzzleFlash) was drawn on the player's own gun.
#
# Runs:
#   idle     -Friendly / -Enemy bots, no input: captures every -Stride steps from the match start (WFC_SHOTMATCH)
#   vehicle  (-Vehicle) the same, transformed once at frame -TransformAt (WFC_AUTOTRANSFORM); log checks only
# Checks:
#   flash    large muzzle-flash EVENTS at the idle player's gun (crosstalk_flash.py: a static view, a near-camera bright warm
#            spike vs the captures 4 before / after): PASS 0. a17309b7: every large event was a blast on the player's own gun
#   ammo     the frame log's local ammo (ammo=clip/reserve) never drops without input on the same weapon: PASS 0 drops
#   hudclip  HUD NotifyWeaponClipAmmoChanged (WFC_HUDTRACE) with clip != capacity while idle (the movie's reload / low-ammo prompt
#            reads it): PASS 0; vehicle_hudclip the same in vehicle form
#
#   .\tools\fidelity\weapon-crosstalk.ps1 -Exe <wfc_rebuild.exe> -OutDir <dir> [-MapId 508] [-Friendly 7] [-Enemy 8]
#       [-Steps 3600] [-Stride 3] [-LargePx 2000] [-Vehicle [-TransformAt 1500]]
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [int]$MapId = 508, [int]$Friendly = 7, [int]$Enemy = 8,
      [int]$Steps = 3600, [int]$Stride = 3, [int]$LargePx = 2000, [switch]$Vehicle, [int]$TransformAt = 1500)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"; $env:PYTHONDONTWRITEBYTECODE = "1"
$res = New-WfcResults
function RunOne([string]$name, [int]$f, [int]$e, [hashtable]$extra, [int]$steps, [int]$stride) {
    $d = Join-Path $OutDir $name; $fr = Join-Path $d "frames"
    if (Test-Path (Join-Path $d "wfc.log")) { return $d }
    New-Item -ItemType Directory -Force $fr | Out-Null
    (Get-BotProfile $f $e 1 -Width 1280 -Height 720) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
    # (the lobby's extended flag is BotsExtended, not ExtendedPlayers - Gameplay: without it the roster clamps to 5 a side, 4v5)
    # bots come from the lobby (a direct match boot ignores ?Bots*): the frontend flow to a private TDM, as capacity-stress does;
    # the real HUD runs, so WFC_HUDTRACE logs its clip calls. The script quits ~15 s after the last capture step.
    $H = Get-ExeHooks $Exe; $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
    $s = (@((Get-MousePark (Get-WfcRoot)), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
            "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5",
            "call:Online.SetSelectedMapID,$MapId", "wait:t=1", "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:t=$([int]($steps / 60) + 15)", "quit")) -join ";"
    $env0 = @{ WFC_BOOT = "frontend"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "$([int]($steps / 60) + 400)"; WFC_RT_MODE = "0"
               WFC_NOMOUSE = "1"; WFC_SKIPINTRO = "1"; WFC_RENDERHZ = "60"; WFC_HUDTRACE = "1"; WFC_LOBBY_OPTIONS = "BotsExtended=$(if ($f + $e + 1 -gt 10) { 1 } else { 0 });BotsFriendly=$f;BotsEnemy=$e;BotsAutobot=$f;BotsDecepticon=$e;PointsToWin=9999;TimeLimit=900"
               WFC_SHOTMATCH = ($fr -replace '\\', '/') + ",30,$steps,$stride"; WFC_SMOKE_FRAMES = "100000000" }
    if ($H.Contains("WFC_CHARSELECT")) { $env0.WFC_CHARSELECT = "1" }
    if ($H.Contains("WFC_FLOWSEED")) { $env0.WFC_FLOWSEED = "1" }
    foreach ($k in $extra.Keys) { $env0[$k] = $extra[$k] }
    $code = Invoke-WfcExe $Exe $d $env0 "run.log" ([int]($steps / 60) + 600)
    "{0:HH:mm:ss} {1}: exit {2}, {3} frames" -f (Get-Date), $name, $code, @(Get-ChildItem $fr -Filter *.bmp).Count | Write-Host
    return $d
}
$idle = RunOne "idle" $Friendly $Enemy @{} $Steps $Stride
# vehicle row (Gameplay: the vehicle HUD mixed the vehicle clip with the robot capacity): the same, transformed at frame -TransformAt
$veh = if ($Vehicle) { RunOne "vehicle" $Friendly $Enemy @{ WFC_AUTOTRANSFORM = "$TransformAt" } $Steps 30 } else { $null }
# image check (python): large muzzle flashes at the idle player's gun with a static view
$js = Join-Path $OutDir "flash.json"
if (-not (Test-Path $js)) { & $py (Join-Path $PSScriptRoot "crosstalk_flash.py") (Join-Path $idle "frames") $js $LargePx | ForEach-Object { "  $_" } }
$fl = if (Test-Path $js) { Get-Content $js -Raw | ConvertFrom-Json } else { $null }
if (-not $fl -or $fl.status -eq "UNKNOWN") { Add-WfcResult $res "crosstalk.flash" "UNKNOWN" $null ("no measurement: {0}" -f $(if ($fl) { $fl.note } else { "no flash.json" })) "Experimental" }
else {
    Add-WfcResult $res "crosstalk.flash" $(if ($fl.events -eq 0) { "PASS" } else { "FAIL" }) $fl.events ("large muzzle-flash events at the idle player's gun {0} ({1} captures; {2} of {3} captures had a static view; first {4}); review events.png" -f $fl.events, $fl.flash_frames, $fl.static_frames, $fl.frames, $fl.first) "Gameplay" 0 "events"
}
# log checks: local ammo drops / HUD clip below capacity while idle
$lg = Join-Path $idle "wfc.log"
if (-not (Test-Path $lg)) { Add-WfcResult $res "crosstalk.ammo" "UNKNOWN" $null "no log" "Experimental" }
else {
    $am = @(Select-String -Path $lg -Pattern 'frame (\d+) pos .* ammo=(\d+)/(\d+) reloading=(\d).* hudW=(\S+)' | ForEach-Object { $g = $_.Matches[0].Groups; [pscustomobject]@{ f = [int]$g[1].Value; clip = [int]$g[2].Value; reserve = [int]$g[3].Value; rl = [int]$g[4].Value; w = $g[5].Value } })
    $drops = 0; $firstDrop = ""; for ($i = 1; $i -lt $am.Count; $i++) { if ($am[$i].w -eq $am[$i - 1].w -and $am[$i].clip -lt $am[$i - 1].clip -or $am[$i].w -eq $am[$i - 1].w -and $am[$i].reserve -lt $am[$i - 1].reserve) { if (-not $drops) { $firstDrop = "frame $($am[$i].f): $($am[$i-1].clip)/$($am[$i-1].reserve) -> $($am[$i].clip)/$($am[$i].reserve)" }; $drops++ } }
    $rl = @($am | Where-Object { $_.rl -eq 1 }).Count
    Add-WfcResult $res "crosstalk.ammo" $(if (-not $am.Count) { "UNKNOWN" } elseif ($drops -eq 0 -and $rl -eq 0) { "PASS" } else { "FAIL" }) $drops ("{0} frame-log samples; local ammo drops without input, same weapon {1} (first {2}; a weapon swap such as the spawn loadout is not a drop); samples reloading {3}" -f $am.Count, $drops, $firstDrop, $rl) "Gameplay" 0 "drops"
    $hc = @(Select-String -Path $lg -Pattern 'HUDCALL _global\.NotifyWeaponClipAmmoChanged\(([\d.]+), ([\d.]+)\)' | ForEach-Object { $g = $_.Matches[0].Groups; [pscustomobject]@{ clip = [double]$g[1].Value; cap = [double]$g[2].Value } })
    $low = @($hc | Where-Object { $_.clip -ne $_.cap })
    Add-WfcResult $res "crosstalk.hudclip" $(if (-not $hc.Count) { "UNKNOWN" } elseif ($low.Count -eq 0) { "PASS" } else { "FAIL" }) $low.Count ("HUD clip calls {0}, clip not equal to the capacity while idle {1}{2}" -f $hc.Count, $low.Count, $(if ($low.Count) { " (first " + $low[0].clip + "/" + $low[0].cap + ")" } else { "" })) "Gameplay" 0 "calls"
}
if ($veh) {
    $vl = Join-Path $veh "wfc.log"
    $vf = @(Select-String -Path $vl -Pattern 'frame \d+ pos .* form=VEHICLE' -ErrorAction SilentlyContinue).Count
    $hv = @(Select-String -Path $vl -Pattern 'HUDCALL _global\.NotifyWeaponClipAmmoChanged\(([\d.]+), ([\d.]+)\)' -ErrorAction SilentlyContinue | ForEach-Object { $g = $_.Matches[0].Groups; "{0}/{1}" -f $g[1].Value, $g[2].Value })
    $bad = @($hv | Where-Object { $p = $_ -split '/'; [double]$p[0] -ne [double]$p[1] })
    Add-WfcResult $res "crosstalk.vehicle_hudclip" $(if (-not $vf) { "UNKNOWN" } elseif ($bad.Count) { "FAIL" } else { "PASS" }) $bad.Count ("vehicle-form samples {0}; HUD clip calls {1} ({2}); not equal to the capacity without firing (a full clip; a17309b7 showed the vehicle clip over the robot capacity, 60/6) {3}" -f $vf, $hv.Count, (($hv | Select-Object -First 8) -join ", "), $bad.Count) "Gameplay" 0 "calls"
}
if (Test-Path (Join-Path $idle "CONTAMINATED.txt")) { Add-WfcResult $res "crosstalk.note" "INFO" $null "other lanes' processes ran during the idle run (untimed check: noted only)" "Experimental" }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"WEAPON CROSS-TALK: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
