# Milestone 06: the real user path on every TDM map, one fresh process per map.
#
#   .\tools\fidelity\m06-maps-tdm.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Maps MP_IAC_Seed,...]
#
# boot (intro skipped) -> Start -> Multiplayer -> Private Match -> TDM -> Create Game -> game lobby -> map selector steps
# to the map (RE C2/C3: TransLevels order, index 0 = Seed) -> Start Game -> 10 s countdown -> loading -> character
# selection -> spawn -> walk / turn / fire / boost cycles / transform every 5 s -> pause + resume -> kills, deaths and
# respawns through Gameplay's rules (WFC_LIFECYCLE=3, TEST ONLY) -> score-limit end -> EndGameStats -> lobby return.
# Every owner is checked for the SAME selected map: frontend selection / launch, Gameplay match.gameplay + MATCH init,
# Systems audio.loaded level, Rendering render data log. Product frames (shot:) at each stage; contact sheet per map.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Maps = @())
$ErrorActionPreference = "Stop"
$Maps = @($Maps | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$order = [ordered]@{ MP_IAC_Seed = 501; MP_IAC_Berth = 502; MP_UND_Complex = 503; MP_IAC_Rust = 504; MP_ORB_Debris = 507; MP_IAC_Streets = 508; MP_KON_Molten = 509; MP_UND_Gorge = 510 }
if (-not $Maps.Count) { $Maps = @($order.Keys) }
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; $rd = Join-Path $Root "work\render"
$H = Get-ExeHooks $exe; $null = Set-WfcInputMode $H $exe
$res = New-WfcResults
function Res($id, $status, $note, $owner = "", $m = $null) { Add-WfcResult $res "tdm.$id" $status $m $note $owner }
$keys = @($order.Keys)
$rows = New-Object System.Collections.Generic.List[object]
foreach ($map in $Maps) {
    $idx = [Array]::IndexOf($keys, $map); $id = $order[$map]
    $d = Join-Path $OutDir $map; New-Item -ItemType Directory -Force $d | Out-Null
    $sel = if ($idx -gt 0) { (Keys @(40) 0.6) + ";" + (Keys (@(39) * $idx) 0.9) + ";" + (Keys @(38) 0.6) } else { "" }
    $s = @((Path-ToHostOptions 0), (Path-CreateGame), $sel, "wait:t=1", (Shot $d "a_lobby"), "snapshot:lobby", (Path-StartGame),
           "wait:loading=1", "wait:t=1", (Shot $d "b_loading"), "wait:level=Match", "wait:t=1.5", (Shot $d "c_pregame"), "wait:ui=InGame", "wait:t=2", (Shot $d "d_ingame"), "dump:Hud",
           "wait:t=4", (Shot $d "e_ingame_later"), "showmenu", "wait:ui=Paused", "wait:t=1", (Shot $d "f_pause"), (K 13), "wait:ui=InGame", "wait:t=1", "snapshot:resumed",
           "wait:ui=Spectating", "wait:t=0.5", (Shot $d "g_spectating"), "wait:ui=GameEnded", "wait:t=2", (Shot $d "h_endstats"), "snapshot:ended",
           "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2", (Shot $d "i_lobby_return"), "snapshot:returned", "quit") -join ";"
    $env = @{ WFC_BOOT = "frontend"; WFC_PLATFORM = "XBOX360"; WFC_SKIPINTRO = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOWSEED = "1"; WFC_FLOW_TIMEOUT = "600"
              WFC_LIFECYCLE = "3"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.3"; WFC_AUTOFIRE = "1"; WFC_AUTOBOOST_CYCLE = "90"; WFC_PRESSTRANSFORM_EVERY = "300"
              WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "15"; WFC_AMBLOG = "1"; WFC_MUSICLOG = "1"; WFC_MATCHAUDIOLOG = "1"; WFC_CUELOG = "1"; WFC_RENDER_DATA = $rd }
    if (-not $H.Contains("WFC_PLATFORM")) { $env.Remove("WFC_PLATFORM") }
    $r = Invoke-WfcSampled $exe $d $env 900 1.0
    $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $log = Join-Path $d "wfc.log"
    $lastWait = (@(Flow-Ev $F "script.wait") | Select-Object -Last 1).cond
    $done = @(Flow-Ev $F "snapshot" | Where-Object why -eq "returned").Count -gt 0
    $selMap = @(Flow-Ev $F "snapshot" | Where-Object why -eq "lobby")[0]; $ml = @(Flow-Ev $F "match.launch")[0]; $mg = @(Flow-Ev $F "match.gameplay")[0]; $al = @(Flow-Ev $F "audio.loaded")[0]
    $init = @(Grep-Log $log '\] MATCH init ')[0]; $rdl = @(Grep-Log $log "(?i)render.*$map|$map.*render")
    $kills = @(Grep-Log $log '\] MATCH kill '); $deaths = @(Grep-Log $log '\] MATCH death '); $resp = @(Grep-Log $log '\] MATCH respawn ' | ForEach-Object { [double][regex]::Match($_.text, 'delay_s=([\d.]+)').Groups[1].Value }); $end = @(Grep-Log $log '\] MATCH end ')[0]
    $spawn = @(Grep-Log $log '\] MATCH spawn ')[0]
    $frames = @(Read-WfcFrames $log | Where-Object { $_.frame -gt 0 })
    $forms = @($frames | ForEach-Object { $_.form } | Select-Object -Unique); $boostF = @($frames | Where-Object { $_.drv -eq 1 }).Count
    $miny = if ($frames.Count) { ($frames | Measure-Object y -Minimum).Minimum } else { $null }
    $killz = @(Grep-Log $log '(?i)fell out of world|KillZ'); $fell = @(Grep-Log $log 'World: player fell out of world')
    $retTr = @(Flow-Ev $F "travel" | Where-Object { $_.from -eq "Match" -and $_.url -like "UI_Lobby_m*" })[0]
    $music = @(Grep-Log $log '\] MUSIC play \S*DM_'); $mAud = @(Grep-Log $log '(?i)\] MATCHAUDIO'); $announce = @(Grep-Log $log '\] ANNOUNCER play ')
    $hudOpen = @(Flow-Ev $F "gfx.movie" | Where-Object { $_.movie -like "*Hud_GFX*" }).Count
    $shotStats = @{}; foreach ($sn in "d_ingame", "e_ingame_later", "f_pause", "g_spectating", "h_endstats") { $st = Shot-Stats (Join-Path $d "$sn.bmp"); if ($st) { $shotStats[$sn] = $st } }
    $tiles = @(Get-ChildItem $d -Filter *.bmp | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = "$map $($_.BaseName)" } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "tdm_$map.png") 3 426 240 }
    $row = [pscustomobject][ordered]@{ map = $map; mapId = $id; completed = $done; stopped_at = $(if (-not $done) { $lastWait } else { "" }); lobby_selected = $selMap.mapId; launch_runtime = $ml.runtimeDir; gameplay_map = $mg.map; gameplay_mode = $mg.mode
        audio_level = $al.level; render_log = $rdl.Count; spawn = $(if ($spawn) { [regex]::Match($spawn.text, 'start=(\S+)').Groups[1].Value }); kills = $kills.Count; deaths = $deaths.Count
        respawn_s = (($resp | ForEach-Object { "{0:N2}" -f $_ }) -join ","); end = $(if ($end) { $end.text -replace '^.*MATCH end ', '' }); return_mapId = $(if ($retTr) { (Parse-Url $retTr.url).keys.MapId })
        forms = ($forms -join "/"); boost_frames = $boostF; min_y = $miny; fell_out = $fell.Count; dm_music = (($music | ForEach-Object { [regex]::Match($_.text, 'play (\S+)').Groups[1].Value }) -join ","); announcer_cues = $announce.Count
        hud_movie = $hudOpen; ingame_black = $(if ($shotStats.d_ingame) { $shotStats.d_ingame.black }); ingame_flatgrey = (FlatGreyFraction (Join-Path $d "d_ingame.bmp")); peak_private_mb = (($r.samples | Measure-Object private_mb -Maximum).Maximum) }
    $rows.Add($row)
    $own = ($ml.runtimeDir -eq $map) -and ($mg.map -eq $map) -and ("$($al.level)" -like "*$map*") -and ([int]$selMap.mapId -eq $id)
    Res "$map.lifecycle" $(if ($done) { "PASS" } else { "FAIL" }) ("frontend -> TDM -> {0} -> match -> pause / resume -> score-limit end -> lobby: {1}" -f $map, $(if ($done) { "completed" } else { "stopped at '$lastWait' (exit $($r.rc), timed out $($r.timedOut))" })) "Integration"
    Res "$map.selection_reaches_owners" $(if ($own) { "PASS" } else { "FAIL" }) ("lobby selected mapId {0} (expected {1}); frontend launch {2}; Gameplay {3} {4}; Systems audio level {5}; render log lines naming the map {6}" -f $selMap.mapId, $id, $ml.runtimeDir, $mg.map, $mg.mode, $al.level, $rdl.Count) "Frontend/Integration"
    Res "$map.match_rules" $(if ($end -and $end.text -match 'score_limit' -and $kills.Count -ge 3 -and $resp.Count -and @($resp | Where-Object { [Math]::Abs($_ - 5.0) -gt 0.35 }).Count -eq 0) { "PASS" } elseif (-not $done) { "FAIL" } else { "FAIL" }) ("kills {0}, deaths {1}, respawn delays {2} s, end {3}; spawn {4}" -f $kills.Count, $deaths.Count, $row.respawn_s, $row.end, $row.spawn) "Gameplay"
    Res "$map.return_to_lobby" $(if ($retTr -and (Parse-Url $retTr.url).keys.MapId -eq "$id") { "PASS" } else { "FAIL" }) ("lobby travel after the match keeps MapId {0} (selected {1})" -f $row.return_mapId, $id) "Gameplay/Frontend"
    Res "$map.robot_vehicle_boost" $(if ($forms -contains "ROBOT" -and $forms -contains "VEHICLE" -and $boostF -gt 0) { "PASS" } elseif ($frames.Count) { "FAIL" } else { "SKIP" }) ("forms seen {0}; boosting frames {1} (transform every 5 s, boost cycles, firing)" -f $row.forms, $boostF) "Gameplay"
    Res "$map.not_under_map" $(if ($fell.Count) { "FAIL" } else { "PASS" }) ("fell out of the world (KillZ) {0}; min y {1}" -f $fell.Count, $miny) "Gameplay"
    # world coverage of the frontend-launched match with the HUD band and the player excluded (a HUD over a missing world FAILS)
    $wv = @("d_ingame", "e_ingame_later" | ForEach-Object { Present-World (Join-Path $d "$_.bmp") } | Where-Object { $_ })
    $wvd = @($wv | ForEach-Object { Present-WorldVerdict $_ })
    Res "$map.world_coverage" (Present-WorldSetVerdict $wv) ("in-match world detail (HUD and player excluded): {0}; verdicts {1} (PASS >= 0.15, FAIL < 0.10 or black >= 0.6 or one blank area >= 0.5)" -f (($wv | ForEach-Object { $_.detail }) -join " / "), ($wvd -join " / ")) "Rendering/Integration"
    Res "$map.presented" $(if (-not $shotStats.d_ingame) { "SKIP" } elseif ($shotStats.d_ingame.black -gt 0.85 -or $row.ingame_flatgrey -gt 0.15) { "FAIL" } else { "HUMAN" }) ("in-game frame: near-black {0:P0}, flat grey {1:P0}; HUD movie opened {2}; tdm_{3}.png is the human check" -f $row.ingame_black, $row.ingame_flatgrey, $hudOpen, $map) "Rendering/Frontend"
    Res "$map.match_audio" $(if ($music.Count) { "PASS" } else { "FAIL" }) ("game-type music starts: {0}; announcer lines played {1} (RE OV A5: DM_START, final stretch, DM_END_<winner>)" -f $row.dm_music, $announce.Count) "Systems/Gameplay"
}
Write-WfcCsv $rows (Join-Path $OutDir "tdm_maps.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"TDM MAPS: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ") + " -> $OutDir"
