# Milestone 05 end-to-end lifecycle gate (v2):
#   cold boot -> logos / intro -> frontend -> Multiplayer -> lobby -> TDM -> Streets -> loading -> countdown -> spawn ->
#   gameplay -> death / kill / score -> respawn -> match end -> return to lobby -> second match -> return to frontend,
#   plus repeated frontend <-> Streets cycles, map / mode ownership, and every owner's self-test on the same exe.
#
#   .\tools\fidelity\m05-e2e-gate.ps1 -Root <work\ab\<target>> [-Config Release|Debug] [-OutDir <dir>] [-Cycles 8]
#                                     [-Full] [-Runs R1,R2,R3,R4,R5,R6,S]
#
# Layers keep "data exists" apart from "the user experiences it":
#   DATA       authored data / catalogs / unit tests
#   STATE      the product's own trace (WFC_FLOWLOG, wfc.log) reaches the state with the expected values
#   PRESENTED  what the player sees: product-side frame captures (shot:, the composed frame) judged in pixels
#   MATCH      the TDM rules as executed by Gameplay, observed through the frontend-launched match
#   AUDIO      one audio lifecycle: music / beds / UI sounds per level, no doubles, no leftovers
#   OWNERSHIP  the map / mode the UI selected is the one every owner runs
#   LIFETIME   memory / handles / GL objects / audio PCM / voices / UI state across repeated cycles
#   SELFTEST   the owners' own self-tests run on THIS executable (their claims, re-measured on the merge)
# Status: PASS / FAIL (contradicts CONFIRMED/HIGH evidence) / KNOWN (documented gap, owner) / INFO / SKIP (hook or
# feature absent in this build; the check is ready) / HUMAN (HUMAN-CHECK-M05.md).
# Every product input goes through the real path: key presses into the shipped movies (Start, arrows, A); bridge
# calls only where the original movie would make the same call and a key path is not needed (Quit from pause).
param([Parameter(Mandatory)][string]$Root, [ValidateSet("Release", "Debug")][string]$Config = "Release", [string]$OutDir = "",
      [int]$Cycles = 8, [switch]$Full, [string[]]$Runs = @(), [int]$TimeoutSec = 1500)
