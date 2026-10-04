# Acceptance tests from the M05 human playtest: each reported problem becomes a repeatable check with an owner.
#
#   .\tools\fidelity\playtest-acceptance.ps1 -Root <work\ab\<target>> [-OutDir <dir>] [-Runs A1,A2,A3]
#
#   A1 boot / title / navigation (fresh profile, keys only)
#      #1 intro movies have video AND audio              #2 title / menu background is not black
#      #3 keyboard can advance the title                 #4 controller (HUMAN: XInput cannot be injected; map = DATA)
#      #5 parent / back navigation follows the shipped menus (HmMenu.closeMenu, BLK A3/A4)
#   A2 loading (outside-the-process periodic window capture: works while the load blocks the main loop)
#      #6 loading presentation keeps updating during the map load   #7 loading overlay gone at match start
#   A3 match state vs presentation (WFC_LIFECYCLE through Gameplay's rules; HUD movie dumps when it is open)
#      #14 clock runs  #15 score updates  #16 kill -> kill / death events  #17 respawn sequence
#      #18 kill feed receives an event  #19 end at the score rule  #20 result state exposed to the UI
#      #21 second match resets  #22 HUD values = Gameplay state  minimap / radar (evidence-dependent)
#   #8/#9/#10 character / vehicle jitter and vehicle states: motion-jitter.ps1; #11/#13 ramps / visible-geometry
#   collision: vehicle-collision.ps1; #12 under-map: transform-stress.ps1; #23-#25 lifetime / audio: m05-e2e-gate R4/R7.
#
# Status: PASS / FAIL (product) / KNOWN (documented gap, owner) / UNKNOWN (original behaviour not established) /
# WAITING (presentation or hook not landed: the check is ready) / HUMAN / SKIP. Owner "Experimental" on a FAIL = test fault.
param([Parameter(Mandatory)][string]$Root, [string]$OutDir = "", [string[]]$Runs = @("A1", "A2", "A3"), [ValidateSet("Release", "Debug")][string]$Config = "Release")
$ErrorActionPreference = "Stop"
$Runs = @($Runs | ForEach-Object { $_ -split "," } | Where-Object { $_ })
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path
if (-not $OutDir) { $OutDir = Join-Path (Get-WfcRoot) ("work\fidelity\acceptance\" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Release") { "build-release\bin\wfc_rebuild.exe" } else { "build\bin\wfc_rebuild.exe" })
$rd = Join-Path $Root "work\render"
$H = Get-ExeHooks $exe
$res = New-WfcResults
function Res($id, $status, $note, $owner = "", $m = $null) { Add-WfcResult $res "accept.$id" $status $m $note $owner }
function Env0($dir, [hashtable]$x = @{}) { $e = @{ WFC_BOOT = "frontend"; WFC_FLOWLOG = (Join-Path $dir "flow.jsonl"); WFC_FLOWSEED = "1"; WFC_FLOW_TIMEOUT = "900"; WFC_MUSICLOG = "1"; WFC_AMBLOG = "1" }; if (Test-Path $rd) { $e.WFC_RENDER_DATA = $rd }; foreach ($k in $x.Keys) { $e[$k] = $x[$k] }; return $e }
function DiffOf($a, $b) { return Shot-Diff $a $b }

