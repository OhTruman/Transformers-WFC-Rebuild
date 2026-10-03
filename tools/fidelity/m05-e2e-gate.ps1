# Milestone 05 end-to-end gate: boot -> frontend -> multiplayer -> mode/map -> Streets TDM -> loading -> spawn ->
# gameplay -> death -> respawn -> score -> match end -> return -> second launch, plus repeated loops.
#
#   .\tools\fidelity\m05-e2e-gate.ps1 -Root <merged tree> [-OutDir <dir>] [-Cycles 3] [-MatchSeconds 6]
#
# Every check has a LAYER, so "data exists" is never confused with "the user experiences it":
#   DATA       authored data / catalog / unit tests exist and pass
#   STATE      the product's own trace (WFC_FLOWLOG, wfc.log) reaches the state with the expected values
#   PRESENTED  the user can actually see / hear it: window pixels captured from OUTSIDE the process
#              (PrintWindow), movies played for real, audio voices sounding, pawn input actually withheld
#   LIFETIME   resources / state across repeated loops (process memory, handles, audio PCM, stale state)
#   MATCH      TDM lifecycle (Gameplay): spawn, team, damage, death, respawn, scores, limits, end, reset
# and a STATUS: PASS / FAIL (the product contradicts CONFIRMED/HIGH evidence) / KNOWN (documented gap or
# provisional adapter, with owner) / INFO / SKIP (not implemented yet: the check is ready) / HUMAN (needs a person).
# Expectations: tools\fidelity\m05\expectations.json (RE / AssetTools / Systems, each with confidence).
param([Parameter(Mandatory)][string]$Root, [string]$OutDir = "", [int]$Cycles = 3, [double]$MatchSeconds = 6, [int]$TimeoutSec = 900)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path
if (-not $OutDir) { $OutDir = Join-Path (Get-WfcRoot) ("work\fidelity\m05gate\" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$X = Get-Content -Raw (Join-Path $PSScriptRoot "m05\expectations.json") | ConvertFrom-Json
$exe = @("build-release\bin\wfc_rebuild.exe", "build\release\bin\wfc_rebuild.exe", "build\bin\wfc_rebuild.exe") | ForEach-Object { Join-Path $Root $_ } | Where-Object { Test-Path $_ } | Select-Object -First 1
$tests = @("build\bin\wfc_frontend_tests.exe", "build-release\bin\wfc_frontend_tests.exe") | ForEach-Object { Join-Path $Root $_ } | Where-Object { Test-Path $_ } | Select-Object -First 1
$rd = Join-Path $Root "work\render"
$AT = "F:\Transformers Rebuild\AssetTools\manifests"
$res = New-WfcResults
function Res($layer, $id, $status, $note, $owner = "", $measured = $null) { Add-WfcResult $res ("e2e.{0}.{1}" -f $layer, $id) $status $measured $note $owner }
function Conf($node) { if ($node.conf -in "CONFIRMED", "HIGH") { "FAIL" } else { "KNOWN" } }   # status for a mismatch against an expectation
$fullScript = { param($matchWait)
    "wait:frontend;wait:ui=FrontEnd;wait:t=2;snapshot:mainmenu;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;" +
    "call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;snapshot:gamelobby;" +
    "call:Online.SetSelectedMapID,508;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=$matchWait;snapshot:ingame;" +
    "showmenu;wait:ui=Paused;wait:t=2;snapshot:paused;call:Game.QuitToMainMenu;wait:level=FrontEnd;wait:ui=FrontEnd;wait:t=2;snapshot:returned" }
function BaseEnv($dir) {
    $e = @{ WFC_BOOT = "frontend"; WFC_FLOWLOG = (Join-Path $dir "flow.jsonl"); WFC_FLOWSEED = "1"; WFC_FLOW_TIMEOUT = "$TimeoutSec"; WFC_AUTOWALK = "1"; WFC_LOGEVERY = "10"; WFC_AMBLOG = "1"; WFC_MUSICLOG = "1"; WFC_RENDERSTATS = "1"; WFC_SMOKE_FRAMES = "100000000" }   # SMOKE_FRAMES: never reached, enables the per-frame pawn log (WFC_BOOT=frontend keeps the frontend boot)
    if (Test-Path $rd) { $e.WFC_RENDER_DATA = $rd }
    return $e
}
if (-not $exe) { Res "DATA" "exe" "SKIP" "no wfc_rebuild.exe under $Root"; $null = Write-WfcReport $res (Join-Path $OutDir "report.json"); return }

# ================= DATA =================
foreach ($m in "frontend_flow", "frontend_gfx", "frontend_modes", "frontend_maps", "frontend_loading", "frontend_audio", "frontend_localization") { $p = Join-Path $AT "$m.json"; Res "DATA" "manifest.$m" $(if (Test-Path $p) { "PASS" } else { "FAIL" }) "AssetTools manifests\$m.json present" "AssetTools" }
$flowM = Get-Content -Raw (Join-Path $AT "frontend_flow.json") | ConvertFrom-Json
$movRoot = "F:\Transformers Rebuild\ExtractedAssets"
$missingMov = @($X.boot.first_launch_movies.value | Where-Object { $f = $flowM.movie_files."$_.mkv"; -not ($f -and (Test-Path (Join-Path $movRoot $f))) })
Res "DATA" "intro_movie_files" $(if (-not $missingMov.Count) { "PASS" } else { "FAIL" }) ("authored intro movies on disk (ExtractedAssets\movies): missing " + ($missingMov -join ","))
if ($tests) {
    $to = Join-Path $OutDir "frontend_tests.txt"; $p = Start-Process -FilePath $tests -WorkingDirectory (Split-Path $tests) -PassThru -Wait -NoNewWindow -RedirectStandardOutput $to -RedirectStandardError "$to.err"
    $txt = Get-Content $to -Raw; $nPass = ([regex]::Matches($txt, '(?im)^\s*(\[?PASS\]?|ok)\b')).Count
    Res "DATA" "frontend_unit_tests" $(if ($p.ExitCode -eq 0) { "PASS" } else { "FAIL" }) ("wfc_frontend_tests exit {0} (exit = failures); failing: {1}; output frontend_tests.txt" -f $p.ExitCode, ((@([regex]::Matches($txt, '(?m)^FAIL (.*)$') | ForEach-Object { $_.Groups[1].Value.Trim() })) -join "; ")) "Frontend" $p.ExitCode
} else { Res "DATA" "frontend_unit_tests" "SKIP" "wfc_frontend_tests.exe not built in this tree" "Frontend" }

# ================= RUN 1: first launch (fresh profile) =================
$d1 = Join-Path $OutDir "run1_first_launch"; New-Item -ItemType Directory -Force $d1 | Out-Null
$e1 = (BaseEnv $d1) + @{ WFC_FRONTEND_SCRIPT = ((& $fullScript $MatchSeconds) + ";quit") }
$r1 = Invoke-WfcObserved $exe $d1 $e1 $TimeoutSec 0.25
$F = Read-FlowLog (Join-Path $d1 "flow.jsonl"); $L1 = Read-RunLog (Join-Path $d1 "wfc.log")
if (-not $F.Count) { Res "STATE" "flow_trace" "SKIP" "no WFC_FLOWLOG trace: this tree has no frontend boot (agents/frontend not merged?)" "Frontend" }
else {
    Res "STATE" "flow_trace" $(if (@(Flow-Ev $F "timeout").Count) { "FAIL" } else { "PASS" }) ("{0} flow events, run exit {1}, timeout events {2}" -f $F.Count, $r1.rc, @(Flow-Ev $F "timeout").Count) "Frontend"
    # ---- boot ----
    $b = @(Flow-Ev $F "boot")[0]
    Res "STATE" "boot_map" $(if ($b.map -eq $X.boot.map.value) { "PASS" } else { Conf $X.boot.map }) ("boot map {0} (expected {1}, {2})" -f $b.map, $X.boot.map.value, $X.boot.map.src) "Frontend"
    Res "STATE" "first_launch_profile" $(if ("$($b.watchedIntro)" -eq "False") { "PASS" } else { "FAIL" }) "fresh run directory: watchedIntro=$($b.watchedIntro)" "Frontend"
    $ls0 = @(Flow-Ev $F "loading.start")[0]; $lc0 = @(Flow-Ev $F "loading.close")[0]
    # The effective boot loading state is what loading.close reports; GameFlow::init switches to InitialStartup after
    # travel() has already emitted a generic loading.start, so a stale start event alone is a trace detail, not a defect.
    Res "STATE" "boot_loading_movie" $(if ($lc0.kind -eq "InitialStartup") { "PASS" } else { Conf $X.boot.initial_loading_movie }) ("first loading screen closes as kind={0} (expected InitialStartup = {1}, {2})" -f $lc0.kind, $X.boot.initial_loading_movie.value, $X.boot.initial_loading_movie.src) "Frontend"
    if ($ls0.kind -ne $lc0.kind) { Res "STATE" "boot_loading_trace" "INFO" ("trace detail for the frontend lane: the first loading.start says kind={0} bink={1} but the screen closes as kind={2}; no event records the switch to {3}" -f $ls0.kind, $ls0.bink, $lc0.kind, $X.boot.initial_loading_movie.value) "Frontend" }
    $plays = @(Flow-Ev $F "movie.play" | ForEach-Object { $_.movie })
    $want = @($X.boot.first_launch_movies.value)
    Res "STATE" "intro_chain_order" $(if ((@($plays | Select-Object -First $want.Count) -join ",") -eq ($want -join ",")) { "PASS" } else { Conf $X.boot.first_launch_movies }) ("movie.play order: {0}; expected {1} ({2})" -f ($plays -join ","), ($want -join ","), $X.boot.first_launch_movies.src) "Frontend"
    # ---- UI controller transitions (RE 1.4 table; a new controller per level starts from NotInGame / its StartingState) ----
    $legal = @{}; foreach ($t in $X.ui_controller.transitions) { $legal["$($t[0])>$($t[1])"] = $true }
    $bad = @(); foreach ($u in (Flow-Ev $F "ui.state")) { $k = "$($u.from)>$($u.to)"; $start = $X.ui_controller.starting_state."$($u.level)"; if (-not $legal[$k] -and -not ($u.from -eq "NotInGame" -and ($u.to -eq $start -or $u.to -eq "FrontEnd"))) { $bad += "$k@$($u.level)" } }
    Res "STATE" "ui_transitions_legal" $(if (-not $bad.Count) { "PASS" } else { "FAIL" }) ("{0} UI state changes; not in the TnUIController table: {1}" -f @(Flow-Ev $F "ui.state").Count, ($bad -join ", ")) "Frontend"
    # ---- movie per state ----
    $opens = @(Flow-Ev $F "ui.open")
    foreach ($st in "FrontEnd", "InLobby@PartyLobby", "InLobby@GameLobby", "WaitingOnGameStart", "Paused") {
        $want = $X.ui_controller.movie_for_state.$st; $s0 = $st.Split('@')[0]; $lvl = if ($st -like "*@*") { $st.Split('@')[1] } else { $null }
        $reached = @(Flow-Ev $F "ui.state" | Where-Object { $_.to -eq $s0 -and (-not $lvl -or $_.level -eq $lvl) })
        if (-not $reached.Count) { Res "STATE" "movie_for.$st" "SKIP" "state $st not reached"; continue }
        $hit = @($opens | Where-Object { $_.movie -eq $want })
        Res "STATE" "movie_for.$st" $(if ($hit.Count) { "PASS" } else { "FAIL" }) ("state {0}: expected {1} opened (RE 1.4); opened: {2}" -f $st, $want, (($opens | ForEach-Object { $_.movie }) -join ",")) "Frontend"
    }
    # ---- duplicate / stale screens ----
    $open = @{}; $dups = @(); $stale = @()
    foreach ($e in $F) {
        if ($e.ev -in "ui.open", "gfx.open") { if ($open[$e.movie]) { $dups += $e.movie }; $open[$e.movie] = $true }
        elseif ($e.ev -in "ui.close", "gfx.close") { $open.Remove($e.movie) }
        elseif ($e.ev -eq "level.begin") { $leftover = @($open.Keys | Where-Object { $_ -notlike "*MovieLoader*" }); if ($leftover.Count -gt 1) { $stale += ("{0}: {1}" -f $e.level, ($leftover -join ",")) }; $open = @{} }
    }
    Res "STATE" "no_duplicate_screens" $(if (-not $dups.Count) { "PASS" } else { "FAIL" }) ("movies opened again without being closed: " + ($dups -join ", ")) "Frontend"
    Res "STATE" "no_stale_screens_across_travel" $(if (-not $stale.Count) { "PASS" } else { "HUMAN" }) ("more than one movie still open when a new level began (travel replaces the UI): " + ($stale -join "; ")) "Frontend"
    # ---- travel / selection ----
    $tr = @(Flow-Ev $F "travel")
    $party = @($tr | Where-Object { $_.url -like "UI_PartyLobby_m*" })[0]
    Res "STATE" "party_lobby_url" $(if ($party.url -eq $X.travel.party_lobby_url.value) { "PASS" } else { Conf $X.travel.party_lobby_url }) ("{0} (expected {1})" -f $party.url, $X.travel.party_lobby_url.value) "Frontend"
    $br = @(Flow-Ev $F "bridge"); $opl = @($br | Where-Object fn -eq "Online.OpenPartyLobby")[0]
    Res "STATE" "multiplayer_call" $(if ($opl.args -eq $X.travel.multiplayer_gts.value) { "PASS" } else { "FAIL" }) ("Online.OpenPartyLobby({0}) (RE 1.5: {1})" -f $opl.args, $X.travel.multiplayer_gts.value) "Frontend"
    $pub = @(Flow-Ev $F "lobby.publishGameInfo")[0]
    Res "STATE" "mode_selection" $(if ($pub.tag -eq "TDM" -and $pub.friendlyName -eq $X.loading.match_title.value) { "PASS" } else { "FAIL" }) ("publishGameInfo tag={0} settings={1} friendlyName={2}" -f $pub.tag, $pub.settings, $pub.friendlyName) "Frontend"
    $gl = @($tr | Where-Object { $_.url -like "UI_Lobby_m*" })[0]; $glu = Parse-Url $gl.url; $badK = @($X.travel.game_lobby_url_keys.value.PSObject.Properties | Where-Object { $glu.keys[$_.Name] -ne $_.Value } | ForEach-Object { "$($_.Name)=$($glu.keys[$_.Name]) (want $($_.Value))" })
    Res "STATE" "game_lobby_url" $(if ($gl -and -not $badK.Count) { "PASS" } else { "FAIL" }) ("{0}; mismatched keys: {1}" -f $gl.url, ($badK -join ", ")) "Frontend"
    $maps = @(Flow-Ev $F "gamelobby.map"); $sel = $maps | Select-Object -Last 1
    Res "STATE" "map_selection" $(if ($sel.mapId -eq 508 -and $sel.map -eq "MP_IAC_Streets_Base_m" -and "$($sel.hasRequiredAssets)" -eq "True") { "PASS" } else { "FAIL" }) ("selected mapId={0} map={1} name={2} hasRequiredAssets={3}; preselected before the call: {4}" -f $sel.mapId, $sel.map, $sel.name, $sel.hasRequiredAssets, $(if ($maps.Count -gt 1) { $maps[0].mapId } else { "n/a" })) "Frontend"
    $cd = @(Flow-Ev $F "gamelobby.countdown")[0]; $ticks = @(Flow-Ev $F "gamelobby.countdownTick"); $fc = @(Flow-Ev $F "gamelobby.finalCountdown")[0]
    $span = if ($cd -and $fc) { [double]$fc.t - [double]$cd.t } else { -1 }
    Res "STATE" "lobby_countdown" $(if ($cd.seconds -eq $X.travel.lobby_countdown_s.value -and [Math]::Abs($span - $X.travel.lobby_countdown_s.value) -le 1.0) { "PASS" } else { "FAIL" }) ("countdown {0} s, {1} ticks, final countdown after {2:F2} s (expected {3} s, 1 s ticks)" -f $cd.seconds, $ticks.Count, $span, $X.travel.lobby_countdown_s.value) "Frontend"
    $teamName = @{ "0" = "Autobots"; "1" = "Decepticons" }
    Res "STATE" "team_pick" $(if ($fc.team -in 0, 1 -and $fc.teamName -eq $teamName["$($fc.team)"]) { "PASS" } else { "FAIL" }) ("finalCountdown team={0} {1} (RE 5.3: 0 Autobots, 1 Decepticons; PickTeam tie -> RandomInt(2) HIGH)" -f $fc.team, $fc.teamName) "Frontend"
    $mu = (@(Flow-Ev $F "gamelobby.startLevel")[0]).url
    Res "STATE" "match_url" $(if ($mu -eq $X.travel.match_url.value) { "PASS" } else { $p = Parse-Url $mu; if (@($X.travel.match_url_keys.value.PSObject.Properties | Where-Object { $p.keys[$_.Name] -ne $_.Value }).Count) { "FAIL" } else { "KNOWN" } }) ("{0}`n expected (RE 3.1): {1}" -f $mu, $X.travel.match_url.value) "Frontend"
    $lm = @(Flow-Ev $F "loading.start" | Where-Object kind -eq "Map")[0]
    Res "STATE" "match_loading_text" $(if ($lm.title -eq $X.loading.match_title.value -and $lm.message -eq $X.loading.match_message.value -and [int]$lm.tips -eq $X.loading.match_tips.value) { "PASS" } else { "FAIL" }) ("title '{0}' message '{1}' tips {2} gfx {3} bink {4}" -f $lm.title, $lm.message, $lm.tips, $lm.gfx, $lm.bink) "Frontend"
    $ml = @(Flow-Ev $F "match.launch")[0]
    Res "STATE" "match_launch" $(if ([int]$ml.goalScore -eq $X.match_tdm.goal_score.value -and [int]$ml.timeLimit -eq $X.match_tdm.time_limit_s.value -and $ml.mode -eq "TDM" -and "$($ml.team)" -eq "$($fc.team)") { "PASS" } else { "FAIL" }) ("mode {0} goalScore {1} timeLimit {2} team {3} runtimeDir {4} (RE 5.1: 40 / 900; team = lobby pick {5})" -f $ml.mode, $ml.goalScore, $ml.timeLimit, $ml.team, $ml.runtimeDir, $fc.team) "Frontend"
    $mld = @(Flow-Ev $F "match.loaded")[0]; Res "STATE" "match_load_time" "INFO" ("blocking match load {0} s" -f $mld.seconds) "" ([double]$mld.seconds)
    # ---- match UI sequence (Gameplay PendingMatch / InProgress drive it; until then a PROVISIONAL adapter) ----
    $ad = @(Flow-Ev $F "adapter")
    $pre = @(Flow-Ev $F "ui.open" | Where-Object { $_.movie -like "*PreGameCountdown*" })[0]; $ing = @(Flow-Ev $F "ui.state" | Where-Object to -eq "InGame")[0]
    $pendSpan = if ($pre -and $ing) { [double]$ing.t - [double]$pre.t } else { -1 }
    Res "MATCH" "pending_countdown" $(if ([Math]::Abs($pendSpan - $X.match_tdm.pending_countdown_s.value) -le 1.5) { "PASS" } elseif ($ad.Count) { "KNOWN" } else { "FAIL" }) ("PreGameCountdown -> InGame after {0:F2} s (RE 5.2: {1} s). {2}" -f $pendSpan, $X.match_tdm.pending_countdown_s.value, $(if ($ad.Count) { "PROVISIONAL adapter: " + $ad[0].what } else { "" })) "Gameplay"
    $hud = @(Flow-Ev $F "ui.hud" | Where-Object { "$($_.visible)" -eq "True" })
    Res "STATE" "hud_visible_in_game" $(if ($hud.Count) { "PASS" } else { "FAIL" }) "ui.hud visible=True on entering InGame (RE 1.4)" "Frontend"
    # ---- pause, input focus, world keeps running (bPauseable false HIGH) ----
    $iP = ($L1 | Where-Object { $_.kind -eq "flow" -and $_.text -match "ui\.state .*to=Paused" } | Select-Object -First 1).i
    $iQ = ($L1 | Where-Object { $_.kind -eq "flow" -and $_.text -match "match\.quit" } | Select-Object -First 1).i
    if ($iP -and $iQ) {
        $before = @($L1 | Where-Object { $_.kind -eq "frame" -and $_.i -lt $iP } | Select-Object -Last 3)
        $during = @($L1 | Where-Object { $_.kind -eq "frame" -and $_.i -gt $iP -and $_.i -lt $iQ })
        $mvB = ($before | Measure-Object hspeed -Maximum).Maximum; $mvD = if ($during.Count) { ($during | Measure-Object hspeed -Maximum).Maximum } else { -1 }
        $disp = if ($during.Count -ge 2) { [Math]::Sqrt([Math]::Pow($during[-1].x - $during[0].x, 2) + [Math]::Pow($during[-1].z - $during[0].z, 2)) } else { -1 }
        # The WFC_AUTO* hooks are injected AFTER the frontend's focus gate (Application.cpp), and the platform polls
        # GetAsyncKeyState (real input to the focused window only). Real keys cannot be sent from the gate without
        # typing into whatever window is foreground on a shared machine, so the focus behaviour is a human check
        # until the product offers a pre-gate input hook. The measured scripted-walk numbers are kept as INFO.
        Res "PRESENTED" "pause_input_focus" "HUMAN" ("hold W, press Esc in a match: the robot must stop while the pause menu is up and the world keeps running. Not automatable: scripted input (WFC_AUTOWALK) enters after the focus gate and the window reads GetAsyncKeyState. Proposed hook for the frontend lane: a scripted-input variant applied before the focus gate. Scripted walk for reference: before pause max {0:F1} m/s, during pause max {1:F1} m/s, displacement {2:F2} m over {3} samples" -f $mvB, $mvD, $disp, $during.Count) "Frontend"
        Res "STATE" "world_runs_while_paused" $(if ($during.Count -ge 2 -and $during[-1].frame -gt $during[0].frame) { "PASS" } elseif ($during.Count -lt 2) { "SKIP" } else { "FAIL" }) ("match frames advance during pause: {0} -> {1} (RE 6: bPauseable false, HIGH)" -f $(if ($during.Count) { $during[0].frame }), $(if ($during.Count) { $during[-1].frame })) "Frontend"
    } else { Res "PRESENTED" "pause_input_focus" "SKIP" "pause or quit not reached" "Frontend" }
    # ---- quit -> main menu ----
    $q = @(Flow-Ev $F "match.quit")[0]; $tq = @($tr | Where-Object { $_.from -eq "Match" })[0]; $un = @(Flow-Ev $F "match.unloaded"); $fe2 = @(Flow-Ev $F "level.begin" | Where-Object level -eq "FrontEnd")
    Res "STATE" "quit_to_main_menu" $(if ($q -and $tq.url -eq $X.travel.quit_to_main_menu.value -and $un.Count -and $fe2.Count -ge 2) { "PASS" } else { "FAIL" }) ("match.quit reason={0}; travel {1}; match.unloaded {2}; FrontEnd level entered {3} times (RE 5.7)" -f $q.reason, $tq.url, $un.Count, $fe2.Count) "Frontend"
    $retFs = @(Flow-Ev $F "fscommand" | Where-Object { $_.cmd -in "enterFrontEnd", "enterMovieSequence" }) | Select-Object -Last 1
    $playsAfter = @(Flow-Ev $F "movie.play" | Where-Object { [int]$_.seq -gt [int]$tq.seq })
    Res "STATE" "return_skips_intro" $(if ($retFs.cmd -eq "enterFrontEnd" -and -not $playsAfter.Count) { "PASS" } else { Conf $X.boot.second_launch_skips_logos }) ("MovieLoader after return: {0}; intro movies after return {1}" -f $retFs.cmd, $playsAfter.Count) "Frontend"
    $snaps = @(Flow-Ev $F "snapshot"); $ret = @($snaps | Where-Object why -eq "returned")[0]
    Res "LIFETIME" "state_reset_after_return" $(if ($ret -and $ret.mode -eq "" -and [int]$ret.mapId -eq -1 -and [int]$ret.team -eq -1 -and "$($ret.hud)" -eq "False") { "PASS" } elseif (-not $ret) { "SKIP" } else { "FAIL" }) ("returned frontend snapshot: mode='{0}' mapId={1} team={2} hud={3} countdown={4}" -f $ret.mode, $ret.mapId, $ret.team, $ret.hud, $ret.countdown) "Frontend"
    Res "STATE" "return_to_game_lobby_after_match_end" "SKIP" ("needs the TDM match end (Gameplay MatchOver {0} s -> ReturnToGameLobby UI_Lobby_m with MapId kept; RE 5.7)" -f $X.match_tdm.match_over_countdown_s.value) "Gameplay"
    # ---- PRESENTED: movies, screens, audio ----
    $un = @(Flow-Ev $F "movie.unavailable")
    Res "PRESENTED" "intro_movies_played" $(if (-not $un.Count -and $plays.Count) { "PASS" } elseif ($un.Count) { "KNOWN" } else { "SKIP" }) ("movie.unavailable {0}: {1} - the files exist (DATA) but the user sees no movie" -f $un.Count, (($un | ForEach-Object { "$($_.movie) ($($_.why))" }) -join "; ")) "Frontend"
    $capRows = @(); foreach ($c in $r1.captures) { if (-not $c.file) { continue }; $l = [WfcImage]::Luma((Join-Path $d1 $c.file), 8); $s = [WfcImage]::Stats($l, 6.0); $capRows += [pscustomobject]@{ seq = $c.seq; ev = $c.ev; detail = $c.detail; file = $c.file; mean = [Math]::Round($s[0], 1); black = [Math]::Round($s[1], 3); flat = [Math]::Round($s[2], 3) } }
    Write-WfcCsv $capRows (Join-Path $d1 "captures.csv")
    # Control: the in-game frame is drawn by the 3D renderer in every tree. If it captures as a uniform image, PrintWindow
    # failed for this run (GL window not composited: occluded / minimised / locked desktop) and no capture is judged.
    $ctl = @($capRows | Where-Object { $_.detail -match "to=InGame" })[0]
    $capValid = $ctl -and $ctl.flat -lt 0.9 -and $ctl.black -lt 0.97
    Res "PRESENTED" "capture_control" $(if ($capValid) { "PASS" } else { "SKIP" }) ("in-game control capture {0}: flat {1:P0}, near-black {2:P0}. {3}" -f $ctl.file, $ctl.flat, $ctl.black, $(if ($capValid) { "window capture works this run" } else { "window capture unavailable this run (uniform control frame): screen checks below are SKIP. Re-run with the product window visible on an unlocked desktop" })) ""
    if (-not $capValid) { $capRows = @() }
    foreach ($grp in @(@{ id = "frontend_screen"; m = "level=FrontEnd" }, @{ id = "party_lobby_screen"; m = "level=PartyLobby" }, @{ id = "game_lobby_screen"; m = "level=GameLobby" }, @{ id = "loading_screen_match"; m = "kind=Map" }, @{ id = "in_game"; m = "to=InGame" }, @{ id = "pause_menu"; m = "to=Paused" })) {
        $c = @($capRows | Where-Object { $_.detail -match $grp.m }) | Select-Object -First 1
        if (-not $c) { Res "PRESENTED" $grp.id "SKIP" "no window capture for this state (state not reached or window not capturable)"; continue }
        $blank = $c.black -ge 0.97
        $st = if ($blank) { if ($grp.id -in "in_game") { "FAIL" } else { "KNOWN" } } else { "HUMAN" }
        Res "PRESENTED" $grp.id $st ("window capture {0}: mean luma {1}, near-black {2:P0}, flat {3:P0}. {4}" -f $c.file, $c.mean, $c.black, $c.flat, $(if ($blank) { "Nothing is drawn: the trace says the screen is open, the user sees a black window (no GFx movie presenter / loading presentation yet)." } else { "Something is drawn: whether it looks like WFC is a human judgement." })) $(if ($blank) { "Frontend" } else { "" })
    }
    if ($capRows.Count) { $tiles = @($capRows | ForEach-Object { @{ png = (Join-Path $d1 $_.file); label = "#$($_.seq) $($_.ev) $($_.detail) black $([Math]::Round($_.black*100))%"; flag = ($_.black -ge 0.97) } }); New-WfcSheet $tiles (Join-Path $OutDir "sheet_first_launch.png") 4 400 225 }
    $pauseCap = @($capRows | Where-Object { $_.detail -match "to=Paused" })[0]; $gameCap = @($capRows | Where-Object { $_.detail -match "to=InGame" })[0]
    if ($pauseCap -and $gameCap) { Res "PRESENTED" "pause_menu_drawn" "HUMAN" ("pause capture vs in-game capture: does a pause menu appear? (no presenter installed means the paused frame is the game view; see sheet_first_launch.png)") "Frontend" }
    $music = @($L1 | Where-Object kind -eq "music")
    Res "PRESENTED" "menu_music" $(if (-not $music.Count) { "SKIP" } elseif (@($music | Where-Object { $_.text -match "MUSIC play $($X.audio_ownership.frontend_music.value)" }).Count) { "PASS" } else { "FAIL" }) ("MUSIC log lines {0} (WFC_MUSICLOG; needs Systems FrontendAudio wired by the frontend owner): {1}" -f $music.Count, (($music | Select-Object -First 4 | ForEach-Object { $_.text -replace '^.*MUSIC ', '' }) -join " | ")) "Systems/Frontend"
    $ambM = @($L1 | Where-Object { $_.kind -eq "amb" -and $_.text -match "backendVoices=(\d+)" } | ForEach-Object { [int]([regex]::Match($_.text, "backendVoices=(\d+)").Groups[1].Value) })
    $ambAny = @($L1 | Where-Object kind -eq "amb")
    Res "PRESENTED" "match_audio_sounding" $(if (-not $ambAny.Count) { "SKIP" } elseif (($ambM | Measure-Object -Maximum).Maximum -gt 0 -or @($ambAny | Where-Object { $_.text -match "voices=([1-9]\d*)" }).Count) { "PASS" } else { "FAIL" }) ("AMB samples {0}; backend voices max {1} (Systems field; absent before the Systems merge)" -f $ambAny.Count, $(if ($ambM.Count) { ($ambM | Measure-Object -Maximum).Maximum } else { "n/a" })) "Systems"
    Res "PRESENTED" "ui_audio_ownership" "SKIP" "UI sounds (FrontendAudio.playUiSound) and per-level music handover need the Systems FrontendAudio wiring (call sequence in Systems FIDELITY M05) - expectations in m05\expectations.json audio_ownership" "Systems/Frontend"
}

# ================= RUN 2: second launch (same profile) =================
$d2 = Join-Path $OutDir "run2_second_launch"; New-Item -ItemType Directory -Force $d2 | Out-Null
if (Test-Path (Join-Path $d1 "wfc_profile.ini")) { Copy-Item (Join-Path $d1 "wfc_profile.ini") $d2 }   # the profile the first launch wrote
$e2 = BaseEnv $d2; $e2.WFC_FRONTEND_SCRIPT = "wait:frontend;wait:ui=FrontEnd;wait:t=2;snapshot:mainmenu;quit"
$r2 = Invoke-WfcObserved $exe $d2 $e2 300 0.25
$F2 = Read-FlowLog (Join-Path $d2 "flow.jsonl")
if ($F2.Count) {
    $b2 = @(Flow-Ev $F2 "boot")[0]; $p2 = @(Flow-Ev $F2 "movie.play")
    Res "STATE" "second_launch_profile" $(if ("$($b2.watchedIntro)" -eq "True") { "PASS" } else { "FAIL" }) "second launch in the same directory: watchedIntro=$($b2.watchedIntro) (profile persisted; medium rebuild-only)" "Frontend"
    Res "STATE" "second_launch_skips_logos" $(if (-not $p2.Count) { "PASS" } else { Conf $X.boot.second_launch_skips_logos }) ("intro movies played on the second launch: {0} ({1})" -f $p2.Count, $X.boot.second_launch_skips_logos.src) "Frontend"
    Res "STATE" "second_launch_reaches_menu" $(if (@(Flow-Ev $F2 "ui.state" | Where-Object to -eq "FrontEnd").Count) { "PASS" } else { "FAIL" }) "main menu reached on the second launch" "Frontend"
} else { Res "STATE" "second_launch" "SKIP" "no trace" }

# ================= RUN 3: repeated loops in one process =================
$d3 = Join-Path $OutDir "run3_loops"; New-Item -ItemType Directory -Force $d3 | Out-Null
$loop = (1..$Cycles | ForEach-Object { & $fullScript ([Math]::Max(3, $MatchSeconds)) }) -join ";"
$e3 = (BaseEnv $d3) + @{ WFC_FRONTEND_SCRIPT = "$loop;wait:t=3;quit"; WFC_SKIPINTRO = "1" }
$r3 = Invoke-WfcSampled $exe $d3 $e3 ($TimeoutSec * 2) 1.0
$F3 = Read-FlowLog (Join-Path $d3 "flow.jsonl"); $L3 = Read-RunLog (Join-Path $d3 "wfc.log")
Write-WfcCsv $r3.samples (Join-Path $d3 "process.csv")
if ($F3.Count) {
    $rets = @(Flow-Ev $F3 "snapshot" | Where-Object why -eq "returned"); $mls = @(Flow-Ev $F3 "match.loaded"); $launch = @(Flow-Ev $F3 "match.launch")
    Res "LIFETIME" "cycles_completed" $(if ($rets.Count -eq $Cycles) { "PASS" } else { "FAIL" }) ("{0}/{1} frontend -> TDM -> pause -> quit -> frontend cycles completed (exit {2})" -f $rets.Count, $Cycles, $r3.rc) "Frontend"
    # memory at each return: the sample whose flow_lines first reaches the 'returned' snapshot's line
    $lines = @(Get-Content (Join-Path $d3 "flow.jsonl")); $mem = @()
    foreach ($s in $rets) { $ln = [Array]::FindIndex($lines, [Predicate[string]] { param($fl) $fl -match ('"seq":' + $s.seq + ',') }) + 1; $smp = @($r3.samples | Where-Object { $_.flow_lines -ge $ln -and $_.private_mb -gt 0 } | Select-Object -First 1); if ($smp) { $mem += $smp } }
    if ($mem.Count -ge 2) {
        $dMb = ($mem[-1].private_mb - $mem[0].private_mb) / ($mem.Count - 1); $dH = ($mem[-1].handles - $mem[0].handles) / ($mem.Count - 1)
        $grow = $dMb -gt 30
        Res "LIFETIME" "memory_per_cycle" $(if (-not $grow) { "PASS" } else { "KNOWN" }) ("private MB at each return to the frontend: {0} (+{1:F1} MB per cycle). The frontend lane documents that the renderer is recreated on return and the previous map's GL objects leak (handoff Rendering: map unload)" -f (($mem | ForEach-Object { $_.private_mb }) -join " -> "), $dMb) $(if ($grow) { "Rendering" } else { "" }) $dMb
        $peak = ($r3.samples | Where-Object { $_.private_mb -gt 0 } | Measure-Object private_mb -Maximum).Maximum
        Res "LIFETIME" "memory_peak" $(if ($peak -gt 6144) { "FAIL" } else { "INFO" }) ("peak private memory over {0} cycles: {1} MB (process.csv). FAIL above 6 GB: at that size a user's ordinary session of a few matches exhausts a typical 8-16 GB machine" -f $Cycles, $peak) $(if ($peak -gt 6144) { "Rendering/Frontend" } else { "" }) $peak
        Res "LIFETIME" "handles_per_cycle" $(if ($dH -gt 50) { "FAIL" } else { "PASS" }) ("handles at each return: {0} ({1:+0.0;-0.0} per cycle)" -f (($mem | ForEach-Object { $_.handles }) -join " -> "), $dH) "" $dH
    } else { Res "LIFETIME" "memory_per_cycle" "SKIP" "fewer than 2 returns sampled" }
    $loads = @(Select-String -Path (Join-Path $d3 "wfc.log") -Pattern "uploaded mesh 0:" -SimpleMatch)
    Res "LIFETIME" "one_world_load_per_match" $(if ($mls.Count -and $loads.Count -eq $mls.Count) { "PASS" } elseif (-not $loads.Count) { "INFO" } else { "FAIL" }) ("match.loaded {0}, world loads logged {1}" -f $mls.Count, $loads.Count) "Gameplay/Frontend"
    Res "LIFETIME" "load_time_per_cycle" "INFO" ("match load seconds per cycle: " + (($mls | ForEach-Object { $_.seconds }) -join ", ")) ""
    $same = @($launch | ForEach-Object { "{0}|{1}|{2}" -f $_.mode, $_.goalScore, $_.timeLimit } | Select-Object -Unique)
    Res "LIFETIME" "match_parameters_stable" $(if ($same.Count -eq 1) { "PASS" } else { "FAIL" }) ("match.launch mode|goal|time per cycle: " + (($launch | ForEach-Object { "{0}|{1}|{2}|team {3}" -f $_.mode, $_.goalScore, $_.timeLimit, $_.team }) -join "; ")) "Frontend"
    $badRet = @($rets | Where-Object { $_.mode -ne "" -or [int]$_.mapId -ne -1 -or [int]$_.team -ne -1 -or "$($_.hud)" -ne "False" })
    Res "LIFETIME" "no_stale_flow_state" $(if (-not $badRet.Count) { "PASS" } else { "FAIL" }) ("frontend snapshots after each return with leftover match/lobby state: {0}" -f $badRet.Count) "Frontend"
    $opensPer = @(); $cur = 0; foreach ($e in $F3) { if ($e.ev -eq "ui.open") { $cur++ }; if ($e.ev -eq "snapshot" -and $e.why -eq "returned") { $opensPer += $cur; $cur = 0 } }; $opensPer = @($opensPer | Select-Object -Skip 1)   # cycle 1 also holds the boot screens
    Res "LIFETIME" "ui_events_per_cycle_constant" $(if (@($opensPer | Select-Object -Unique).Count -le 1) { "PASS" } else { "FAIL" }) ("ui.open events per cycle: " + ($opensPer -join ", ") + " (growth = screens accumulating)") "Frontend"
    # first match frame per cycle: player state must start fresh
    $starts = @(); $inMatch = $false; foreach ($lx in $L3) { if ($lx.kind -eq "flow" -and $lx.text -match "FLOW match\.loaded") { $inMatch = $true }; if ($inMatch -and $lx.kind -eq "frame") { $starts += $lx; $inMatch = $false } }
    $ammo = @($starts | ForEach-Object { [regex]::Match($_.text, "ammo=(\d+/\d+)").Groups[1].Value } | Select-Object -Unique)
    $forms = @($starts | ForEach-Object { [regex]::Match($_.text, "form=(\w+)").Groups[1].Value } | Select-Object -Unique)
    Res "LIFETIME" "player_state_fresh_each_match" $(if ($starts.Count -lt 2) { "SKIP" } elseif ($ammo.Count -eq 1 -and $forms.Count -eq 1) { "PASS" } else { "FAIL" }) ("first frame of each match: ammo {0}; form {1} ({2} matches)" -f ($ammo -join ","), ($forms -join ","), $starts.Count) "Gameplay"
    $pcm = @($L3 | Where-Object { $_.kind -eq "amb" -and $_.text -match "pcm=([\d.]+)MB map=(\S+)" } | ForEach-Object { $m = [regex]::Match($_.text, "pcm=([\d.]+)MB map=(\S+)"); [pscustomobject]@{ pcm = [double]$m.Groups[1].Value; map = $m.Groups[2].Value } })
    Res "LIFETIME" "audio_pcm_returns_to_base" $(if (-not $pcm.Count) { "SKIP" } else { "INFO" }) ("decoded PCM per AMB sample (needs Systems): first {0} MB, last {1} MB, max {2} MB; Systems guarantee: back to base after unloadMapAudio" -f $(if ($pcm.Count) { $pcm[0].pcm }), $(if ($pcm.Count) { $pcm[-1].pcm }), $(if ($pcm.Count) { ($pcm | Measure-Object pcm -Maximum).Maximum })) "Systems"
    foreach ($k in "map_collision", "render_assets", "frontend_movies", "event_queues", "timers") { Res "LIFETIME" "$k" "SKIP" ("no product counter yet for $k across travel; proposal: one 'LIFETIME <name>=<count/bytes>' log line per level.begin (protocols\RUNTIME-EVENTS.md)") "Frontend/Rendering/Systems" }
} else { Res "LIFETIME" "loops" "SKIP" "no trace" }

# ================= MATCH (Gameplay TDM) =================
$matchEv = @($L1 | Where-Object { $_.text -match "\] MATCH " })
foreach ($k in "spawn_after_pending", "team_assignment", "damage", "death", "respawn_delay", "team_score", "player_score", "score_limit_end", "time_limit_end", "tie", "match_end_ui", "reset", "second_match") {
    $exp = switch ($k) { "respawn_delay" { "$($X.match_tdm.respawn_delay_s.value) s" } "team_score" { "kill +$($X.match_tdm.kill_score.value.team)" } "score_limit_end" { "$($X.match_tdm.goal_score.value)" } "time_limit_end" { "$($X.match_tdm.time_limit_s.value) s" } "match_end_ui" { "UI event 9 -> EndGameStats_GFX, HUD hidden" } default { "RE 5" } }
    Res "MATCH" $k $(if ($matchEv.Count) { "INFO" } else { "SKIP" }) ("expected {0} ({1}). {2}" -f $exp, "m05\expectations.json match_tdm", $(if ($matchEv.Count) { "MATCH events present: run match-validator.ps1 -Log on run1" } else { "not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9)" })) "Gameplay"
}

# ================= report =================
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Milestone 05 end-to-end gate - $Root ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))"); $md.Add("")
$md.Add(("**PASS {0} / FAIL {1} / KNOWN {2} / INFO {3} / SKIP {4} / HUMAN-CHECK {5}**" -f $sum.pass, $sum.fail, $sum.known, $sum.info, $sum.skip, $sum.human)); $md.Add("")
foreach ($layer in "DATA", "STATE", "PRESENTED", "MATCH", "LIFETIME") {
    $md.Add("## $layer"); $md.Add(""); $md.Add("| status | check | owner | evidence |"); $md.Add("|---|---|---|---|")
    foreach ($rx in ($res | Where-Object { $_.id -like "e2e.$layer.*" })) { $md.Add(("| {0} | {1} | {2} | {3} |" -f $rx.status, ($rx.id -replace "^e2e\.$layer\.", ""), $rx.owner, (($rx.note -replace '\|', '/') -replace "`n", ' '))) }
    $md.Add("")
}
$md.Add("Evidence: run1_first_launch\ (flow.jsonl, wfc.log, captures + captures.csv), sheet_first_launch.png, run3_loops\process.csv. HUMAN checks: tools\fidelity\HUMAN-CHECK-M05.md.")
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "M05-GATE.md")
"M05 E2E GATE: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ") + " -> $OutDir"