$ErrorActionPreference = "Stop"
$Runs = @($Runs | ForEach-Object { $_ -split "," } | Where-Object { $_ }); if (-not $Runs.Count) { $Runs = @("R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "S") }
if (-not $Full) { $Runs = @($Runs | Where-Object { $_ -ne "R3" }) }
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path
if (-not $OutDir) { $OutDir = Join-Path (Get-WfcRoot) ("work\fidelity\m05gate\" + (Get-Date -Format "yyyyMMdd-HHmmss") + "-$Config") }
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$X = Get-Content -Raw (Join-Path $PSScriptRoot "m05\expectations.json") | ConvertFrom-Json
$binDir = if ($Config -eq "Release") { Join-Path $Root "build-release\bin" } else { Join-Path $Root "build\bin" }
$exe = Join-Path $binDir "wfc_rebuild.exe"
$rd = Join-Path $Root "work\render"
$AT = "F:\Transformers Rebuild\AssetTools\manifests"
$res = New-WfcResults
function Res($layer, $id, $status, $note, $owner = "", $measured = $null) { Add-WfcResult $res ("e2e.{0}.{1}" -f $layer, $id) $status $measured $note $owner }
function Conf($node) { if ($node.conf -in "CONFIRMED", "HIGH") { "FAIL" } else { "KNOWN" } }
function Near($a, $b, $tol) { return ($a -ne $null) -and ([Math]::Abs([double]$a - [double]$b) -le $tol) }
$target = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { (Get-Content (Join-Path $Root "M05_TARGET.txt")) -join "; " } else { "(no M05_TARGET.txt)" }
if (-not (Test-Path $exe)) { Res "DATA" "exe" "SKIP" "no $exe"; $null = Write-WfcReport $res (Join-Path $OutDir "report.json"); return }
$H = Get-ExeHooks $exe
function Has($hook) { return $H.Contains($hook) }
$uiMode = Set-WfcInputMode $H $exe   # ui:<Action> through UiBindings when supported, else key:<flash code>
Set-Content -Encoding UTF8 (Join-Path $OutDir "target.txt") ("exe=$exe`nconfig=$Config`n$target`nhooks=" + (($H | Sort-Object) -join ","))
function BaseEnv($dir, [hashtable]$extra = @{}) {
    $e = @{ WFC_BOOT = "frontend"; WFC_FLOWLOG = (Join-Path $dir "flow.jsonl"); WFC_FLOWSEED = "1"; WFC_FLOW_TIMEOUT = "$TimeoutSec"; WFC_LOGEVERY = "15"
            WFC_SMOKE_FRAMES = "100000000"; WFC_AMBLOG = "1"; WFC_MUSICLOG = "1"; WFC_MIXERLOG = "1"; WFC_LEVELAUDIOLOG = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.35" }
    if (Test-Path $rd) { $e.WFC_RENDER_DATA = $rd }
    if (Has "WFC_PLATFORM") { $e.WFC_PLATFORM = "XBOX360" }   # the Xbox 360 game is the specification (PC SKU: R8)
    foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] }
    return $e
}
$teamCluster = @{ "0" = $X.match_tdm.initial_clusters.value.Autobots; "1" = $X.match_tdm.initial_clusters.value.Decepticons }

# ======================================================================= DATA
foreach ($m in "frontend_flow", "frontend_gfx", "frontend_modes", "frontend_maps", "frontend_loading", "frontend_audio", "frontend_localization", "mp_map_catalog") { $p = Join-Path $AT "$m.json"; Res "DATA" "manifest.$m" $(if (Test-Path $p) { "PASS" } else { "FAIL" }) "AssetTools manifests\$m.json" "AssetTools" }
$ft = Join-Path $binDir "wfc_frontend_tests.exe"; if (-not (Test-Path $ft)) { $ft = @(Get-ChildItem (Join-Path $Root "build*\bin") -Filter wfc_frontend_tests.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName)[0] }
if ($ft -and (Test-Path $ft)) {
    $to = Join-Path $OutDir "frontend_tests.txt"; $p = Start-Process -FilePath $ft -WorkingDirectory (Split-Path $ft) -PassThru -Wait -NoNewWindow -RedirectStandardOutput $to -RedirectStandardError "$to.err"
    $txt = Get-Content $to -Raw; $fails = @([regex]::Matches($txt, '(?m)^FAIL (.*)$') | ForEach-Object { $_.Groups[1].Value.Trim() })
    $stale = @($fails | Where-Object { $_ -like "catalog.tdm_selectable_maps_now*" })
    $st = if ($p.ExitCode -eq 0) { "PASS" } elseif ($fails.Count -eq $stale.Count -and $stale.Count) { "KNOWN" } else { "FAIL" }
    Res "DATA" "frontend_unit_tests" $st ("wfc_frontend_tests exit {0}; failing: {1}{2}" -f $p.ExitCode, ($fails -join "; "), $(if ($stale.Count) { " (catalog.tdm_selectable_maps_now expects Streets only; selectable = runtime data present, and AssetTools' next-map pipeline added Gorge: STALE TEST EXPECTATION, owner Frontend)" } else { "" })) "Frontend" $p.ExitCode
} else { Res "DATA" "frontend_unit_tests" "SKIP" "wfc_frontend_tests.exe not built" "Frontend" }

# ======================================================================= R1 first launch, real menu input, first match
if ($Runs -contains "R1") {
    $d1 = Join-Path $OutDir "R1_first_launch"; New-Item -ItemType Directory -Force $d1 | Out-Null
    $s1 = @("wait:t=6", (Shot $d1 "a_intro1"), "wait:t=8", (Shot $d1 "a_intro2"), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", (Shot $d1 "b_title"),
            (K 114), "wait:t=1.2", (Shot $d1 "c_mainmenu"), (Keys @(40)), (K 13), "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=2.5", (Shot $d1 "d_party"),
            (Keys @(40)), (K 13), "wait:t=1.2", (Shot $d1 "e_modes"), (K 13), "wait:t=1.5", (Shot $d1 "f_hostoptions"),
            (K 13), "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2.5", (Shot $d1 "g_gamelobby"), "snapshot:gamelobby",
            (K 13), "wait:t=4", (Shot $d1 "h_countdown"), "wait:loading=1", "wait:t=0.8", (Shot $d1 "i_loading"),
            "wait:level=Match", "wait:t=1", (Shot $d1 "j_match_pending"), "wait:ui=InGame", "wait:t=1.5", (Shot $d1 "k_ingame0"), "wait:t=8", (Shot $d1 "l_ingame1"), "snapshot:ingame",
            "showmenu", "wait:ui=Paused", "wait:t=1.2", (Shot $d1 "m_pause"), "wait:t=1", "call:Game.QuitToMainMenu", "wait:level=FrontEnd", "wait:ui=FrontEnd", "wait:t=3", (Shot $d1 "n_return"), "snapshot:returned", "quit") -join ";"
    $r1 = Invoke-WfcSampled $exe $d1 (BaseEnv $d1 @{ WFC_FRONTEND_SCRIPT = $s1; WFC_CUELOG = "1" }) $TimeoutSec 1.0
    Write-WfcCsv $r1.samples (Join-Path $d1 "process.csv")
    $F = Read-FlowLog (Join-Path $d1 "flow.jsonl"); $L1 = Read-RunLog (Join-Path $d1 "wfc.log"); $log1 = Join-Path $d1 "wfc.log"
    if (-not $F.Count) { Res "STATE" "flow_trace" "FAIL" "no WFC_FLOWLOG trace from a frontend boot" "Frontend/Integration" }
    else {
        $to1 = @(Flow-Ev $F "timeout")
        Res "STATE" "flow_trace" $(if ($to1.Count -or $r1.timedOut) { "FAIL" } else { "PASS" }) ("{0} events, exit {1}; script timeout {2}{3}" -f $F.Count, $r1.rc, $to1.Count, $(if ($to1.Count) { " (stuck at: " + ((@(Flow-Ev $F "script.wait") | Select-Object -Last 1).cond) + ")" } else { "" })) "Frontend"
        # ---- boot / intro ----
        $b = @(Flow-Ev $F "boot")[0]
        Res "STATE" "boot_map" $(if ($b.map -eq $X.boot.map.value) { "PASS" } else { Conf $X.boot.map }) ("boot map {0}; watchedIntro {1} (fresh profile expects False)" -f $b.map, $b.watchedIntro) "Frontend"
        $lc0 = @(Flow-Ev $F "loading.close")[0]
        Res "STATE" "boot_loading_kind" $(if ($lc0.kind -eq "InitialStartup") { "PASS" } else { Conf $X.boot.initial_loading_movie }) ("first loading screen closes as {0} ({1})" -f $lc0.kind, $X.boot.initial_loading_movie.src) "Frontend"
        $plays = @(Flow-Ev $F "movie.play" | ForEach-Object { $_.movie }); $want = @($X.boot.first_launch_movies.value)
        Res "STATE" "intro_chain_order" $(if ((@($plays | Select-Object -First 4) -join ",") -eq ($want -join ",")) { "PASS" } else { Conf $X.boot.first_launch_movies }) ("movie.play: {0} (expected {1})" -f ($plays -join ","), ($want -join ",")) "Frontend"
        $un = @(Flow-Ev $F "movie.unavailable"); $ff = @(Flow-Ev $F "movie.firstFrame"); $fin = @(Flow-Ev $F "movie.finished")
        $introFF = @($want | Where-Object { $m = $_; @($ff | Where-Object { $_.movie -like "$m*" }).Count })
        Res "PRESENTED" "intro_movies_decoded" $(if ($un.Count) { "FAIL" } elseif ($introFF.Count -eq $want.Count) { "PASS" } else { "FAIL" }) ("intro movies with a decoded first frame: {0}/{1}; finished events {2}; unavailable {3} {4}" -f $introFF.Count, $want.Count, $fin.Count, $un.Count, (($un | ForEach-Object { "$($_.movie): $($_.why)" }) -join "; ")) "Frontend"
        $skipped = @($fin | Where-Object { "$($_.skipped)" -eq "True" })
        Res "STATE" "intro_played_to_end" $(if ($fin.Count -ge 4 -and -not $skipped.Count) { "PASS" } elseif (-not $fin.Count) { "SKIP" } else { "INFO" }) ("movie.finished: " + (($fin | ForEach-Object { "$($_.movie) pos $($_.position) skipped $($_.skipped)" }) -join "; ")) "Frontend"
        $sa = Shot-Stats (Join-Path $d1 "a_intro1.bmp"); $sb = Shot-Stats (Join-Path $d1 "a_intro2.bmp"); $dIntro = Shot-Diff (Join-Path $d1 "a_intro1.bmp") (Join-Path $d1 "a_intro2.bmp")
        Res "PRESENTED" "intro_on_screen" $(if (-not $sa) { "SKIP" } elseif ($sa.black -lt 0.97 -or $sb.black -lt 0.97) { "HUMAN" } else { "FAIL" }) ("frames at 6 s / 14 s after boot: near-black {0:P0} / {1:P0}, mean luma {2} / {3}, change {4}. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check" -f $sa.black, $sb.black, $sa.mean, $sb.mean, $dIntro) "Frontend"
        $mute = @(Flow-Ev $F "audio.moviePlaying")
        Res "AUDIO" "movie_mute" $(if (-not $mute.Count) { "SKIP" } elseif (@($mute | Where-Object { "$($_.playing)" -eq "True" -or "$($_.value)" -eq "True" -or "$($_.on)" -eq "True" }).Count -and @($mute | Where-Object { "$($_.playing)" -eq "False" -or "$($_.value)" -eq "False" -or "$($_.on)" -eq "False" }).Count) { "PASS" } else { "INFO" }) ("audio.moviePlaying events {0}: {1} (CINE_MUTE_FOR_BINK while a Bink plays: {2})" -f $mute.Count, (($mute | Select-Object -First 6 | ForEach-Object { ($_.PSObject.Properties | Where-Object { $_.Name -notin 't', 'seq', 'ev' } | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join " " }) -join " | "), $X.audio_ownership.movie_mute_preset.src) "Systems/Frontend"
        # ---- UI controller ----
        $legal = @{}; foreach ($t in $X.ui_controller.transitions) { $legal["$($t[0])>$($t[1])"] = $true }
        $bad = @(); foreach ($u in (Flow-Ev $F "ui.state")) { $k = "$($u.from)>$($u.to)"; if (-not $legal[$k] -and -not ($u.from -eq "NotInGame")) { $bad += "$k@$($u.level)" } }
        Res "STATE" "ui_transitions_legal" $(if (-not $bad.Count) { "PASS" } else { "FAIL" }) ("{0} UI state changes; outside the TnUIController table: {1}" -f @(Flow-Ev $F "ui.state").Count, ($bad -join ", ")) "Frontend"
        $opened = @(@(Flow-Ev $F "ui.open") + @(Flow-Ev $F "gfx.movie" | Where-Object { "$($_.opened)" -eq "True" }) | ForEach-Object { $_.movie } | Select-Object -Unique)
        foreach ($st in "FrontEnd", "InLobby@PartyLobby", "InLobby@GameLobby", "Paused") { $w = $X.ui_controller.movie_for_state.$st; Res "STATE" "movie_for.$st" $(if ($opened -contains $w) { "PASS" } else { "FAIL" }) "expected $w opened (RE 1.4)" "Frontend" }
        $dupOpen = @(); $open = @{}
        foreach ($e in $F) { if ($e.ev -eq "gfx.movie" -and "$($e.opened)" -eq "True") { if ($open[$e.movie]) { $dupOpen += $e.movie }; $open[$e.movie] = $true } elseif ($e.ev -eq "gfx.movieClosed") { $open.Remove($e.movie) } }
        Res "STATE" "no_duplicate_screens" $(if (-not $dupOpen.Count) { "PASS" } else { "FAIL" }) ("GFx movies opened again while open: " + ($dupOpen -join ", ")) "Frontend"
        # ---- real-input navigation (keys -> the movies' own ActionScript -> bridge) ----
        $br = @(Flow-Ev $F "bridge")
        $opl = @($br | Where-Object fn -eq "Online.OpenPartyLobby")[0]
        Res "STATE" "key_multiplayer" $(if ($opl.args -eq $X.travel.multiplayer_gts.value) { "PASS" } else { "FAIL" }) ("Start, Down, A on the main menu -> Online.OpenPartyLobby({0}) (BLK A2: GTS_TeamGame)" -f $opl.args) "Frontend"
        $egm = @($br | Where-Object fn -eq "Online.EditGameMode")
        Res "STATE" "key_mode_focus_tdm" $(if ($egm.Count -and $egm[0].args -eq "TDM") { "PASS" } else { Conf $X.travel.mode_list_initial_focus }) ("first EditGameMode on entering the mode list: {0} (BLK B2: initial focus TDM, EditGameMode on focus)" -f $(if ($egm.Count) { $egm[0].args } else { "none" })) "Frontend"
        $sw = @(Flow-Ev $F "settings.write"); $def = $X.travel.host_option_defaults.value
        $got = @{}; foreach ($w in $sw) { $got[$w.field] = "$($w.value)" }
        $okDef = ($got.AutobalanceTeams -eq $def.TeamBalancing) -and ($got.MapSelectionMethod -eq $def.MapSelection) -and ($got.TimeLimit -like "$($def.TimeLimit)*") -and ($got.PointsToWin -eq $def.PointsToWin)
        Res "STATE" "host_option_defaults" $(if (-not $sw.Count) { "SKIP" } elseif ($okDef) { "PASS" } else { "FAIL" }) ("Create Game with untouched rows wrote: " + (($sw | ForEach-Object { "$($_.field)=$($_.value)" }) -join ", ") + " (BLK B4: Autobalanced / Host's Choice / 15 minutes / 40)") "Frontend"
        $gm = @(Flow-Ev $F "gamelobby.map"); $firstSel = @($br | Where-Object fn -eq "Online.SetSelectedMapID")[0]
        $selectable = @($X.travel.tdm_map_order.value | Where-Object { $id = $_; @($gm | Where-Object { [int]$_.mapId -eq $id -and "$($_.hasRequiredAssets)" -eq "True" }).Count -or $id -eq 508 })
        Res "STATE" "lobby_default_map" $(if ($firstSel -and [int]$firstSel.args -eq [int]$selectable[0]) { "PASS" } elseif (-not $firstSel) { "FAIL" } else { "FAIL" }) ("game lobby opened: server pick {0}, then the lobby movie pushed SetSelectedMapID({1}); expected the first selectable map in TransLevels order (BLK C4: index 0; rebuild-selectable maps {2} -> {3}). Original dump: Seed of Corruption 501 is index 0; the rebuild only lists maps with runtime data" -f $(if ($gm.Count) { $gm[0].mapId } else { "-" }), $firstSel.args, (($X.travel.tdm_map_order.value) -join ","), $selectable[0]) "Frontend"
        $cd = @(Flow-Ev $F "gamelobby.countdown")[0]; $fc = @(Flow-Ev $F "gamelobby.finalCountdown")[0]; $span = if ($cd -and $fc) { [double]$fc.t - [double]$cd.t } else { -1 }
        Res "STATE" "lobby_countdown" $(if ([int]$cd.seconds -eq 10 -and (Near $span 10 1.0)) { "PASS" } else { "FAIL" }) ("A on Start Game -> countdown {0} s, final countdown after {1:F2} s (BLK C8: 10 s)" -f $cd.seconds, $span) "Frontend"
        $mu = (@(Flow-Ev $F "gamelobby.startLevel")[0]).url; $pu = Parse-Url $mu
        $badK = @($X.travel.match_url_keys.value.PSObject.Properties | Where-Object { $pu.keys[$_.Name] -ne $_.Value } | ForEach-Object { "$($_.Name)=$($pu.keys[$_.Name])" })
        Res "STATE" "match_url" $(if ($mu -eq $X.travel.match_url.value) { "PASS" } elseif (-not $badK.Count) { "KNOWN" } else { "FAIL" }) ("{0}; mismatched keys: {1} (RE 3.1 / BLK D1)" -f $mu, ($badK -join ", ")) "Frontend"
        $lm = @(Flow-Ev $F "loading.start" | Where-Object kind -eq "Map")[0]
        Res "STATE" "match_loading_text" $(if ($lm.title -eq $X.loading.match_title.value -and $lm.message -eq $X.loading.match_message.value -and [int]$lm.tips -eq 3) { "PASS" } else { "FAIL" }) ("title '{0}' message '{1}' tips {2}" -f $lm.title, $lm.message, $lm.tips) "Frontend"
        $ml = @(Flow-Ev $F "match.launch")[0]
        Res "OWNERSHIP" "launch_request" $(if ($ml.mode -eq "TDM" -and [int]$ml.goalScore -eq 40 -and [int]$ml.timeLimit -eq 900 -and $ml.runtimeDir -eq "MP_IAC_Streets") { "PASS" } else { "FAIL" }) ("frontend MatchLaunch: mode {0} goal {1} time {2} runtimeDir {3} team {4}" -f $ml.mode, $ml.goalScore, $ml.timeLimit, $ml.runtimeDir, $ml.team) "Frontend"
        $mg = @(Flow-Ev $F "match.gameplay")[0]; $al = @(Flow-Ev $F "audio.loaded")[0]
        if ($mg) {
            $okG = $mg.mode -eq $ml.mode -and [int]$mg.goalScore -eq [int]$ml.goalScore -and [int]$mg.timeLimit -eq [int]$ml.timeLimit -and $mg.map -eq $ml.runtimeDir
            Res "OWNERSHIP" "gameplay_match_settings" $(if ($okG) { "PASS" } else { "FAIL" }) ("Gameplay launched map {0} mode {1} goal {2} time {3}; frontend selection runtimeDir {4} mode {5} goal {6} time {7} (RE D1-D3: from the URL)" -f $mg.map, $mg.mode, $mg.goalScore, $mg.timeLimit, $ml.runtimeDir, $ml.mode, $ml.goalScore, $ml.timeLimit) "Integration/Gameplay"
            Res "MATCH" "default_rules_tdm" $(if ([int]$mg.goalScore -eq $X.match_tdm.goal_score.value -and [int]$mg.timeLimit -eq $X.match_tdm.time_limit_s.value) { "PASS" } else { "FAIL" }) ("default host options -> Gameplay goal {0} / time {1} s (BLK B4/D2: 40 / 900)" -f $mg.goalScore, $mg.timeLimit) "Gameplay/Frontend"
        }
        if ($al) { Res "OWNERSHIP" "audio_map_follows_selection" $(if ("$($al.level)" -like "*$($ml.runtimeDir)*" -or "$($al.level)" -like "*Streets*") { "PASS" } else { "FAIL" }) ("Systems loaded level audio '{0}' for the selected map {1}" -f $al.level, $ml.runtimeDir) "Systems/Integration" }
        # ---- MATCH: Gameplay's execution of the request ----
        $gLaunch = @(Grep-Log $log1 'match: launched'); $gBegin = @(Grep-Log $log1 'match: \S+ begin \(goal'); $gSpawn = @(Grep-Log $log1 'match: local player spawned at')
        if (-not $gLaunch.Count -and -not $gBegin.Count) {
            Res "OWNERSHIP" "gameplay_runs_request" $(if (@(Flow-Ev $F "adapter").Count) { "FAIL" } else { "FAIL" }) ("no Gameplay match launch in the frontend-launched match (no 'match: launched' / 'match: <mode> begin'). {0}" -f $(if (@(Flow-Ev $F "adapter").Count) { "The frontend still uses the PROVISIONAL adapter (no PendingMatch): the TDM rules are not running behind the UI" } else { "" })) "Integration/Gameplay"
        } else {
            $gl = [regex]::Match($gLaunch[0].text, 'match: launched (\S+) (\S+) \(goal (\d+), time (\d+) s\)')
            $okL = $gl.Success -and $gl.Groups[2].Value -eq $ml.mode -and [int]$gl.Groups[3].Value -eq [int]$ml.goalScore -and [int]$gl.Groups[4].Value -eq [int]$ml.timeLimit
            Res "OWNERSHIP" "gameplay_runs_request" $(if ($okL) { "PASS" } else { "FAIL" }) ("Gameplay: '{0}' vs frontend request mode {1} goal {2} time {3}" -f ($gLaunch[0].text -replace '^.*match: ', ''), $ml.mode, $ml.goalScore, $ml.timeLimit) "Integration/Gameplay"
        }
        $pre = @(Flow-Ev $F "ui.open" | Where-Object { $_.movie -like "*PreGameCountdown*" }) + @(Flow-Ev $F "gfx.movie" | Where-Object { $_.movie -like "*PreGameCountdown*" -and "$($_.opened)" -eq "True" })
        $lvlM = @(Flow-Ev $F "level.begin" | Where-Object level -eq "Match")[0]; $ing = @(Flow-Ev $F "ui.state" | Where-Object to -eq "InGame")[0]
        $pend = if ($lvlM -and $ing) { [double]$ing.t - [double]$lvlM.t } else { -1 }
        $ad = @(Flow-Ev $F "adapter")
        Res "MATCH" "pending_countdown" $(if (Near $pend 10 1.5) { "PASS" } elseif ($ad.Count) { "FAIL" } else { "FAIL" }) ("match level -> InGame after {0:F2} s (BLK D4 / RE 5.2: PendingMatch 10 s, PreGameCountdown shown {1}){2}" -f $pend, $(if ($pre.Count) { "yes" } else { "no" }), $(if ($ad.Count) { "; PROVISIONAL adapter still in the path: " + $ad[0].what } else { "" })) "Gameplay/Frontend"
        if ($gSpawn.Count) {
            $sp = [regex]::Match($gSpawn[0].text, 'spawned at (\S+) \((\S+) team (\d+)\)'); $iIn = Flow-LogIndex $L1 "ui.state" 0
            $inGameIdx = @($L1 | Where-Object { $_.kind -eq "flow" -and $_.text -match "to=InGame" } | Select-Object -First 1).i
            $before = $inGameIdx -and $gSpawn[0].i -lt (@($L1 | Where-Object { $_.kind -eq "flow" -and $_.text -match "FLOW level\.begin.*level=Match" } | Select-Object -First 1).i)
            Res "MATCH" "no_spawn_before_start" $(if (-not $before) { "PASS" } else { "FAIL" }) ("first spawn log line {0} vs match level begin / InGame (BLK E5: no spawns in PendingMatch)" -f $gSpawn[0].i) "Gameplay"
            $team = $sp.Groups[3].Value; $clu = $sp.Groups[2].Value
            Res "MATCH" "spawn_team_cluster" $(if ($sp.Success -and $team -in "0", "1" -and $clu -eq $teamCluster[$team]) { "PASS" } elseif ($sp.Success) { "FAIL" } else { "INFO" }) ("spawned at {0} in {1}, team {2} (RE 5.3 / BLK E6: initial clusters Autobots 7810, Decepticons 4159, locked 15 s)" -f $sp.Groups[1].Value, $clu, $team) "Gameplay"
            Res "MATCH" "spawn_class" $(if ($sp.Groups[1].Value -like "TnTeamPlayerStart*") { "PASS" } else { "FAIL" }) ("start actor {0} (TnTeamPlayerStart in TDM)" -f $sp.Groups[1].Value) "Gameplay"
            Res "OWNERSHIP" "team_consistent" $(if ("$($ml.team)" -eq $team) { "PASS" } else { "INFO" }) ("frontend launch team {0}, Gameplay spawn team {1} (BLK E2/E4: offline the lobby assigns no team; the match PickTeam decides)" -f $ml.team, $team) "Integration"
        } else { Res "MATCH" "spawn_team_cluster" $(if ($gBegin.Count) { "FAIL" } else { "SKIP" }) "no 'match: local player spawned' line" "Gameplay" }
        # ---- PRESENTED: what the player saw at each stage ----
        $stages = [ordered]@{ b_title = "title"; c_mainmenu = "main menu"; d_party = "party lobby"; e_modes = "mode list"; f_hostoptions = "host options"; g_gamelobby = "game lobby"; h_countdown = "lobby countdown"; i_loading = "loading screen"; j_match_pending = "match pre-game"; k_ingame0 = "in game"; l_ingame1 = "in game +8 s"; m_pause = "pause menu"; n_return = "returned frontend" }
        $rows = @(); foreach ($k in $stages.Keys) { $s = Shot-Stats (Join-Path $d1 "$k.bmp"); if ($s) { $rows += [pscustomobject]@{ stage = $stages[$k]; file = $s.file; mean = $s.mean; black = $s.black; flat = $s.flat } } }
        Write-WfcCsv $rows (Join-Path $d1 "shots.csv")
        if ($rows.Count) { $tiles = @(Get-ChildItem $d1 -Filter *.bmp | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = $_.BaseName } }); New-WfcSheet $tiles (Join-Path $OutDir "R1_sheet.png") 4 400 225 }
        foreach ($row in $rows) {
            if ($row.stage -like "in game*") { continue }
            Res "PRESENTED" ("screen." + ($row.stage -replace '\W', '_')) $(if ($row.black -ge 0.97) { "FAIL" } else { "HUMAN" }) ("{0}: near-black {1:P0}, mean luma {2}{3}" -f $row.file, $row.black, $row.mean, $(if ($row.black -ge 0.97) { " - nothing on screen" } else { " - drawn; whether it looks like WFC is a human check (R1_sheet.png)" })) $(if ($row.black -ge 0.97) { "Frontend" } else { "" })
        }
        $g0 = Shot-Stats (Join-Path $d1 "k_ingame0.bmp"); $g1 = Shot-Stats (Join-Path $d1 "l_ingame1.bmp"); $gd = Shot-Diff (Join-Path $d1 "k_ingame0.bmp") (Join-Path $d1 "l_ingame1.bmp")
        if ($g0) {
            $worldOk = $g0.black -lt 0.85 -and $g1.black -lt 0.85
            Res "PRESENTED" "in_game_world_visible" $(if ($worldOk) { "PASS" } else { "FAIL" }) ("in-game frames (trace: InGame, HUD on): near-black {0:P0} / {1:P0}, mean luma {2} / {3}{4}" -f $g0.black, $g1.black, $g0.mean, $g1.mean, $(if (-not $worldOk) { " - the player sees no world although the trace says InGame (k_ingame0.bmp)" } else { "" })) "Integration/Rendering/Frontend"
            Res "PRESENTED" "in_game_live" $(if ($gd -gt 2) { "PASS" } else { "FAIL" }) ("frames 8 s apart with walk + turn input differ by {0} luma (frozen view if ~0)" -f $gd) "Integration"
            $pz = Shot-Diff (Join-Path $d1 "l_ingame1.bmp") (Join-Path $d1 "m_pause.bmp")
            Res "PRESENTED" "pause_menu_drawn" $(if ($pz -gt 4) { "HUMAN" } else { "FAIL" }) ("pause frame differs from the game frame by {0} luma (menu over the running match; backdrop blend PARTIAL per Frontend)" -f $pz) "Frontend"
        }
        Res "PRESENTED" "pause_input_focus" "HUMAN" "hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop)" "Frontend"
        # ---- quit / return ----
        $tq = @(Flow-Ev $F "travel" | Where-Object { $_.from -eq "Match" })[0]; $ret = @(Flow-Ev $F "snapshot" | Where-Object why -eq "returned")[0]
        Res "STATE" "quit_to_main_menu" $(if ($tq.url -eq $X.travel.quit_to_main_menu.value -and $ret -and $ret.uiState -eq "FrontEnd") { "PASS" } else { "FAIL" }) ("travel {0}; returned uiState {1}; intro replayed after return: {2}" -f $tq.url, $ret.uiState, @(Flow-Ev $F "movie.play" | Where-Object { [int]$_.seq -gt [int]$tq.seq }).Count) "Frontend"
        $dTitle = Shot-Diff (Join-Path $d1 "n_return.bmp") (Join-Path $d1 "b_title.bmp"); $dMain = Shot-Diff (Join-Path $d1 "n_return.bmp") (Join-Path $d1 "c_mainmenu.bmp")
        if ($dTitle -ge 0 -and $dMain -ge 0) { Res "STATE" "return_lands_on_main_menu" $(if ($dMain -lt $dTitle) { "PASS" } else { "FAIL" }) ("frontend after the match resembles the main menu (diff {0}) rather than Press START (diff {1}) (Online.ShouldShowStartScreen: true only until ShowDeviceSelectionUI, Frontend 76b8287 from the recovered script)" -f $dMain, $dTitle) "Frontend" }
        Res "LIFETIME" "state_reset_after_return" $(if ($ret -and $ret.mode -eq "" -and [int]$ret.mapId -eq -1 -and "$($ret.hud)" -eq "False") { "PASS" } elseif (-not $ret) { "SKIP" } else { "FAIL" }) ("returned snapshot: mode '{0}' mapId {1} team {2} hud {3}" -f $ret.mode, $ret.mapId, $ret.team, $ret.hud) "Frontend"
        # ---- AUDIO in R1 ----
        $music = @(Grep-Log $log1 '\] MUSIC (play|stop|queued) ')
        $lvlIdx = @($L1 | Where-Object { $_.kind -eq "flow" -and $_.ev -eq "level.begin" } | ForEach-Object { [pscustomobject]@{ i = $_.i; level = [regex]::Match($_.text, 'level=(\S+)').Groups[1].Value } })
        function LevelAt($i) { $l = "Boot"; foreach ($x in $lvlIdx) { if ($x.i -lt $i) { $l = $x.level } }; return $l }
        $mPlays = @($music | Where-Object { $_.text -match 'MUSIC play (\S+)' } | ForEach-Object { [pscustomobject]@{ level = (LevelAt $_.i); cue = [regex]::Match($_.text, 'MUSIC play (\S+)').Groups[1].Value } })
        if (-not $music.Count) { Res "AUDIO" "level_music" "SKIP" "no MUSIC log lines (WFC_MUSICLOG): Systems music not wired into the frontend path" "Systems/Frontend" }
        else {
            $exp = [ordered]@{ FrontEnd = $X.audio_ownership.frontend_music.value; PartyLobby = $X.audio_ownership.party_lobby_music.value; GameLobby = $X.audio_ownership.game_lobby_music.value }
            foreach ($lv in $exp.Keys) { $p = @($mPlays | Where-Object level -eq $lv); $visits = @($lvlIdx | Where-Object level -eq $lv).Count
                # cue names may be package-qualified (BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01): the authored cue is the
                # object name; the package prefix is how Systems logs it (stale exact-match expectation retired 2026-10-04)
                $wrong = @($p | Where-Object { $_.cue -ne $exp[$lv] -and $_.cue -notlike "*.$($exp[$lv])" })
                Res "AUDIO" "music.$lv" $(if ($p.Count -eq $visits -and -not $wrong.Count) { "PASS" } else { "FAIL" }) ("{0} visits, music plays: {1} (expected {2} once per visit; package prefix accepted)" -f $visits, (($p | ForEach-Object { $_.cue }) -join ","), $exp[$lv]) "Systems" }
            $mm = @($mPlays | Where-Object level -eq "Match")
            Res "AUDIO" "no_music_in_match" $(if (-not $mm.Count) { "PASS" } else { "FAIL" }) ("music plays during the Streets match: {0} (Streets authors no SeqAct_PlayMusic)" -f (($mm | ForEach-Object { $_.cue }) -join ",")) "Systems"
        }
        $us = @(Flow-Ev $F "ui.sound")
        if ($us.Count) {
            $silent = @($us | Where-Object { $_.audio -eq "none" }); $names = @($us | ForEach-Object { $_.name } | Select-Object -Unique)
            $bad = @($names | Where-Object { $_ -notin $X.travel.menu_sounds.value -and $_ -ne "BUTTON_START" })
            Res "AUDIO" "ui_sounds_played" $(if (-not $silent.Count) { "PASS" } else { "FAIL" }) ("{0} UI sounds requested by the movies ({1}); without an audio owner (audio=none): {2}" -f $us.Count, ($names -join ","), $silent.Count) "Systems/Frontend"
            $dbl = 0; for ($k = 1; $k -lt $us.Count; $k++) { if ($us[$k].name -eq $us[$k - 1].name -and [Math]::Abs([double]$us[$k].t - [double]$us[$k - 1].t) -lt 0.02) { $dbl++ } }
            Res "AUDIO" "ui_sounds_single" $(if (-not $dbl) { "PASS" } else { "FAIL" }) ("same UI sound requested twice within 20 ms: {0}" -f $dbl) "Frontend"
        }
        $amb1 = @(Read-Amb $log1)
        $dc = @(Find-DoubledCues $log1)
        Res "AUDIO" "doubled_sounds_R1" $(if (-not (Has "WFC_CUELOG")) { "SKIP" } elseif (-not $dc.Count) { "PASS" } else { "FAIL" }) ("cue instances started twice at the same place and moment: {0} {1}" -f $dc.Count, (($dc | Group-Object cue | Sort-Object Count -Descending | Select-Object -First 6 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", ")) "Systems"
        $mA = @($amb1 | Where-Object { $_.map })
        if ($mA.Count) { Res "OWNERSHIP" "audio_level" $(if (@($mA | Where-Object { $_.map -like "*Streets*" }).Count) { "PASS" } else { "FAIL" }) ("Systems AMB level during the match: " + (($mA | ForEach-Object { $_.map } | Select-Object -Unique) -join ",")) "Systems/Integration" }
        $rl = @(Grep-Log $log1 'render data|loadMapRenderData|Maps[\\/]MP_')
        Res "OWNERSHIP" "render_map" $(if (@($rl | Where-Object { $_.text -match 'MP_IAC_Streets' }).Count) { "PASS" } else { "INFO" }) ("render log lines naming the map: " + (($rl | Select-Object -First 2 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | ")) "Rendering"
    }
}

# ======================================================================= R2 second launch
if ($Runs -contains "R2" -and (Test-Path (Join-Path $OutDir "R1_first_launch\wfc_profile.ini"))) {
    $d2 = Join-Path $OutDir "R2_second_launch"; New-Item -ItemType Directory -Force $d2 | Out-Null
    Copy-Item (Join-Path $OutDir "R1_first_launch\wfc_profile.ini") $d2
    $r2 = Invoke-WfcSampled $exe $d2 (BaseEnv $d2 @{ WFC_FRONTEND_SCRIPT = ("wait:frontend;wait:ui=FrontEnd;wait:t=2;" + (Shot $d2 "title") + ";snapshot:menu;quit") }) 300 1.0
    $F2 = Read-FlowLog (Join-Path $d2 "flow.jsonl")
    $b2 = @(Flow-Ev $F2 "boot")[0]; $p2 = @(Flow-Ev $F2 "movie.play" | Where-Object { $_.movie -in $X.boot.first_launch_movies.value })
    Res "STATE" "second_launch_profile" $(if ("$($b2.watchedIntro)" -eq "True") { "PASS" } else { "FAIL" }) "watchedIntro=$($b2.watchedIntro) with the first launch's profile" "Frontend"
    Res "STATE" "second_launch_skips_intro" $(if (-not $p2.Count -and @(Flow-Ev $F2 "ui.state" | Where-Object to -eq "FrontEnd").Count) { "PASS" } else { Conf $X.boot.second_launch_skips_logos }) ("intro movies on the second launch: {0}; title reached" -f $p2.Count) "Frontend"
}

# ======================================================================= R3 full TDM lifecycle (time limit -> end -> lobby -> 2nd match -> frontend)
if ($Runs -contains "R3") {
    $d3 = Join-Path $OutDir "R3_full_match"; New-Item -ItemType Directory -Force $d3 | Out-Null
    $s3 = @((Path-ToHostOptions 0), (Path-CreateGame -TenMinutes), (Shot $d3 "a_lobby"), (Path-StartGame), "wait:level=Match", "wait:ui=InGame", "wait:t=5", (Shot $d3 "b_ingame"),
            "wait:t=280", (Shot $d3 "c_midmatch"), "wait:ui=GameEnded", "wait:t=2", (Shot $d3 "d_endstats"), "snapshot:ended",
            "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2.5", (Shot $d3 "e_lobby_after"), "snapshot:lobby2",
            (Path-StartGame), "wait:level=Match", "wait:ui=InGame", "wait:t=8", (Shot $d3 "f_match2"), "snapshot:match2",
            "showmenu", "wait:ui=Paused", "wait:t=1", "call:Game.QuitToMainMenu", "wait:level=FrontEnd", "wait:ui=FrontEnd", "wait:t=3", (Shot $d3 "g_return"), "snapshot:returned", "quit") -join ";"
    $r3 = Invoke-WfcSampled $exe $d3 (BaseEnv $d3 @{ WFC_FRONTEND_SCRIPT = $s3; WFC_SKIPINTRO = "1"; WFC_AUTOFIRE = "1" }) 1800 2.0
    Write-WfcCsv $r3.samples (Join-Path $d3 "process.csv")
    $F3 = Read-FlowLog (Join-Path $d3 "flow.jsonl"); $log3 = Join-Path $d3 "wfc.log"
    $to3 = @(Flow-Ev $F3 "timeout"); $lastWait = (@(Flow-Ev $F3 "script.wait") | Select-Object -Last 1).cond
    Res "MATCH" "full_lifecycle_completed" $(if (@(Flow-Ev $F3 "snapshot" | Where-Object why -eq "returned").Count) { "PASS" } else { "FAIL" }) ("match to the time limit -> end -> lobby -> second match -> frontend; {0}" -f $(if ($to3.Count -or $r3.timedOut) { "stopped after '$lastWait'" } else { "completed" })) "Gameplay/Frontend/Integration"
    $tlw = @(Flow-Ev $F3 "settings.write" | Where-Object field -eq "TimeLimit")[0]; $ml3 = @(Flow-Ev $F3 "match.launch")
    Res "OWNERSHIP" "host_option_to_rules" $(if ($ml3.Count -and [int]$ml3[0].timeLimit -eq 600) { "PASS" } else { "FAIL" }) ("Time Limit row set to '{0}' with the keys; match.launch timeLimit {1}; URL TimeLimit {2}" -f $tlw.value, $(if ($ml3.Count) { $ml3[0].timeLimit }), $(if ($ml3.Count) { (Parse-Url $ml3[0].url).keys.TimeLimit })) "Frontend"
    $gl3 = @(Grep-Log $log3 'match: launched'); $end3 = @(Grep-Log $log3 'match: EndGame')
    if ($gl3.Count) { Res "MATCH" "time_limit_passed_to_rules" $(if ($gl3[0].text -match 'time 600 s') { "PASS" } else { "FAIL" }) ($gl3[0].text -replace '^.*match: ', '') "Gameplay/Integration" }
    $ing3 = @(Flow-Ev $F3 "ui.state" | Where-Object to -eq "InGame")[0]; $ge3 = @(Flow-Ev $F3 "ui.state" | Where-Object to -eq "GameEnded")[0]
    $dur = if ($ing3 -and $ge3) { [double]$ge3.t - [double]$ing3.t } else { -1 }
    Res "MATCH" "time_limit_end" $(if (Near $dur 600 4) { "PASS" } elseif ($dur -lt 0) { "FAIL" } else { "FAIL" }) ("InGame -> GameEnded after {0:F1} s (TimeLimit 600 s; BLK F3 RemainingTime 0 -> EndGame)" -f $dur) "Gameplay"
    $mi3 = @(Grep-Log $log3 '\] MATCH spawn player=(\d+) team=(\d)' | ForEach-Object { [regex]::Match($_.text, 'team=(\d)').Groups[1].Value } | Select-Object -Unique)
    if ($end3.Count) { $em = [regex]::Match($end3[0].text, 'reason "([^"]*)" score (\d+)-(\d+) winner team (-?\d+)'); $oneTeam = $mi3.Count -eq 1
        $exp3 = if ($oneTeam) { [int]$mi3[0] } elseif ($em.Groups[2].Value -eq $em.Groups[3].Value) { -1 } else { $null }
        Res "MATCH" "time_limit_winner" $(if (-not $em.Success) { "FAIL" } elseif ($exp3 -ne $null -and [int]$em.Groups[4].Value -eq $exp3) { "PASS" } else { "FAIL" }) ("{0}; populated teams {1}. TnVersusGame.PickWinningTeam: a team wins if it scores more OR the other team is empty; equal scores with both teams populated = no winner, no overtime (BLK F3) -> expected winner {2}" -f ($end3[0].text -replace '^.*match: ', ''), ($mi3 -join ","), $exp3) "Gameplay" }
    $pe3 = @(Grep-Log $log3 '\] MATCH end reason=(\S+)')
    if ($pe3.Count) { Res "MATCH" "protocol_end_reason" $(if ($pe3[0].text -match 'reason=time_limit') { "PASS" } else { "INFO" }) ("MATCH protocol line: " + ($pe3[0].text -replace '^.*MATCH ', '') + " (RUNTIME-EVENTS expects reason=time_limit for a clock end; Integration maps Gameplay's empty EndGame reason to 'other' - validation-hook nit)") "Integration" }
    $hudOff = @(Flow-Ev $F3 "ui.hud" | Where-Object { "$($_.visible)" -eq "False" -and $ge3 -and [double]$_.t -ge [double]$ge3.t - 0.5 })
    $endMovie = @(@(Flow-Ev $F3 "gfx.movie") + @(Flow-Ev $F3 "ui.open") | Where-Object { $_.movie -like "*EndGameStats*" })
    Res "MATCH" "match_end_ui" $(if ($ge3 -and $hudOff.Count -and $endMovie.Count) { "PASS" } else { "FAIL" }) ("GameEnded {0}; HUD hidden {1}; EndGameStats opened {2} (BLK F4: event 9, HUD hidden, EndGameStats_GFX)" -f [bool]$ge3, $hudOff.Count, $endMovie.Count) "Gameplay/Frontend"
    $trL = @(Flow-Ev $F3 "travel" | Where-Object { $_.from -eq "Match" -and $_.url -like "UI_Lobby_m*" })[0]
    $over = if ($ge3 -and $trL) { [double]$trL.t - [double]$ge3.t } else { -1 }
    Res "MATCH" "match_over_return_to_lobby" $(if ($trL -and (Near $over 15 2) -and (Parse-Url $trL.url).keys.MapId -eq "508") { "PASS" } else { "FAIL" }) ("travel {0} after {1:F1} s (BLK F4/F6: MatchOver 15 s -> ReturnToGameLobby UI_Lobby_m?...?MapId=)" -f $trL.url, $over) "Gameplay/Frontend"
    $sh = Shot-Stats (Join-Path $d3 "d_endstats.bmp")
    if ($sh) { Res "PRESENTED" "end_stats_screen" $(if ($sh.black -ge 0.97) { "FAIL" } else { "HUMAN" }) ("end-of-match screen: near-black {0:P0} - results layout (columns Level / Name / Score / Kills / Deaths, BLK F5) is a human check" -f $sh.black) "Frontend" }
    $l2 = @(Flow-Ev $F3 "snapshot" | Where-Object why -eq "lobby2")[0]
    Res "STATE" "lobby_after_match" $(if ($l2 -and $l2.level -eq "GameLobby") { if ([int]$l2.mapId -eq 508) { "PASS" } else { "INFO" } } else { "FAIL" }) ("back in the game lobby; selected map {0} (BLK C7 HIGH: the lobby movie re-sends selector index 0 = first selectable = Streets in the rebuild)" -f $l2.mapId) "Frontend"
    Res "MATCH" "second_match" $(if ($ml3.Count -ge 2 -and [int]$ml3[1].timeLimit -eq 600 -and @(Flow-Ev $F3 "snapshot" | Where-Object why -eq "match2").Count) { "PASS" } else { "FAIL" }) ("second launch: {0} (host options persist for the session, HIGH: TimeLimit 600 expected)" -f $(if ($ml3.Count -ge 2) { "mode $($ml3[1].mode) goal $($ml3[1].goalScore) time $($ml3[1].timeLimit)" } else { "not reached" })) "Frontend/Gameplay"
    $b2 = @(Grep-Log $log3 'match: \S+ begin'); $sp3 = @(Grep-Log $log3 'match: local player spawned')
    Res "MATCH" "second_match_fresh" $(if ($b2.Count -ge 2 -and $sp3.Count -ge 2) { "PASS" } elseif ($b2.Count -lt 2) { "FAIL" } else { "FAIL" }) ("match begins {0}, local spawns {1} (BLK F6: a new match is a fresh level load)" -f $b2.Count, $sp3.Count) "Gameplay"
    $ann = @(Grep-Log $log3 '(?i)announce|GameProgress|TimeAnnouncement')
    Res "MATCH" "time_announcements" $(if ($ann.Count -ge 3) { "INFO" } else { "SKIP" }) ("announcement lines: {0} (RE: at 120 / 60 / 30 s remaining; audible check in HUMAN-CHECK)" -f $ann.Count) "Gameplay/Systems"
    $dc3 = @(Find-DoubledCues $log3)
    Res "AUDIO" "doubled_sounds_full_match" $(if (-not (Has "WFC_CUELOG")) { "SKIP" } elseif (-not $dc3.Count) { "PASS" } else { "FAIL" }) ("doubled cue starts over a 10-minute match with constant fire: {0} {1}" -f $dc3.Count, (($dc3 | Group-Object cue | Sort-Object Count -Descending | Select-Object -First 6 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", ")) "Systems"
    $amb3 = @(Read-Amb $log3 | Where-Object { $_.live -ne $null })
    if ($amb3.Count -gt 20) {
        $q1 = $amb3[[int]($amb3.Count * 0.25)]; $q4 = $amb3[[int]($amb3.Count * 0.9)]
        Res "AUDIO" "voices_stable_in_match" $(if ($q4.live -le $q1.live * 1.5 + 10) { "PASS" } else { "FAIL" }) ("live cue instances 25% / 90% through the match: {0} / {1}; backend voices {2} / {3}; PCM {4} / {5} MB" -f $q1.live, $q4.live, $q1.backendVoices, $q4.backendVoices, $q1.pcm, $q4.pcm) "Systems"
    }
}

# ======================================================================= R4 repeated frontend <-> Streets cycles (lifetime)
if ($Runs -contains "R4") {
    $d4 = Join-Path $OutDir "R4_cycles"; New-Item -ItemType Directory -Force $d4 | Out-Null
    $one = "wait:frontend;wait:ui=FrontEnd;wait:t=2;snapshot:frontend;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1.5;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;call:Online.SetSelectedMapID,508;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=15;snapshot:inmatch;showmenu;wait:ui=Paused;wait:t=0.5;call:Game.QuitToMainMenu;wait:level=FrontEnd;wait:ui=FrontEnd;wait:t=4;snapshot:returned"
    $e4 = BaseEnv $d4 @{ WFC_FRONTEND_SCRIPT = (((1..$Cycles) | ForEach-Object { $one }) -join ";") + ";wait:t=3;quit"; WFC_SKIPINTRO = "1"; WFC_AUTOFIRE = "1"; WFC_AUTOBOOST_CYCLE = "90"; WFC_CUELOG = "1" }
    $r4 = Invoke-WfcSampled $exe $d4 $e4 ($TimeoutSec + 120 * $Cycles) 1.0
    Write-WfcCsv $r4.samples (Join-Path $d4 "process.csv")
    $F4 = Read-FlowLog (Join-Path $d4 "flow.jsonl"); $log4 = Join-Path $d4 "wfc.log"; $L4 = Read-RunLog $log4
    $rets = @(Flow-Ev $F4 "snapshot" | Where-Object why -eq "returned")
    Res "LIFETIME" "cycles_completed" $(if ($rets.Count -eq $Cycles) { "PASS" } else { "FAIL" }) ("{0}/{1} frontend -> Streets TDM (15 s of walking, turning, firing, boosting) -> frontend cycles; exit {2}" -f $rets.Count, $Cycles, $r4.rc) "Integration"
    $loaded = @(Flow-Ev $F4 "match.loaded"); $unl = @(Flow-Ev $F4 "match.unloaded"); $glr = @(Flow-Ev $F4 "match.glRelease")
    $flowLines = @(Get-Content (Join-Path $d4 "flow.jsonl"))
    function SampleAtSeq($seq) { $ln = [Array]::FindIndex($flowLines, [Predicate[string]] { param($q) $q -match ('"seq":' + $seq + ',') }) + 1; return @($r4.samples | Where-Object { $_.flow_lines -ge $ln -and $_.private_mb -gt 0 } | Select-Object -First 1)[0] }
    $rows = @(); for ($c = 0; $c -lt $rets.Count; $c++) {
        $smp = SampleAtSeq $rets[$c].seq
        $rows += [pscustomobject][ordered]@{ cycle = $c + 1; loaded_private_mb = $(if ($c -lt $loaded.Count) { [double]$loaded[$c].privateMB }); unloaded_private_mb = $(if ($c -lt $unl.Count) { [double]$unl[$c].privateMB })
            frontend_private_mb = $(if ($smp) { $smp.private_mb }); frontend_ws_mb = $(if ($smp) { $smp.ws_mb }); handles = $(if ($smp) { $smp.handles }); threads = $(if ($smp) { $smp.threads })
            gl_released = $(if ($c -lt $glr.Count) { "$($glr[$c].released)" }); load_s = $(if ($c -lt $loaded.Count) { $loaded[$c].seconds }) }
    }
    Write-WfcCsv $rows (Join-Path $d4 "cycles.csv")
    function Slope($vals) { $v = @($vals | Where-Object { $_ -ne $null -and "$_" -ne "" } | ForEach-Object { [double]$_ }); if ($v.Count -lt 3) { return $null }; $v = @($v | Select-Object -Skip 1); $n = $v.Count; $mx = ($n - 1) / 2.0; $my = ($v | Measure-Object -Average).Average; $num = 0.0; $den = 0.0; for ($k = 0; $k -lt $n; $k++) { $num += ($k - $mx) * ($v[$k] - $my); $den += ($k - $mx) * ($k - $mx) }; return [Math]::Round($num / [Math]::Max($den, 1e-9), 1) }
    $slUnl = Slope ($rows | ForEach-Object { $_.unloaded_private_mb }); $slFe = Slope ($rows | ForEach-Object { $_.frontend_private_mb }); $slH = Slope ($rows | ForEach-Object { $_.handles }); $slT = Slope ($rows | ForEach-Object { $_.threads })
    $grow = ($slUnl -ne $null -and $slUnl -gt 25) -or ($slFe -ne $null -and $slFe -gt 25)
    # owner attribution: GL objects released per cycle (Rendering), decoded PCM / voices after the match (Systems), UI screens (Frontend)
    $glCounts = @($glr | ForEach-Object { "$($_.released)" } | Select-Object -Unique)
    $amb4 = @(Read-Amb $log4)
    $pcmPeaks = @(); $livePeaks = @(); $lo = 0
    for ($c = 0; $c -lt $unl.Count; $c++) {
        $iL = Flow-LogIndex $L4 "match.loaded" $c; $iU = Flow-LogIndex $L4 "match.unloaded" $c
        $seg = @($amb4 | Where-Object { $_.i -gt $iL -and $_.i -lt $iU })
        $pcmPeaks += (MaxOf $seg "pcm"); $livePeaks += (MaxOf $seg "live")
    }
    $slPcm = Slope $pcmPeaks
    $attrib = @()
    if ($grow) {
        if ($glCounts.Count -gt 1) { $attrib += "GL objects released per return change across cycles ($($glCounts -join ' / ')): Rendering map unload" }
        if ($slPcm -ne $null -and $slPcm -gt 2) { $attrib += "decoded audio PCM peak grows $slPcm MB/cycle: Systems" }
        if (-not $attrib.Count) { $attrib += "neither GL release counts nor audio PCM explain it: run with WFC_NO_GL_RELEASE A/B and Systems WFC_LEVELAUDIO_CYCLE to split; unattributed growth -> Integration" }
    }
    Res "LIFETIME" "memory_stabilizes" $(if ($slUnl -eq $null -and $slFe -eq $null) { "SKIP" } elseif (-not $grow) { "PASS" } elseif ($rets.Count -lt 5) { "INFO" } else { "FAIL" }) ("private MB after each unload: {0} (slope {1} MB/cycle after cycle 1); at the frontend after each return: {2} (slope {3}). {4}" -f (($rows | ForEach-Object { $_.unloaded_private_mb }) -join " > "), $slUnl, (($rows | ForEach-Object { $_.frontend_private_mb }) -join " > "), $slFe, ($attrib -join "; ")) $(if ($grow) { "Rendering/Systems/Integration" } else { "" }) $slUnl
    $peak = ($r4.samples | Where-Object { $_.private_mb -gt 0 } | Measure-Object private_mb -Maximum).Maximum
    Res "LIFETIME" "memory_peak" $(if ($peak -gt 6144) { "FAIL" } else { "INFO" }) ("peak private memory over {0} cycles: {1} MB" -f $Cycles, $peak) "" $peak
    Res "LIFETIME" "handles_threads" $(if (($slH -ne $null -and $slH -gt 20) -or ($slT -ne $null -and $slT -gt 0.5)) { if ($rets.Count -lt 5) { "INFO" } else { "FAIL" } } else { "PASS" }) ("handles at each return: {0} (slope {1}); threads: {2} (slope {3})" -f (($rows | ForEach-Object { $_.handles }) -join " > "), $slH, (($rows | ForEach-Object { $_.threads }) -join " > "), $slT) "Integration"
    Res "LIFETIME" "gl_release_per_return" $(if (-not $glr.Count) { "INFO" } elseif ($glCounts.Count -eq 1) { "PASS" } else { "FAIL" }) ("GL objects released at each return: " + ($glCounts -join " | ") + $(if (-not $glr.Count) { " (no match.glRelease events: Rendering's unloadMapRenderData replaced the frontend GlCensus stopgap, or nothing was released)" } else { "" })) "Rendering"
    Res "LIFETIME" "audio_pcm_per_match" $(if (-not @($pcmPeaks | Where-Object { $_ -ne $null }).Count) { "SKIP" } elseif ($slPcm -ne $null -and $slPcm -gt 2) { "FAIL" } else { "PASS" }) ("peak decoded PCM per match: {0} MB; peak live instances per match: {1}" -f ($pcmPeaks -join " > "), ($livePeaks -join " > ")) "Systems"
    # Integration's audio.baseline / audio.loaded / audio.unloaded flow events: Systems' own audio state at each step
    $aBase = @(Flow-Ev $F4 "audio.baseline"); $aLoad = @(Flow-Ev $F4 "audio.loaded"); $aUnl = @(Flow-Ev $F4 "audio.unloaded")
    if ($aUnl.Count) {
        $basePcm = if ($aBase.Count) { [double]$aBase[0].pcmMB } else { $null }
        $badU = @($aUnl | Where-Object { [int]$_.voices -ne 0 -or [int]$_.instances -ne 0 -or [int]$_.levelCues -ne 0 -or ($basePcm -ne $null -and [Math]::Abs([double]$_.pcmMB - $basePcm) -gt 1.0) })
        Res "AUDIO" "map_audio_released_each_unload" $(if (-not $badU.Count) { "PASS" } else { "FAIL" }) ("after each of {0} unloads (voices / instances / level cues / PCM MB): {1}; pre-load baseline PCM {2} MB (Systems guarantee: 0 / 0 / 0 / baseline)" -f $aUnl.Count, (($aUnl | ForEach-Object { "$($_.voices)/$($_.instances)/$($_.levelCues)/$($_.pcmMB)" }) -join " | "), $basePcm) "Systems"
        $lv = @($aLoad | ForEach-Object { "$($_.level)" } | Select-Object -Unique); $lp = @($aLoad | ForEach-Object { [double]$_.pcmMB }); $lc = @($aLoad | ForEach-Object { [int]$_.levelCues } | Select-Object -Unique)
        Res "AUDIO" "map_audio_loaded_each_match" $(if ($aLoad.Count -eq $loaded.Count -and $lc.Count -eq 1 -and (($lp | Measure-Object -Maximum).Maximum - ($lp | Measure-Object -Minimum).Minimum) -le 1.0) { "PASS" } else { "FAIL" }) ("map audio at each match load: level {0}, level cues {1}, PCM {2} MB" -f ($lv -join ","), ($lc -join ","), ($lp -join " / ")) "Systems"
    }
    $afterUnl = @(); for ($c = 0; $c -lt $unl.Count; $c++) { $iU = Flow-LogIndex $L4 "match.unloaded" $c; $nx = @($amb4 | Where-Object { $_.i -gt $iU } | Select-Object -First 1)[0]; if ($nx) { $afterUnl += $nx } }
    $afterUnl = @($afterUnl | Where-Object { $_.map -ne $null -or $_.pcm -ne $null })
    if ($aUnl.Count) { $afterUnl = @() }   # Systems state at the unload (audio.unloaded) is the evidence; AMB only logs inside a match
    if ($afterUnl.Count) { $bad = @($afterUnl | Where-Object { $_.map -like "*Streets*" -or $_.pcm -gt $X.audio_ownership.pcm_base_mb.value * 1.15 + 260 }); Res "AUDIO" "map_audio_released_on_return" $(if (-not $bad.Count) { "PASS" } else { "FAIL" }) ("first AMB sample after each unload: " + (($afterUnl | ForEach-Object { "map $($_.map) live $($_.live) voices $($_.backendVoices) pcm $($_.pcm)" }) -join " | ") + " (Streets audio must be gone; frontend level only)") "Systems" }
    elseif (-not $aUnl.Count) { Res "AUDIO" "map_audio_released_on_return" "SKIP" "no AMB samples outside the match (the frontend runtime does not log AMB); see Systems' own soak in SELFTEST" "Systems" }
    $fePlays = @(Grep-Log $log4 ("\] MUSIC play (\S+\.)?" + $X.audio_ownership.frontend_music.value + " "))
    Res "AUDIO" "menu_music_resumes" $(if (-not (@(Grep-Log $log4 '\] MUSIC ')).Count) { "SKIP" } elseif ($fePlays.Count -ge $rets.Count) { "PASS" } else { "FAIL" }) ("frontend music starts: {0} for {1} returns (+ boot)" -f $fePlays.Count, $rets.Count) "Systems/Frontend"
    $mInMatch = 0; for ($c = 0; $c -lt $unl.Count; $c++) { $iL = Flow-LogIndex $L4 "match.loaded" $c; $iU = Flow-LogIndex $L4 "match.unloaded" $c; $mInMatch += @(Grep-Log $log4 '\] MUSIC play ' | Where-Object { $_.i -gt $iL -and $_.i -lt $iU }).Count }
    Res "AUDIO" "no_menu_music_in_matches" $(if (-not (@(Grep-Log $log4 '\] MUSIC ')).Count) { "SKIP" } elseif (-not $mInMatch) { "PASS" } else { "FAIL" }) ("music starts inside {0} matches: {1}" -f $unl.Count, $mInMatch) "Systems"
    $rev = @(Grep-Log $log4 '\] MIXER reverb')
    if ($rev.Count) { $stale = 0; for ($c = 0; $c -lt $unl.Count; $c++) { $iU = Flow-LogIndex $L4 "match.unloaded" $c; $nxt = @($rev | Where-Object { $_.i -gt $iU } | Select-Object -First 1)[0]; if ($nxt -and $nxt.text -match 'MIXER reverb (\S*MP_\S*) ->') { $stale++ } }; Res "AUDIO" "no_stale_reverb" $(if (-not $stale) { "PASS" } else { "FAIL" }) ("after {0} returns, the next reverb change still starts from a map preset (stale map reverb): {1}" -f $unl.Count, $stale) "Systems" }
    $dc4 = @(Find-DoubledCues $log4)
    Res "AUDIO" "doubled_sounds_cycles" $(if (-not (Has "WFC_CUELOG")) { "SKIP" } elseif (-not $dc4.Count) { "PASS" } else { "FAIL" }) ("doubled cue starts over {0} matches: {1} {2}" -f $Cycles, $dc4.Count, (($dc4 | Group-Object cue | Sort-Object Count -Descending | Select-Object -First 6 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", ")) "Systems"
    $opensPer = @(); $cur = 0; foreach ($e in $F4) { if ($e.ev -eq "gfx.movie" -and "$($e.opened)" -eq "True") { $cur++ }; if ($e.ev -eq "snapshot" -and $e.why -eq "returned") { $opensPer += $cur; $cur = 0 } }
    $opensPer = @($opensPer | Select-Object -Skip 1)
    Res "LIFETIME" "ui_movies_per_cycle" $(if (@($opensPer | Select-Object -Unique).Count -le 1) { "PASS" } else { "FAIL" }) ("GFx movies opened per cycle (after the first): " + ($opensPer -join ", ")) "Frontend"
    $badRet = @($rets | Where-Object { $_.mode -ne "" -or [int]$_.mapId -ne -1 -or "$($_.hud)" -ne "False" })
    Res "LIFETIME" "no_stale_flow_state" $(if (-not $badRet.Count) { "PASS" } else { "FAIL" }) ("returned snapshots with leftover match / lobby state: {0}" -f $badRet.Count) "Frontend"
    $loads = @(Grep-Log $log4 'match: launched'); Res "LIFETIME" "one_launch_per_cycle" $(if (-not $loads.Count) { "SKIP" } elseif ($loads.Count -eq $loaded.Count) { "PASS" } else { "FAIL" }) ("Gameplay launches {0}, frontend match loads {1}" -f $loads.Count, $loaded.Count) "Gameplay/Integration"
    foreach ($k in "map_collision", "map_objects", "particles", "event_queues", "timers") { Res "LIFETIME" "counter.$k" "SKIP" "no product counter for $k across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles" "Integration" }
}

# ======================================================================= R5 map ownership (select Gorge in the UI)
if ($Runs -contains "R5") {
    $d5 = Join-Path $OutDir "R5_map_gorge"; New-Item -ItemType Directory -Force $d5 | Out-Null
    $s5 = @((Path-ToHostOptions 0), (Path-CreateGame), (Path-SelectMap 1), "wait:t=1", (Shot $d5 "a_lobby_gorge"), "snapshot:selected", (Path-StartGame), "wait:t=25", (Shot $d5 "b_after"), "snapshot:after", "wait:t=30", "snapshot:after2", (Shot $d5 "c_after2"), "quit") -join ";"
    $r5 = Invoke-WfcSampled $exe $d5 (BaseEnv $d5 @{ WFC_FRONTEND_SCRIPT = $s5; WFC_SKIPINTRO = "1" }) 600 1.0
    $F5 = Read-FlowLog (Join-Path $d5 "flow.jsonl"); $log5 = Join-Path $d5 "wfc.log"
    $sel = @(Flow-Ev $F5 "snapshot" | Where-Object why -eq "selected")[0]; $ml5 = @(Flow-Ev $F5 "match.launch")[0]
    $gL = @(Grep-Log $log5 'match: launched|launchMatch|rejected|not loaded|unknown map'); $amb5 = @(Grep-Log $log5 'ambient: \S+:'); $after = @(Flow-Ev $F5 "snapshot" | Where-Object why -eq "after2")[0]
    $uiMap = $sel.mapId; $saysStreets = @($gL | Where-Object { $_.text -match 'match: launched \S*Streets' }).Count -or @($amb5 | Where-Object { $_.text -match 'Streets' -and $_.i -gt (Flow-LogIndex (Read-RunLog $log5) "match.launch" 0) }).Count
    $mg5 = @(Flow-Ev $F5 "match.gameplay")[0]; $al5 = @(Flow-Ev $F5 "audio.loaded")[0]
    $gorgeEnabled = @(Flow-Ev $F5 "gamelobby.map" | Where-Object { [int]$_.mapId -eq 510 -and "$($_.hasRequiredAssets)" -eq "True" }).Count -gt 0
    $consistent = $ml5 -and $mg5 -and $mg5.map -eq $ml5.runtimeDir -and (-not $al5 -or "$($al5.level)" -like "*$($ml5.runtimeDir)*")
    $st5 = if (-not $sel) { "SKIP" } elseif ([int]$uiMap -eq 510) { if ($saysStreets) { "FAIL" } elseif ($consistent) { "PASS" } elseif ($after -and $after.level -ne "Match") { "KNOWN" } else { "FAIL" } } elseif (-not $gorgeEnabled -and $consistent) { "PASS" } else { "FAIL" }
    Res "OWNERSHIP" "gorge_not_selectable" $(if (-not $gorgeEnabled -and [int]$uiMap -ne 510) { "KNOWN" } elseif ($gorgeEnabled) { "INFO" } else { "FAIL" }) ("map selector stepped right: selected {0}; Gorge selectable {1}. Integration lists Gorge disabled by design (no AssetTools render export; not validated by Gameplay / Rendering) - not counted as a working map" -f $uiMap, $gorgeEnabled) "AssetTools/Integration"
    Res "OWNERSHIP" "ui_map_is_the_map_loaded" $st5 ("UI selected map {0}; frontend launch runtimeDir {1}; Gameplay: {2}; Systems: {3}; level after 55 s: {4}. FAIL = an owner runs Streets behind a Gorge selection; KNOWN = launch refused cleanly (Gameplay documents launchMatch accepts only MP_IAC_Streets in this milestone)" -f $uiMap, $ml5.runtimeDir, (($gL | Select-Object -First 2 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | "), (($amb5 | Select-Object -Last 1 | ForEach-Object { $_.text -replace '^.*\] ', '' })), $after.level) "Integration/Gameplay"
    $crash = $r5.rc -ne 0 -and -not $r5.timedOut -and -not @(Flow-Ev $F5 "exit").Count
    Res "OWNERSHIP" "unsupported_map_handled" $(if ($crash) { "FAIL" } else { "PASS" }) ("process exit {0}; clean exit event {1}" -f $r5.rc, @(Flow-Ev $F5 "exit").Count) "Integration"
}

# ======================================================================= R6 mode ownership (DM through the mode list)
if ($Runs -contains "R6") {
    $d6 = Join-Path $OutDir "R6_mode_dm"; New-Item -ItemType Directory -Force $d6 | Out-Null
    $s6 = @((Path-ToHostOptions 1), (Path-CreateGame), "snapshot:lobby", "wait:t=1", (Path-StartGame), "wait:level=Match", "wait:t=13", (Shot $d6 "a_dm"), "snapshot:dm", "showmenu", "wait:ui=Paused", "call:Game.QuitToMainMenu", "wait:level=FrontEnd", "quit") -join ";"
    $r6 = Invoke-WfcSampled $exe $d6 (BaseEnv $d6 @{ WFC_FRONTEND_SCRIPT = $s6; WFC_SKIPINTRO = "1" }) 600 1.0
    $F6 = Read-FlowLog (Join-Path $d6 "flow.jsonl"); $log6 = Join-Path $d6 "wfc.log"
    $egm6 = @(Flow-Ev $F6 "bridge" | Where-Object fn -eq "Online.PlayPrivateGame")[0]; $ml6 = @(Flow-Ev $F6 "match.launch")[0]; $lm6 = @(Flow-Ev $F6 "loading.start" | Where-Object kind -eq "Map")[0]
    $g6 = @(Grep-Log $log6 'match: launched|match: \S+ begin'); $sp6 = @(Grep-Log $log6 'match: local player spawned')
    Res "OWNERSHIP" "ui_mode_is_the_mode_run" $(if (-not $ml6) { "FAIL" } elseif ($ml6.mode -eq "DM" -and @($g6 | Where-Object { $_.text -match 'DM' }).Count -and -not @($g6 | Where-Object { $_.text -match 'TDM' }).Count) { "PASS" } elseif (-not $g6.Count) { "FAIL" } else { "FAIL" }) ("mode list Down -> {0}; launch mode {1} goal {2}; loading title '{3}'; Gameplay: {4}; spawn: {5}" -f $egm6.args, $ml6.mode, $ml6.goalScore, $lm6.title, (($g6 | ForEach-Object { $_.text -replace '^.*match: ', '' }) -join " | "), (($sp6 | Select-Object -First 1 | ForEach-Object { $_.text -replace '^.*spawned at ', '' }))) "Integration/Gameplay"
    if ($sp6.Count) { Res "MATCH" "dm_spawn_class" $(if ($sp6[0].text -match 'TnFreeForAllPlayerStart') { "PASS" } else { "FAIL" }) ("DM local spawn: " + ($sp6[0].text -replace '^.*spawned at ', '')) "Gameplay" }
}

# ======================================================================= R8 PC SKU presentation (Frontend 76b8287: $version WIN by default)
# The shipped movies' own PC branches: no Press START gate, mc_menuMainPC with Accounts / Exit Game, clickable buttons.
# Keyboard-logical path (ui:Down / ui:Accept through UiBindings) and the mouse path (clickclip on the button clip).
if ($Runs -contains "R8" -and (Has "WFC_PLATFORM") -and $uiMode) {
    $d8 = Join-Path $OutDir "R8_pc_sku"; New-Item -ItemType Directory -Force $d8 | Out-Null
    $s8 = @("wait:frontend", "wait:ui=FrontEnd", "wait:t=2.5", (Shot $d8 "a_pc_main"), "dump:FrontEnd_GFX", "snapshot:pcmain",
            "ui:Down", "wait:t=0.6", "ui:Accept", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=2", (Shot $d8 "b_party_keys"), "snapshot:party_keys",
            "ui:Back", "wait:t=1", "wait:level=FrontEnd", "wait:ui=FrontEnd", "wait:t=2.5", (Shot $d8 "c_main_again"), "snapshot:main_again",
            "clickclip:menuMain_mc.multiplayerBtn_mc", "wait:t=1.5", "clickclip:_root.menuMain_mc.multiplayerBtn_mc", "wait:t=1.5", "snapshot:after_click", (Shot $d8 "d_after_click"), "quit") -join ";"
    $e8 = BaseEnv $d8 @{ WFC_FRONTEND_SCRIPT = $s8; WFC_SKIPINTRO = "1" }; $e8.Remove("WFC_PLATFORM")   # default = PC
    $r8 = Invoke-WfcSampled $exe $d8 $e8 300 1.0
    $F8 = Read-FlowLog (Join-Path $d8 "flow.jsonl"); $log8 = Join-Path $d8 "wfc.log"
    $clips = @(Grep-Log $log8 "clip '(exitGameBtn_mc|accountsBtn_mc|multiplayerBtn_mc|campaignBtn_mc)'" | ForEach-Object { [regex]::Match($_.text, "clip '(\w+)'").Groups[1].Value } | Select-Object -Unique)
    $sdui = @(Flow-Ev $F8 "bridge" | Where-Object fn -eq "Online.ShowDeviceSelectionUI")
    $pm = Shot-Stats (Join-Path $d8 "a_pc_main.bmp")
    Res "STATE" "pc.no_start_gate" $(if ($clips -contains "multiplayerBtn_mc" -and $clips -contains "exitGameBtn_mc") { "PASS" } else { "FAIL" }) ("PC SKU main menu without a Start press: buttons {0} (shipped FrontEnd_GFX PC branch: mc_menuMainPC with Accounts / Exit Game)" -f ($clips -join ",")) "Frontend"
    $opl8 = @(Flow-Ev $F8 "bridge" | Where-Object fn -eq "Online.OpenPartyLobby")
    Res "STATE" "pc.keyboard_to_multiplayer" $(if (@(Flow-Ev $F8 "snapshot" | Where-Object why -eq "party_keys" | Where-Object level -eq "PartyLobby").Count) { "PASS" } else { "FAIL" }) ("ui:Down + ui:Accept (logical Accept: Enter / pad A) -> party lobby; OpenPartyLobby calls {0}" -f $opl8.Count) "Frontend"
    $ma = @(Flow-Ev $F8 "snapshot" | Where-Object why -eq "main_again")[0]
    Res "STATE" "pc.back_to_main" $(if ($ma -and $ma.level -eq "FrontEnd") { "PASS" } else { "FAIL" }) ("ui:Back at the party root -> {0} (BLK A3: Game.QuitToMainMenu)" -f $ma.level) "Frontend"
    $cc = @(Flow-Ev $F8 "script.clickclip"); $found = @($cc | Where-Object { "$($_.found)" -eq "True" })
    Res "STATE" "pc.mouse_click" $(if (-not $cc.Count) { "SKIP" } elseif ($found.Count -and @(Flow-Ev $F8 "bridge" | Where-Object { $_.fn -eq "Online.OpenPartyLobby" -and [int]$_.seq -gt [int]$found[0].seq }).Count) { "PASS" } elseif ($found.Count) { "FAIL" } else { "INFO" }) ("clickclip on the Multiplayer button: paths tried {0}, found {1}; OpenPartyLobby after the click: {2}" -f $cc.Count, (($found | ForEach-Object { $_.path }) -join ","), @(Flow-Ev $F8 "bridge" | Where-Object { $_.fn -eq "Online.OpenPartyLobby" -and $found.Count -and [int]$_.seq -gt [int]$found[0].seq }).Count) "Frontend"
    Res "PRESENTED" "pc.main_menu" $(if (-not $pm) { "SKIP" } elseif ($pm.black -ge 0.97) { "FAIL" } else { "HUMAN" }) ("PC main menu frame near-black {0:P0}: the PC menu look is a human check; the spec remains the Xbox 360 presentation (R1)" -f $(if ($pm) { $pm.black })) "Frontend"
} elseif ($Runs -contains "R8") { Res "STATE" "pc.sku" "SKIP" "this exe has no PC SKU switch (WFC_PLATFORM) or ui: actions" "Frontend" }

# ======================================================================= R7 score-limit lifecycle x3 through Gameplay's rules
# WFC_LIFECYCLE=<goal> (Integration, TEST ONLY): shortens PointsToWin and adds one Gameplay diagnostic opponent; every
# 2.5 s of InProgress the local player and the opponent alternately kill each other through World::applyMatchDamage,
# so Gameplay's own scoring, death, wave respawn, score-limit end, MatchOver and ReturnToGameLobby run. The authored
# defaults (40 / 900) are checked separately in R1 (MATCH.default_rules_tdm).
if ($Runs -contains "R7" -and (Has "WFC_LIFECYCLE")) {
    $d7 = Join-Path $OutDir "R7_lifecycle"; New-Item -ItemType Directory -Force $d7 | Out-Null
    $goal = 5
    $m1 = @("wait:level=Match", "wait:ui=InGame", "wait:t=1", (Shot $d7 "a_m1_ingame"), "wait:ui=Spectating", "wait:t=0.8", (Shot $d7 "b_m1_spectating"), "wait:ui=GameEnded", "wait:t=2", (Shot $d7 "c_m1_endstats"), "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2.5", (Shot $d7 "d_lobby_after1"), "snapshot:lobby1")
    $m2 = @((Path-StartGame), "wait:level=Match", "wait:ui=InGame", "wait:t=1", (Shot $d7 "e_m2_ingame"), "wait:ui=GameEnded", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2.5", "snapshot:lobby2")
    $m3 = @((Path-StartGame), "wait:level=Match", "wait:ui=InGame", "wait:t=4", (Shot $d7 "f_m3_ingame"), "snapshot:m3", "showmenu", "wait:ui=Paused", "wait:t=1", "call:Game.QuitToMainMenu", "wait:level=FrontEnd", "wait:ui=FrontEnd", "wait:t=3", (Shot $d7 "g_frontend"), "snapshot:returned", "quit")
    $s7 = (@((Path-ToHostOptions 0), (Path-CreateGame), (Path-StartGame)) + $m1 + $m2 + $m3) -join ";"
    $r7 = Invoke-WfcSampled $exe $d7 (BaseEnv $d7 @{ WFC_FRONTEND_SCRIPT = $s7; WFC_SKIPINTRO = "1"; WFC_LIFECYCLE = "$goal"; WFC_CUELOG = "1" }) 1500 1.0
    Write-WfcCsv $r7.samples (Join-Path $d7 "process.csv")
    $F7 = Read-FlowLog (Join-Path $d7 "flow.jsonl"); $log7 = Join-Path $d7 "wfc.log"
    $done7 = @(Flow-Ev $F7 "snapshot" | Where-Object why -eq "returned").Count
    Res "MATCH" "lifecycle_three_matches" $(if ($done7) { "PASS" } else { "FAIL" }) ("3 matches to the score limit ({0}) through the shipped lobby, then pause -> quit -> frontend: {1}" -f $goal, $(if ($done7) { "completed" } else { "stopped after '" + ((@(Flow-Ev $F7 "script.wait") | Select-Object -Last 1).cond) + "'" })) "Gameplay/Frontend/Integration"
    # local player id = the victim of the kill that follows a 'local' lifecycle damage
    $me = $null; for ($k = 0; $k -lt $F7.Count - 1; $k++) { if ($F7[$k].ev -eq "test.lifecycle.damage" -and $F7[$k].victim -eq "local") { $nk = @($F7[($k + 1)..($F7.Count - 1)] | Where-Object ev -eq "match.kill" | Select-Object -First 1)[0]; if ($nk) { $me = "$($nk.victim)"; break } } }
    $deaths = @(Flow-Ev $F7 "match.kill" | Where-Object { "$($_.victim)" -eq $me })
    $spect = @(); $resp = @(); $back = @()
    foreach ($dk in $deaths) {
        $s = @($F7 | Where-Object { [int]$_.seq -gt [int]$dk.seq -and $_.ev -eq "ui.state" -and $_.to -eq "Spectating" } | Select-Object -First 1)[0]
        $r = @($F7 | Where-Object { [int]$_.seq -gt [int]$dk.seq -and $_.ev -eq "match.spawn" -and "$($_.player)" -eq $me } | Select-Object -First 1)[0]
        $b = @($F7 | Where-Object { $r -and [int]$_.seq -ge [int]$r.seq -and $_.ev -eq "ui.state" -and $_.from -eq "Spectating" -and $_.to -eq "InGame" } | Select-Object -First 1)[0]
        if ($s) { $spect += [double]$s.t - [double]$dk.t }; if ($r) { $resp += [double]$r.t - [double]$dk.t }; if ($b -and $r) { $back += [double]$b.t - [double]$r.t }
    }
    Res "MATCH" "death_to_spectating" $(if (-not $deaths.Count) { "FAIL" } elseif (@($spect | Where-Object { [Math]::Abs($_ - 3.0) -gt 0.35 }).Count -or $spect.Count -lt $deaths.Count) { "FAIL" } else { "PASS" }) ("{0} local deaths; Spectating UI after {1} s (BLK E7: MinRespawnDelay 3.0 s -> UI event 4)" -f $deaths.Count, (($spect | ForEach-Object { "{0:N2}" -f $_ }) -join ", ")) "Gameplay/Integration"
    Res "MATCH" "respawn_delay" $(if (-not $resp.Count) { "FAIL" } elseif (@($resp | Where-Object { [Math]::Abs($_ - 5.0) -gt 0.35 }).Count -or $resp.Count -lt $deaths.Count) { "FAIL" } else { "PASS" }) ("respawn after {0} s (BLK E7 / RE 5.6: wave 5.0 s)" -f (($resp | ForEach-Object { "{0:N2}" -f $_ }) -join ", ")) "Gameplay"
    Res "MATCH" "respawn_returns_to_ingame" $(if ($back.Count -eq $resp.Count -and $back.Count -and @($back | Where-Object { $_ -gt 0.2 }).Count -eq 0) { "PASS" } else { "FAIL" }) ("Spectating -> InGame at the respawn ({0} of {1}; UI event 5)" -f $back.Count, $resp.Count) "Integration/Frontend"
    # scores from the RUNTIME-EVENTS MATCH lines, per match
    $mlines = @(Grep-Log $log7 '\] MATCH (init|restart|kill|score|end|spawn|respawn|death|cleanup) ')
    $segs = @(); $cur = New-Object System.Collections.Generic.List[object]; $inSeg = $false   # (an empty @() is "falsy" in PowerShell)
    foreach ($m in $mlines) { if ($m.text -match '\] MATCH init ') { if ($inSeg) { $segs += , $cur.ToArray() }; $cur = New-Object System.Collections.Generic.List[object]; $inSeg = $true }; if ($inSeg) { $cur.Add($m) } }
    if ($inSeg) { $segs += , $cur.ToArray() }
    $segRows = @()
    for ($k = 0; $k -lt $segs.Count; $k++) {
        $sg = $segs[$k]; $sc = @($sg | Where-Object { $_.text -match 'MATCH score team=(\d) score=(\d+)' } | ForEach-Object { $mm = [regex]::Match($_.text, 'team=(\d) score=(\d+)'); [pscustomobject]@{ team = [int]$mm.Groups[1].Value; score = [int]$mm.Groups[2].Value } })
        $mono = $true; foreach ($t in 0, 1) { $v = @($sc | Where-Object team -eq $t | ForEach-Object { $_.score }); for ($q = 0; $q -lt $v.Count; $q++) { if ($v[$q] -ne $q + 1) { $mono = $false } } }
        $end = @($sg | Where-Object { $_.text -match 'MATCH end ' })[0]; $kills = @($sg | Where-Object { $_.text -match 'MATCH kill ' }).Count
        $win = if ($end) { [regex]::Match($end.text, 'winner=(\S+)').Groups[1].Value } else { "" }; $rsn = if ($end) { [regex]::Match($end.text, 'reason=(\S+)').Groups[1].Value } else { "" }
        $maxT = @(0, 1 | ForEach-Object { $t = $_; (@($sc | Where-Object team -eq $t) | Measure-Object score -Maximum).Maximum })
        $segRows += [pscustomobject]@{ match = $k + 1; kills = $kills; scores = ($sc | ForEach-Object { "t$($_.team)=$($_.score)" }) -join " "; increments_by_one = $mono; end = $rsn; winner = $win; top = ($maxT -join "-") }
    }
    Write-WfcCsv $segRows (Join-Path $d7 "matches.csv")
    Res "MATCH" "kill_scores_team" $(if ($segRows.Count -and @($segRows | Where-Object { -not $_.increments_by_one }).Count -eq 0) { "PASS" } else { "FAIL" }) ("per match, team score lines step 1, 2, 3 ... from 0 (BLK F1: kill +1 team): " + (($segRows | ForEach-Object { "match $($_.match): $($_.scores)" }) -join " | ")) "Gameplay"
    $ended = @($segRows | Where-Object { $_.end -eq "score_limit" })
    Res "MATCH" "score_limit_end" $(if ($ended.Count -ge 2 -and @($ended | Where-Object { $_.top -notmatch "(^|-)$goal(-|$)" }).Count -eq 0) { "PASS" } else { "FAIL" }) ("matches ending on the score limit: {0} ({1}); a team reached exactly {2} (BLK F3 CheckScore)" -f $ended.Count, (($segRows | ForEach-Object { "match $($_.match): $($_.end) winner $($_.winner) top $($_.top)" }) -join "; "), $goal) "Gameplay"
    Res "MATCH" "second_match_scores_reset" $(if ($segRows.Count -ge 2 -and $segRows[1].increments_by_one -and $segRows[1].scores -match 't\d=1') { "PASS" } else { "FAIL" }) ("match 2 score lines start again at 1: '{0}' (BLK F6: a new match is a fresh level)" -f $(if ($segRows.Count -ge 2) { $segRows[1].scores })) "Gameplay"
    $ge7 = @(Flow-Ev $F7 "ui.state" | Where-Object to -eq "GameEnded"); $ret7 = @(Flow-Ev $F7 "match.return"); $trL7 = @(Flow-Ev $F7 "travel" | Where-Object { $_.from -eq "Match" -and $_.url -like "UI_Lobby_m*" })
    $gaps = @(); for ($k = 0; $k -lt [Math]::Min($ge7.Count, $trL7.Count); $k++) { $gaps += [double]$trL7[$k].t - [double]$ge7[$k].t }
    Res "MATCH" "match_over_15s_return" $(if ($gaps.Count -ge 2 -and @($gaps | Where-Object { [Math]::Abs($_ - 15) -gt 1.5 }).Count -eq 0 -and @($trL7 | Where-Object { (Parse-Url $_.url).keys.MapId -ne "508" }).Count -eq 0) { "PASS" } else { "FAIL" }) ("GameEnded -> lobby travel after {0} s; URLs keep MapId: {1} (BLK F4/F6)" -f (($gaps | ForEach-Object { "{0:N1}" -f $_ }) -join ", "), (($trL7 | ForEach-Object { (Parse-Url $_.url).keys.MapId }) -join ",")) "Gameplay/Integration"
    $hud7 = @(Flow-Ev $F7 "ui.hud" | Where-Object { "$($_.visible)" -eq "False" }).Count; $es7 = @(@(Flow-Ev $F7 "gfx.movie") + @(Flow-Ev $F7 "ui.open") | Where-Object { $_.movie -like "*EndGameStats*" }).Count; $rsp7 = @(@(Flow-Ev $F7 "gfx.movie") + @(Flow-Ev $F7 "ui.open") | Where-Object { $_.movie -like "*MultiplayerRespawn*" }).Count
    Res "MATCH" "end_and_respawn_screens" $(if ($es7 -ge 2 -and $rsp7 -ge 1 -and $hud7 -ge 2) { "PASS" } else { "FAIL" }) ("EndGameStats opened {0}x, MultiplayerRespawn {1}x, HUD hidden {2}x (BLK E7/F4)" -f $es7, $rsp7, $hud7) "Frontend/Integration"
    foreach ($sn in "b_m1_spectating", "c_m1_endstats", "d_lobby_after1", "e_m2_ingame", "g_frontend") { $st = Shot-Stats (Join-Path $d7 "$sn.bmp"); if ($st) { Res "PRESENTED" "lifecycle.$sn" $(if ($st.black -ge 0.97) { "FAIL" } else { "HUMAN" }) ("{0}: near-black {1:P0}, mean luma {2}{3}" -f $st.file, $st.black, $st.mean, $(if ($st.black -ge 0.97) { " - nothing drawn" } else { " - drawn; look is a human check (R7_sheet.png)" })) "Frontend" } }
    $tiles = @(Get-ChildItem $d7 -Filter *.bmp | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = $_.BaseName } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "R7_sheet.png") 4 400 225 }
    $sp7 = @(Flow-Ev $F7 "match.spawn" | Where-Object { "$($_.player)" -eq $me } | ForEach-Object { "$($_.start)" })
    Res "MATCH" "respawn_starts" $(if (@($sp7 | Where-Object { $_ -notlike "TnTeamPlayerStart*" }).Count) { "FAIL" } else { "PASS" }) ("local spawns: " + (($sp7 | Select-Object -Unique) -join ", ")) "Gameplay"
    $l1 = @(Flow-Ev $F7 "snapshot" | Where-Object why -eq "lobby1")[0]
    Res "STATE" "lobby_after_score_limit" $(if ($l1 -and $l1.level -eq "GameLobby" -and [int]$l1.mapId -eq 508 -and $l1.mode -eq "TDM") { "PASS" } else { "FAIL" }) ("lobby after match 1: level {0} mode {1} mapId {2} (BLK C7: selection index 0 = Streets in the rebuild)" -f $l1.level, $l1.mode, $l1.mapId) "Frontend"
    $dc7 = @(Find-DoubledCues $log7)
    Res "AUDIO" "doubled_sounds_lifecycle" $(if (-not $dc7.Count) { "PASS" } else { "FAIL" }) ("doubled cue starts over 3 matches with deaths / respawns: {0} {1}" -f $dc7.Count, (($dc7 | Group-Object cue | Sort-Object Count -Descending | Select-Object -First 6 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", ")) "Systems"
    $aU7 = @(Flow-Ev $F7 "audio.unloaded"); if ($aU7.Count) { Res "AUDIO" "audio_released_after_lifecycle" $(if (@($aU7 | Where-Object { [int]$_.voices -ne 0 -or [int]$_.instances -ne 0 }).Count) { "FAIL" } else { "PASS" }) ("after each match: " + (($aU7 | ForEach-Object { "voices $($_.voices) instances $($_.instances) pcm $($_.pcmMB)" }) -join " | ")) "Systems" }
} elseif ($Runs -contains "R7") { Res "MATCH" "lifecycle_three_matches" "SKIP" "WFC_LIFECYCLE not compiled into this exe" "Integration" }

# ======================================================================= S owners' self-tests on THIS executable
if ($Runs -contains "S") {
    $dS = Join-Path $OutDir "S_selftests"; New-Item -ItemType Directory -Force $dS | Out-Null
    $tests = [ordered]@{
        WFC_TDMTEST   = @{ owner = "Gameplay"; pat = 'TDMTEST SUMMARY: (\d+)/(\d+)'; what = "TDM session: hitscan kill, scoring, teammate damage, death mid-transform, 5 s respawn, score limit, MatchOver 15 s, second match reset, HUD state" }
        WFC_MATCHTEST = @{ owner = "Gameplay"; pat = 'MATCHTEST B time limit: state MatchOver.*winner team -1'; what = "match core: TDM to 40, clock tie with 120/60/30 announcements, DM to 20, suicide / environmental death, respawn" }
        WFC_XFORMTEST = @{ owner = "Gameplay"; pat = 'XFORM SUMMARY: (\d+)/(\d+) transforms ended UNDER THE MAP'; what = "boost -> robot under-map stress (Gameplay's own)" }
        WFC_CHAOS     = @{ owner = "Gameplay"; pat = 'CHAOS SUMMARY: .*: (\d+) runs UNDER THE MAP.*?, (\d+) KillZ'; what = "seeded random play from nav points: under the map / KillZ / stuck / inside props" }
        WFC_VEHTEST   = @{ owner = "Gameplay"; pat = 'VEHTEST'; what = "vehicle handling measurements" }
        WFC_RELOADTEST = @{ owner = "Rendering"; pat = '(?i)reload'; what = "in-process map render unload / reload" }
    }
    foreach ($k in $tests.Keys) {
        $t = $tests[$k]
        if (-not (Has $k)) { Res "SELFTEST" $k "SKIP" "$k not compiled into this exe" $t.owner; continue }
        $dd = Join-Path $dS $k; New-Item -ItemType Directory -Force $dd | Out-Null
        $env0 = @{ $k = "1"; WFC_BOOT = "match" }; if ($k -eq "WFC_RELOADTEST") { $env0 = @{ WFC_RELOADTEST = "120"; WFC_SMOKE_FRAMES = "400"; WFC_BOOT = "match" } }; if (Test-Path $rd) { $env0.WFC_RENDER_DATA = $rd }
        $rc = Invoke-WfcExe $exe $dd $env0 "run.log" 1200
        $lg = Join-Path $dd "wfc.log"; $hits = @(Grep-Log $lg $t.pat)
        $last = if ($hits.Count) { $hits[-1].text -replace '^.*\] ', '' } else { "" }
        $st = "INFO"
        switch ($k) {
            "WFC_TDMTEST" { $m = [regex]::Match($last, '(\d+)/(\d+)'); $st = if ($m.Success -and $m.Groups[1].Value -eq $m.Groups[2].Value) { "PASS" } elseif ($m.Success) { "FAIL" } else { "FAIL" } }
            "WFC_MATCHTEST" { $st = if ($hits.Count) { "PASS" } else { "FAIL" }; $last = ((@(Grep-Log $lg 'MATCHTEST [ABC] ') | Select-Object -Last 6 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | ") }
            "WFC_XFORMTEST" { $m = [regex]::Match($last, 'SUMMARY: (\d+)/(\d+)'); $st = if ($m.Success -and $m.Groups[1].Value -eq "0") { "PASS" } else { "FAIL" } }
            "WFC_CHAOS" { $m = [regex]::Match($last, ': (\d+) runs UNDER THE MAP.*?, (\d+) KillZ'); $st = if ($m.Success -and $m.Groups[1].Value -eq "0" -and $m.Groups[2].Value -eq "0") { "PASS" } elseif ($m.Success) { "FAIL" } else { "FAIL" }; $u = @(Grep-Log $lg 'CHAOS (UNDER-FLOOR|STUCK)'); if ($u.Count) { $last += " | " + (($u | Select-Object -First 3 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | ") } }
            "WFC_VEHTEST" { $st = "INFO"; $last = ((@(Grep-Log $lg 'VEHTEST') | Select-Object -Last 3 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | ") }
            "WFC_RELOADTEST" { $st = if (@(Grep-Log $lg '^\[(error|ERROR)').Count) { "FAIL" } elseif ($hits.Count) { "PASS" } else { "INFO" }; $last = ((@(Grep-Log $lg '(?i)reload') | Select-Object -Last 3 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | ") }
        }
        Res "SELFTEST" $k $st ("{0}: {1} (exit {2})" -f $t.what, $last, $rc) $t.owner
    }
    $audioSuite = @(Get-ChildItem $binDir -Filter "*audio*suite*.exe" -ErrorAction SilentlyContinue)[0]
    if ($audioSuite) { $to = Join-Path $dS "audio_suite.txt"; $p = Start-Process -FilePath $audioSuite.FullName -WorkingDirectory $binDir -PassThru -Wait -NoNewWindow -RedirectStandardOutput $to -RedirectStandardError "$to.err"; $tail = (Get-Content $to -Tail 3) -join " | "; Res "SELFTEST" "systems_audio_suite" $(if ($p.ExitCode -eq 0) { "PASS" } else { "FAIL" }) ("$($audioSuite.Name) exit $($p.ExitCode): $tail") "Systems" }
    else { Res "SELFTEST" "systems_audio_suite" "SKIP" "no *audio*suite* executable built in this tree (Systems' 544-check suite is a tools/systems target)" "Systems" }
}

# ======================================================================= report
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Milestone 05 end-to-end gate ($Config)"); $md.Add(""); $md.Add("- target: $target"); $md.Add("- exe: ``$exe``"); $md.Add("- runs: $($Runs -join ', '); cycles $Cycles; $(Get-Date -Format 'yyyy-MM-dd HH:mm')"); $md.Add("")
$md.Add(("**PASS {0} / FAIL {1} / KNOWN {2} / INFO {3} / SKIP {4} / HUMAN-CHECK {5}**" -f $sum.pass, $sum.fail, $sum.known, $sum.info, $sum.skip, $sum.human)); $md.Add("")
foreach ($layer in "DATA", "STATE", "PRESENTED", "MATCH", "AUDIO", "OWNERSHIP", "LIFETIME", "SELFTEST") {
    $rowsL = @($res | Where-Object { $_.id -like "e2e.$layer.*" }); if (-not $rowsL.Count) { continue }
    $md.Add("## $layer"); $md.Add(""); $md.Add("| status | check | owner | evidence |"); $md.Add("|---|---|---|---|")
    foreach ($rx in $rowsL) { $md.Add(("| {0} | {1} | {2} | {3} |" -f $rx.status, ($rx.id -replace "^e2e\.$layer\.", ""), $rx.owner, (($rx.note -replace '\|', '/') -replace "`n", ' '))) }
    $md.Add("")
}
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "M05-GATE.md")
"M05 E2E GATE ($Config): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ") + " -> $OutDir"