# ============================================================== A1 boot / title / navigation
if ($Runs -contains "A1") {
    $d = Join-Path $OutDir "A1_boot_nav"; New-Item -ItemType Directory -Force $d | Out-Null
    $s = @("wait:t=5", (Shot $d "a_intro"), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2.5", (Shot $d "b_title"), "snapshot:title",
           "key:13", "wait:t=1.5", (Shot $d "c_title_after_enter"), "snapshot:after_enter",
           "key:114", "wait:t=1.5", (Shot $d "d_mainmenu"), "snapshot:after_start",
           (Keys @(40)), "key:13", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=2.5", (Shot $d "e_party_root"),
           (Keys @(40)), "key:13", "wait:t=1.5", (Shot $d "f_modes"), "snapshot:modes",
           "key:27", "wait:t=1.5", (Shot $d "g_party_after_back"), "snapshot:back1",
           "key:13", "wait:t=1.5", (Shot $d "h_modes_again"), "key:13", "wait:t=1.5", (Shot $d "i_hostoptions"),
           "key:27", "wait:t=1.5", (Shot $d "j_modes_after_back"), "snapshot:back2",
           "key:27", "wait:t=1.5", (Shot $d "k_party_after_back2"), "snapshot:back3",
           "key:27", "wait:t=1", "wait:level=FrontEnd", "wait:ui=FrontEnd", "wait:t=2.5", (Shot $d "l_frontend_after_back"), "snapshot:back4", "quit") -join ";"
    $r = Invoke-WfcSampled $exe $d (Env0 $d @{ WFC_FRONTEND_SCRIPT = $s }) 900 1.0
    $F = Read-FlowLog (Join-Path $d "flow.jsonl")
    $stopAt = (@(Flow-Ev $F "script.wait") | Select-Object -Last 1).cond
    # ---- #1 intro video + audio
    $ff = @(Flow-Ev $F "movie.firstFrame" | Where-Object { $_.movie -match 'Logo_|FMV_intro' }); $mo = @(Flow-Ev $F "movie.open" | Where-Object { $_.movie -match 'Logo_|FMV_intro' })
    $audEv = @($F | Where-Object { $_.ev -match '^movie\.audio' -or ($_.ev -eq "movie.open" -and ($_.PSObject.Properties.Name -contains "audio") -and "$($_.audio)" -notin "", "0", "false", "False", "none") })
    $ai = Shot-Stats (Join-Path $d "a_intro.bmp")
    Res "intro.video" $(if ($ff.Count -ge 4 -and $ai -and $ai.black -lt 0.97) { "PASS" } elseif ($ff.Count -ge 4) { "HUMAN" } else { "FAIL" }) ("intro movies with decoded frames {0}/4; frame at 5 s near-black {1:P0}" -f $ff.Count, $(if ($ai) { $ai.black } else { 1 })) "Frontend"
    Res "intro.audio" $(if ($audEv.Count) { "PASS" } else { "FAIL" }) ("movie audio evidence (movie.audio* events / movie.open audio=...): {0}. Human playtest: the intro has no sound. Frontend documents movie audio as PARTIAL (10 mono FLAC tracks, layout unidentified; MoviesToAlwaysPlaySound lists the logos) - owner Frontend for decode, Systems for playback on the one audio device, AssetTools / RE for the track layout" -f $audEv.Count) "Frontend/Systems"
    # ---- #2 title background
    $bt = Shot-Stats (Join-Path $d "b_title.bmp"); $mm = Shot-Stats (Join-Path $d "d_mainmenu.bmp")
    Res "title.background" $(if (-not $bt) { "SKIP" } elseif ($bt.black -lt 0.85) { "PASS" } else { "FAIL" }) ("title frame near-black {0:P0}, main menu {1:P0}. The original shows the UI_FrontEnd_m 3D scene (orbit camera, Cybertron, fireworks; RE 1.2 Kismet). Documented gap: UI_FrontEnd_m not exported (AssetTools) / not rendered (Frontend / Rendering)" -f $bt.black, $(if ($mm) { $mm.black })) "AssetTools/Frontend/Rendering"
    # ---- #3 keyboard on the title (CONFIRMED: FrontEnd_GFX sprite 23 inputEvent reacts to 'buttonStart' on release only)
    $sdBefore = @(Flow-Ev $F "bridge" | Where-Object { $_.fn -eq "Online.ShowDeviceSelectionUI" -and [int]$_.seq -lt [int]((@(Flow-Ev $F "snapshot" | Where-Object why -eq "after_enter"))[0].seq) })
    $sdAfter = @(Flow-Ev $F "bridge" | Where-Object { $_.fn -eq "Online.ShowDeviceSelectionUI" })
    Res "title.start_advances" $(if ($sdAfter.Count -and -not $sdBefore.Count) { "PASS" } elseif ($sdBefore.Count) { "INFO" } else { "FAIL" }) ("Start (keyboard F3) -> ShowDeviceSelectionUI + main menu: {0}; A (Enter) before it advanced: {1}. CONFIRMED original: the title reacts to buttonStart only (FrontEnd_GFX inputEvent), so Enter doing nothing is original; keyboard Start is F3 in the rebuild's PROVISIONAL key map - usability decision for Frontend, not a fidelity defect" -f [bool]$sdAfter.Count, [bool]$sdBefore.Count) "Frontend"
    # ---- #4 controller
    Res "controller" "HUMAN" "XInput is mapped (Win32Window: A/B/X/Y/Start/Back/D-pad/shoulders/triggers/thumbs -> UiKey) but pad input cannot be injected safely; play the whole route with a controller" "Frontend"
    # ---- #5 back navigation (HmMenu.closeMenu: show the parent, give it input, BUTTON_BACK; party root B = Game.QuitToMainMenu)
    $backs = @(Flow-Ev $F "ui.sound" | Where-Object { $_.name -eq "BUTTON_BACK" })
    $dP = DiffOf (Join-Path $d "e_party_root.bmp") (Join-Path $d "g_party_after_back.bmp"); $dM = DiffOf (Join-Path $d "f_modes.bmp") (Join-Path $d "j_modes_after_back.bmp"); $dP2 = DiffOf (Join-Path $d "e_party_root.bmp") (Join-Path $d "k_party_after_back2.bmp")
    $quitTravel = @(Flow-Ev $F "travel" | Where-Object { $_.from -eq "PartyLobby" -and $_.url -like "UI_FrontEnd_m*" })
    $qtm = @(Flow-Ev $F "bridge" | Where-Object fn -eq "Game.QuitToMainMenu")
    Res "nav.back_mode_list" $(if ($dP -ge 0 -and $dP -lt 6) { "PASS" } elseif ($dP -lt 0) { "SKIP" } else { "FAIL" }) ("B in the mode list returns to the party lobby root: frame difference to the root {0} luma (focus highlight may differ)" -f $dP) "Frontend"
    Res "nav.back_host_options" $(if ($dM -ge 0 -and $dM -lt 6) { "PASS" } elseif ($dM -lt 0) { "SKIP" } else { "FAIL" }) ("B in the host options returns to the mode list: difference {0}" -f $dM) "Frontend"
    Res "nav.back_twice" $(if ($dP2 -ge 0 -and $dP2 -lt 6) { "PASS" } elseif ($dP2 -lt 0) { "SKIP" } else { "FAIL" }) ("B twice from the host options returns to the party root: difference {0}" -f $dP2) "Frontend"
    Res "nav.back_party_root" $(if ($qtm.Count -and $quitTravel.Count) { "PASS" } else { "FAIL" }) ("B at the party root -> Game.QuitToMainMenu {0} -> travel to UI_FrontEnd_m {1} (BLK A3)" -f $qtm.Count, $quitTravel.Count) "Frontend"
    Res "nav.back_sound" $(if ($backs.Count -ge 4) { "PASS" } else { "FAIL" }) ("BUTTON_BACK requested {0} times for 4 B presses (BLK A4)" -f $backs.Count) "Frontend"
    Res "nav.flow_completed" $(if (@(Flow-Ev $F "snapshot" | Where-Object why -eq "back4").Count) { "PASS" } else { "FAIL" }) ("navigation script {0}" -f $(if (@(Flow-Ev $F "snapshot" | Where-Object why -eq "back4").Count) { "completed" } else { "stopped at '$stopAt'" })) "Frontend"
    $tiles = @(Get-ChildItem $d -Filter *.bmp | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = $_.BaseName } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "A1_sheet.png") 4 400 225 }
}

# ============================================================== A2 loading presentation
if ($Runs -contains "A2") {
    $d = Join-Path $OutDir "A2_loading"; New-Item -ItemType Directory -Force $d | Out-Null
    $s = "wait:frontend;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;call:Online.SetSelectedMapID,508;call:Online.BeginLobbyExitCountdown;wait:loading=1;" + (Shot $d "a_loading_start") + ";wait:level=Match;" + (Shot $d "b_match_first") + ";wait:ui=InGame;wait:t=2;" + (Shot $d "c_ingame") + ";wait:t=2;quit"
    $r = Invoke-WfcPeriodic $exe $d (Env0 $d @{ WFC_FRONTEND_SCRIPT = $s; WFC_SKIPINTRO = "1" }) 600 0.25
    $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $lines = @(Get-Content (Join-Path $d "flow.jsonl"))
    $ls = @(Flow-Ev $F "loading.start" | Where-Object kind -eq "Map")[0]; $lc = @(Flow-Ev $F "loading.close" | Where-Object kind -eq "Map")[0]
    $iS = if ($ls) { [Array]::FindIndex($lines, [Predicate[string]] { param($q) $q -match ('"seq":' + $ls.seq + ',') }) + 1 } else { -1 }
    $iC = if ($lc) { [Array]::FindIndex($lines, [Predicate[string]] { param($q) $q -match ('"seq":' + $lc.seq + ',') }) + 1 } else { -1 }
    $during = @($r.captures | Where-Object { $_.file -and $_.flow_lines -ge $iS -and $_.flow_lines -lt $iC })
    $ctlC = @($r.captures | Where-Object { $_.file -and $_.flow_lines -ge $iC } | Select-Object -Last 1)[0]
    $ctl = if ($ctlC) { Shot-Stats (Join-Path $d $ctlC.file) } else { $null }
    $capOk = $ctl -and $ctl.flat -lt 0.9 -and $ctl.black -lt 0.97
    $mld = @(Flow-Ev $F "match.loaded")[0]
    if (-not $capOk) { Res "loading.updates_during_load" "SKIP" ("window capture unavailable (control frame after the load uniform or missing); {0} captures in the load window. Run with the window visible on an unlocked desktop" -f $during.Count) "Experimental" }
    else {
        $diffs = @(); for ($k = 1; $k -lt $during.Count; $k++) { $diffs += DiffOf (Join-Path $d $during[$k - 1].file) (Join-Path $d $during[$k].file) }
        $changing = @($diffs | Where-Object { $_ -gt 0.8 }).Count
        $span = if ($during.Count -ge 2) { $during[-1].t - $during[0].t } else { 0 }
        Res "loading.updates_during_load" $(if ($during.Count -lt 3) { "SKIP" } elseif ($changing -ge [Math]::Max(2, ($diffs.Count * 0.3))) { "PASS" } else { "FAIL" }) ("{0} window captures over {1:N1} s of map load ({2} s blocking per match.loaded); consecutive captures that change: {3}/{4}. A frozen loading screen = the load blocks the presentation loop (Frontend documents the blocking load as PARTIAL; RE: native threaded load + Bink underlay keeps animating)" -f $during.Count, $span, $mld.seconds, $changing, $diffs.Count) "Frontend/Integration"
        if ($during.Count) { $tiles = @($during | Select-Object -First 12 | ForEach-Object { @{ png = (Join-Path $d $_.file); label = "t=$($_.t)" } }); New-WfcSheet $tiles (Join-Path $OutDir "A2_loading_sheet.png") 4 320 180 }
    }
    $gi = Shot-Stats (Join-Path $d "c_ingame.bmp"); $gl = if ($during.Count) { Join-Path $d $during[-1].file } else { $null }
    Res "loading.overlay_gone" $(if (-not $gi) { "SKIP" } elseif ($gi.black -lt 0.85) { "PASS" } else { "FAIL" }) ("in-game frame 2 s after InGame: near-black {0:P0}, mean luma {1} (the loading underlay was composited over the match until Integration 7a11ce2)" -f $gi.black, $gi.mean) "Frontend/Integration"
}

# ============================================================== A3 match state vs presentation
if ($Runs -contains "A3") {
    $d = Join-Path $OutDir "A3_hud_table"; New-Item -ItemType Directory -Force $d | Out-Null
    if (-not $H.Contains("WFC_LIFECYCLE")) { Res "hud.table" "SKIP" "WFC_LIFECYCLE not in this exe" "Integration" }
    else {
        $s = "wait:frontend;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;call:Online.SetSelectedMapID,508;call:Online.BeginLobbyExitCountdown;wait:level=Match;" +
             "wait:ui=InGame;wait:t=1;dump:Hud;" + (Shot $d "a_ingame") + ";snapshot:ingame;wait:t=3;dump:Hud;wait:ui=Spectating;wait:t=0.5;dump:Respawn;" + (Shot $d "b_spectating") + ";snapshot:spectating;wait:ui=InGame;wait:t=0.5;dump:Hud;" +
             "wait:ui=GameEnded;wait:t=2;dump:EndGame;" + (Shot $d "c_endgame") + ";snapshot:ended;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=1.5;dump:Hud;snapshot:match2;" + (Shot $d "d_match2") + ";quit"
        $r = Invoke-WfcSampled $exe $d (Env0 $d @{ WFC_FRONTEND_SCRIPT = $s; WFC_SKIPINTRO = "1"; WFC_LIFECYCLE = "3" }) 900 1.0
        $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $log = Join-Path $d "wfc.log"
        $timer = @(Grep-Log $log '\] MATCH timer remaining_s=(\d+)' | ForEach-Object { [int][regex]::Match($_.text, 'remaining_s=(\d+)').Groups[1].Value })
        $dec = 0; for ($k = 1; $k -lt $timer.Count; $k++) { if ($timer[$k] -eq $timer[$k - 1] - 1) { $dec++ } }
        $kills = @(Flow-Ev $F "match.kill"); $mk = @(Grep-Log $log '\] MATCH kill '); $md = @(Grep-Log $log '\] MATCH death '); $sc = @(Grep-Log $log '\] MATCH score '); $rs = @(Grep-Log $log '\] MATCH respawn '); $en = @(Grep-Log $log '\] MATCH end ')
        $hudOpen = @(@(Flow-Ev $F "gfx.movie") + @(Flow-Ev $F "ui.open") | Where-Object { $_.movie -like "*Hud*" -and ("$($_.opened)" -ne "False") })
        $dumps = @(Grep-Log $log 'GFX DUMP .*Hud')
        $feed = @($F | Where-Object { ($_.ev -match 'gfx\.(call|invoke|push)|hud\.' -and "$($_.PSObject.Properties.Value)" -match 'PointEvent|GameAnnouncement|RewardAnnouncement') })
        $ended = @(Flow-Ev $F "snapshot" | Where-Object why -eq "ended")[0]
        $unh = @(Flow-Ev $F "datastore.unhandled" | Where-Object { $ended -and [int]$_.seq -gt [int]((@(Flow-Ev $F "ui.state" | Where-Object to -eq "GameEnded"))[0].seq) -and [int]$_.seq -lt [int]$ended.seq } | ForEach-Object { "$($_.fn) $($_.markup)" } | Select-Object -Unique)
        $hudRows = @(
            [pscustomobject]@{ item = "clock"; source = ("MATCH timer: {0} samples, {1} 1-s decrements, first {2} last {3}" -f $timer.Count, $dec, $(if ($timer.Count) { $timer[0] }), $(if ($timer.Count) { $timer[-1] })); presentation = $(if ($hudOpen.Count) { "Hud_GFX open; dump lines $($dumps.Count)" } else { "Hud_GFX not opened" }) }
            [pscustomobject]@{ item = "team score"; source = (($sc | ForEach-Object { $_.text -replace '^.*MATCH score ', '' }) -join " | "); presentation = $(if ($hudOpen.Count) { "see dumps" } else { "not drawn" }) }
            [pscustomobject]@{ item = "kill event"; source = ("{0} MATCH kill, {1} flow match.kill" -f $mk.Count, $kills.Count); presentation = ("PointEvent pushes traced: {0}" -f $feed.Count) }
            [pscustomobject]@{ item = "death / respawn"; source = ("{0} deaths, {1} respawns ({2})" -f $md.Count, $rs.Count, (($rs | ForEach-Object { [regex]::Match($_.text, 'delay_s=([\d.]+)').Groups[1].Value }) -join ",")); presentation = ("Spectating UI {0}x" -f @(Flow-Ev $F "ui.state" | Where-Object to -eq "Spectating").Count) }
            [pscustomobject]@{ item = "match end / winner"; source = (($en | ForEach-Object { $_.text -replace '^.*MATCH end ', '' }) -join " | "); presentation = ("GameEnded {0}x; EndGameStats unhandled reads: {1}" -f @(Flow-Ev $F "ui.state" | Where-Object to -eq "GameEnded").Count, ($unh -join "; ")) }
        )
        Write-WfcCsv $hudRows (Join-Path $d "hud_table.csv")
        Res "match.clock_runs" $(if ($timer.Count -ge 5 -and $dec -ge $timer.Count * 0.8) { "PASS" } else { "FAIL" }) ("Gameplay RemainingTime: {0} samples, {1} 1-s decrements (GRI 1 Hz timer)" -f $timer.Count, $dec) "Gameplay"
        Res "match.score_updates" $(if ($sc.Count -ge 3) { "PASS" } else { "FAIL" }) ("team score lines: " + (($sc | Select-Object -First 6 | ForEach-Object { $_.text -replace '^.*MATCH score ', '' }) -join " | ")) "Gameplay"
        Res "match.kill_death_events" $(if ($mk.Count -and $md.Count -ge $mk.Count) { "PASS" } else { "FAIL" }) ("MATCH kill {0}, MATCH death {1}, flow match.kill {2}" -f $mk.Count, $md.Count, $kills.Count) "Gameplay/Integration"
        Res "match.respawn_sequence" $(if ($rs.Count -and @(Flow-Ev $F "ui.state" | Where-Object to -eq "Spectating").Count) { "PASS" } else { "FAIL" }) ("respawns {0}; Spectating UI shown" -f $rs.Count) "Gameplay/Integration"
        Res "match.end_rule" $(if (@($en | Where-Object { $_.text -match 'reason=score_limit' }).Count) { "PASS" } else { "FAIL" }) (($en | ForEach-Object { $_.text -replace '^.*MATCH end ', '' }) -join " | ") "Gameplay"
        Res "match.result_exposed" $(if (-not $ended) { "FAIL" } elseif ($unh.Count) { "PARTIAL" } else { "PASS" }) ("EndGameStats: data-store reads the rebuild does not answer: {0} (Integration documents 'undefined / +NaN' in the experience panel: no profile / XP service)" -f $(if ($unh.Count) { $unh -join "; " } else { "none" })) "Frontend/Integration"
        Res "match.second_match_reset" $(if (@(Flow-Ev $F "snapshot" | Where-Object why -eq "match2").Count -and @(Grep-Log $log '\] MATCH restart').Count) { "PASS" } else { "FAIL" }) "second match from the lobby: MATCH restart + InGame reached (score reset is checked by m05-e2e-gate R7)" "Gameplay"
        Res "hud.clock_displayed" $(if ($hudOpen.Count -and $dumps.Count) { "HUMAN" } else { "WAITING" }) ("Hud_GFX (clock / team score / health / ammo, BLK G) open in play: {0}; dumps {1}. Until the HUD movie is drawn, displayed vs Gameplay values cannot be compared (owner Frontend / Rendering: HUD ownership handoff)" -f [bool]$hudOpen.Count, $dumps.Count) "Frontend/Rendering"
        Res "hud.score_displayed" $(if ($hudOpen.Count -and $dumps.Count) { "HUMAN" } else { "WAITING" }) "team score bars (<CurrentGame:Teams> polled every 500 ms, BLK G) - waiting for the HUD movie" "Frontend/Rendering"
        $dm = @($F | Where-Object { $_.ev -match '(?i)kill.?feed|death.?message|localmessage' }) + @(Grep-Log $log '(?i)\] (KILLFEED|DEATHMSG|DEATH MESSAGE)')
        Res "hud.kill_feed_event" $(if ($dm.Count -ge $mk.Count -and $dm.Count) { "PASS" } elseif ($mk.Count) { "WAITING" } else { "SKIP" }) ("kills {0}; kill-feed (TnDeathMessage: DeathMessage(killer, victim) / SuicideMessage, 3.0 s lifetime, CONFIRMED) entries traced: {1}. Gameplay 391b7f0 exposes HudGameState::killFeed (TDMTEST 39/39); presentation (text / colour) is Frontend / Rendering - WAITING until a trace or drawn feed exists" -f $mk.Count, $dm.Count) "Frontend/Rendering"
        Res "hud.point_event" $(if ($feed.Count) { "PASS" } elseif ($hudOpen.Count) { "FAIL" } else { "WAITING" }) ("kill -> _global.PointEvent (TnHUD KillTransactionObserver, CONFIRMED authored) pushes traced: {0}" -f $feed.Count) "Frontend"
        Res "hud.values_match_gameplay" $(if ($hudOpen.Count -and $dumps.Count) { "HUMAN" } else { "WAITING" }) "HUD text fields vs MATCH timer / score lines (hud_table.csv) - waiting for the HUD movie" "Frontend"
        Res "hud.minimap" "UNKNOWN" "original minimap / radar not established: AssetTools future_hud_handoff 'radar_minimap_assets: none found in authored.db' (CONFIRMED AUTHORED DATA) supports absence; RE has not confirmed. Not a failure. Retire when RE confirms none; validate if RE finds one" "RE"
        foreach ($sn in "a_ingame", "b_spectating", "c_endgame", "d_match2") { $st = Shot-Stats (Join-Path $d "$sn.bmp"); if ($st) { Res "present.$sn" $(if ($st.black -ge 0.97) { "FAIL" } else { "HUMAN" }) ("{0}: near-black {1:P0}" -f $st.file, $st.black) "Frontend" } }
        $tiles = @(Get-ChildItem $d -Filter *.bmp | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = $_.BaseName } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "A3_sheet.png") 4 400 225 }
    }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Playtest acceptance ($Config)"); $md.Add(""); $md.Add("exe ``$exe``"); $md.Add("")
$md.Add(("**" + (($sum.Keys | ForEach-Object { "$($_.ToUpper()) $($sum[$_])" }) -join " / ") + "**")); $md.Add(""); $md.Add("| status | check | owner | evidence |"); $md.Add("|---|---|---|---|")
foreach ($x in $res) { $md.Add(("| {0} | {1} | {2} | {3} |" -f $x.status, ($x.id -replace '^accept\.', ''), $x.owner, ($x.note -replace '\|', '/'))) }
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "ACCEPTANCE.md")
"PLAYTEST ACCEPTANCE: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ") + " -> $OutDir"
