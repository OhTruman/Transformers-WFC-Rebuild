# PRESENTATION GATE: is what a player SEES healthy? Rejects catastrophic presentation failures that asset counts, draw
# counters, events and exit codes cannot see (Milestone 06 human playtest: Streets world missing behind the HUD, menus
# persisting over play, soft-locked screens, malformed frontend geometry).
#
#   .\tools\fidelity\presentation-gate.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Config Debug|Release] [-RenderData <dir>]
#                                          [-Parts route,direct,reference,watchdog] [-Reference <dir>] [-MapId 508]
#
# Defaults follow the human: Debug exe (Integration's playtest build is Debug), the PC SKU, the product's default render
# data (<exe>\..\..\work\render) unless -RenderData, the real frontend route. Every part is its own process with a
# watchdog (timeout + last script step), so one soft-lock cannot hide the others.
#
# Parts
#   route      frontend -> lobby -> map -> loading -> character select (keys) -> spawn -> moving -> pause -> resume ->
#              quit to frontend -> second match. Judged per state: frontend 3D scene coverage, menu screens not blank,
#              gameplay WORLD coverage with HUD and player excluded, pause overlay cleared on resume, gameplay not moving
#              under an exclusive menu, selected body resolved, second match equivalent to the first.
#   direct     the same map and start by direct boot, the player's own camera: a frontend-launched world must not lose
#              content against the direct-boot world (route-only defects are Integration seams).
#   reference  fixed cameras behind 13 Streets team starts vs a known-good reference set (default M05 1e14900):
#              a strong structural difference flags the WHOLE merged visual pipeline, whatever the map count says.
#   watchdog   Extras -> Movies / Credits, Accounts -> Create (typed text must arrive), Settings -> Controls (a rebind
#              must change the binding), Create a Character (preview, exit), repeated forward / back. Every screen is
#              classified SCREEN PRESENT / DISPLAY CORRECT / INTERACTION WORKING / FULLY FUNCTIONAL; no exit = SOFT LOCK.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("Debug", "Release")][string]$Config = "Debug",
      [string]$RenderData = "", [string[]]$Parts = @(), [string]$Reference = "", [int]$MapId = 508, [string]$Label = "", [switch]$ReportOnly)   # -ReportOnly: judge existing evidence in -OutDir, run nothing
$ErrorActionPreference = "Stop"
$Parts = @($Parts | ForEach-Object { $_ -split "," } | Where-Object { $_ }); if (-not $Parts.Count) { $Parts = @("route", "direct", "reference", "watchdog") }
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Shots.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
if (-not (Test-Path $exe)) { throw "no $Config exe under $Root" }
if ($RenderData) { $RenderData = (Resolve-Path $RenderData).Path }
if (-not $Reference) { $Reference = Join-Path $PSScriptRoot "references\streets_spawn_m05_1e14900" }
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$H = Get-ExeHooks $exe
# Frontend pass 4 routing (CONFIRMED original per Frontend): Quit asks TnQuitMessageBox Yes / No; quitting a match returns
# to the PARTY LOBBY; Back from the party lobby (+ Yes) returns to the title.
# text entry: "type:<text>" (WM_CHAR-equivalent, Frontend f5ada69+) when the build has it, else key:<code> per letter
$typeStep = [bool](Get-ChildItem (Join-Path $Root "src") -Recurse -Include *.cpp -ErrorAction SilentlyContinue | Select-String -Pattern '"type:"' -SimpleMatch -List | Select-Object -First 1)
$quitBox = [bool](Get-ChildItem (Join-Path $Root "src") -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue | Select-String -Pattern "TnQuitMessageBox" -SimpleMatch -List | Select-Object -First 1)
function WaitGpu { . (Join-Path $PSScriptRoot "lib\M07.ps1"); if (-not (Wait-WfcGpu)) { throw "GPU busy past WFC_GATE_GPU_WAIT_MIN: refusing to start a second Experimental renderer" } }   # shared policy (lib\M07.ps1)
$res = New-WfcResults
function Res($id, $status, $note, $owner = "", $m = $null) { Add-WfcResult $res "present.$id" $status $m $note $owner }
$MapDirs = @{ 501 = "MP_IAC_Seed"; 502 = "MP_IAC_Berth"; 503 = "MP_UND_Complex"; 504 = "MP_IAC_Rust"; 507 = "MP_ORB_Debris"; 508 = "MP_IAC_Streets"; 509 = "MP_KON_Molten"; 510 = "MP_UND_Gorge" }
$mapDir = $MapDirs[$MapId]
function BaseEnv([string]$d, [hashtable]$x = @{}) {
    $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOWSEED = "1"; WFC_FLOW_TIMEOUT = "420" }
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    foreach ($k in $x.Keys) { $e[$k] = $x[$k] }; return $e
}
function LastStep([string]$d) { $F = Read-FlowLog (Join-Path $d "flow.jsonl"); return (@(Flow-Ev $F "script.wait") + @(Flow-Ev $F "script.ui") | Sort-Object { [int]$_.seq } | Select-Object -Last 1) }
function Sheet([string]$d, [string]$name) { $t = @(Get-ChildItem $d -Filter *.bmp | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = "$name $($_.BaseName)" } }); if ($t.Count) { New-WfcSheet $t (Join-Path $OutDir "sheet_$name.png") 4 400 225 } }
$rows = New-Object System.Collections.Generic.List[object]
function Row($part, $state, $file, $verdict, $m) { $rows.Add([pscustomobject][ordered]@{ part = $part; state = $state; file = $file; verdict = $verdict; detail = $m.detail; black = $m.black; maxFlat = $m.maxFlat; edges = $m.edges }) }

# =========================================================================================== route
$routeSpawn = $null; $routeWorld = @{}
if ($Parts -contains "route") {
    $d = Join-Path $OutDir "route"; New-Item -ItemType Directory -Force $d | Out-Null
    $match = { param($p)
        @("call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=2.5", "shot:$d\${p}02_party.bmp",
          "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2", "call:Online.SetSelectedMapID,$MapId", "wait:t=1.5", "shot:$d\${p}03_lobby.bmp",
          "call:Online.BeginLobbyExitCountdown", "wait:loading=1", "wait:t=1", "shot:$d\${p}04_loading.bmp", "wait:level=Match", "wait:movie=CustomTransformers", "wait:t=2", "shot:$d\${p}05_charselect.bmp", "dump:CustomTransformers",
          "ui:Down", "wait:t=0.8", "shot:$d\${p}06_charselect_down.bmp", "ui:Accept", "wait:ui=InGame", "wait:t=1.5", "shot:$d\${p}07_spawn.bmp", "wait:t=3", "shot:$d\${p}08_moving.bmp", "wait:t=3", "shot:$d\${p}09_moving2.bmp",
          "showmenu", "wait:ui=Paused", "wait:t=1.5", "shot:$d\${p}10_paused.bmp", "wait:t=1", "shot:$d\${p}10b_paused_later.bmp", "ui:Accept", "wait:ui=InGame", "wait:t=1.5", "shot:$d\${p}11_resumed.bmp", "wait:t=2", "shot:$d\${p}12_resumed_later.bmp",
          "showmenu", "wait:ui=Paused", "wait:t=0.5", "call:Game.QuitToMainMenu", $(if ($quitBox) { "wait:t=1.5;shot:$d\${p}12b_quitbox.bmp;ui:Accept;wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;shot:$d\${p}12c_party_after.bmp;ui:Back;wait:t=1.5;ui:Accept;wait:level=FrontEnd;wait:ui=FrontEnd" } else { "wait:level=FrontEnd;wait:ui=FrontEnd" }), "wait:t=3", "shot:$d\${p}13_frontend_after.bmp") -join ";" }
    $s = @("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=3", "shot:$d\a00_title.bmp", "wait:t=6", "shot:$d\a01_title_9s.bmp", (& $match "a"), (& $match "b"), "quit") -join ";"
    if (-not $H.Contains("WFC_CHARSELECT")) { $s = $s.Replace("wait:movie=CustomTransformers;", "").Replace(";dump:CustomTransformers;ui:Down;wait:t=0.8;", ";").Replace("ui:Accept;wait:ui=InGame", "wait:ui=InGame") }   # builds before character selection: the match starts on its own
    $e = BaseEnv $d @{ WFC_FRONTEND_SCRIPT = $s; WFC_CHARSELECT = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.25"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "10" }
    $r = if ($ReportOnly) { [pscustomobject]@{ rc = "n/a"; timedOut = "n/a" } } else { WaitGpu; Invoke-WfcSampled $exe $d $e 900 1.0 }
    $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $log = Join-Path $d "wfc.log"; $done = @(Flow-Ev $F "script.wait" | Where-Object cond -eq "t=3").Count -ge 3 -and (Test-Path "$d\b13_frontend_after.bmp")
    $ls = LastStep $d
    Res "route.completes" $(if ($done) { "PASS" } else { "FAIL" }) ("frontend -> {0} -> pause / resume -> frontend -> second match: {1}" -f $mapDir, $(if ($done) { "completed" } else { "stopped at '$($ls.cond)$($ls.action)' (exit $($r.rc), timed out $($r.timedOut)) - a soft lock or a missing transition" })) "Integration"
    # --- frontend title: the authored 3D scene must occupy the area left of the menu, not black / blank / one giant shape
    foreach ($t in "a00_title", "a01_title_9s", "a13_frontend_after", "b13_frontend_after") {
        $m = Present-Measure "$d\$t.bmp" $script:PresentRegions.title_scene; if (-not $m) { continue }; $v = Present-SceneVerdict $m; Row "route" $t "$t.bmp" $v $m
        Res "frontend.scene.$t" $(if ($v -eq "PARTIAL") { "HUMAN" } else { $v }) ("title 3D scene region: detail {0}, black {1}, largest blank area {2}, untextured / placeholder surface largest {4} total {5}, noise texture {6} (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)" -f $m.detail, $m.black, $m.maxFlat, $m.edges, $m.untexMax, $m.untexFrac, $m.noise) "Frontend/Rendering"
    }
    # --- menu screens: not blank / grey; lobby scene area not one flat slab
    foreach ($t in "a02_party", "a03_lobby", "a04_loading", "b02_party", "b03_lobby") {
        $m = Present-Measure "$d\$t.bmp" $script:PresentRegions.full; if (-not $m) { continue }; $v = Present-UiVerdict $m; Row "route" $t "$t.bmp" $v $m
        $sc = if ($t -like "*0[23]_*") { Present-Measure "$d\$t.bmp" $script:PresentRegions.lobby_scene } else { $null }
        $grey = if ($sc) { FlatGreyFraction "$d\$t.bmp" } else { 0 }
        # The original party / game lobby backdrop is SPARSE (CONFIRMED, Frontend from authored.db): UI_PartyLobby_m / UI_Lobby_m
        # have no geometry; the scene is UI_CharacterCustomization_m's SpaceDome_STAT + four CybertronCard_STAT planes and the
        # emblem MaterialInstanceActors; its robots are authored hidden (they appear only in Create a Character). A dark dome
        # with large quiet areas is correct (measured: detail 0.07-0.12, largest blank 0.42-0.58); a clear colour (detail 0,
        # blank 1.0) or an untextured grey slab is not. No room or characters are expected here.
        $lv = if ($sc) { @(Flow-Ev $F "scene.levels" | Where-Object { "$($_.uiLevel)" -match $(if ($t -like "*party*") { "UI_PartyLobby" } else { "^UI_Lobby" }) }) } else { @() }
        $domeLv = @($lv | Where-Object { "$($_.levels)" -match "UI_CharacterCustomization_m" -and "$($_.drawn)" -eq "True" }).Count -gt 0
        $clear = $sc -and ($sc.detail -lt 0.02 -or $sc.maxFlat -ge 0.95)
        $v2 = if ($v -eq "FAIL") { "FAIL" } elseif ($sc -and ($clear -or $grey -gt 0.15)) { "FAIL" } elseif ($sc -and $lv.Count -and -not $domeLv) { "FAIL" } else { "PASS" }
        Res "frontend.screen.$t" $v2 ("screen: detail {0}, black {1}, largest blank {2}, untextured {4} / {5}, noise {6}{3}" -f $m.detail, $m.black, $m.maxFlat, $(if ($sc) { "; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail {0}, largest blank {1} (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey {2:P0}; dome/cards level UI_CharacterCustomization_m drawn: {3}; emblem glow and no robots outside Create a Character: HUMAN" -f $sc.detail, $sc.maxFlat, $grey, $(if ($lv.Count) { $domeLv } else { "no scene.levels trace" }) }), $m.untexMax, $m.untexFrac, $m.noise) "Frontend/Rendering"
    }
    # --- character select: UI visible, preview / body resolution
    foreach ($p in "a", "b") {
        $m = Present-Measure "$d\${p}05_charselect.bmp" $script:PresentRegions.full
        if ($m) { $dm = @((Read-GfxDumps $log) | Where-Object { $_.movie -like "*CustomTransformers*" }); $has = @($dm | Where-Object { @($_.texts | Where-Object { $_ -match '(?i)choose character|scout|scientist|leader|soldier' }).Count })
            Res "charselect.ui.$p" $(if ((Present-UiVerdict $m) -eq "FAIL") { "FAIL" } elseif ($dm.Count -and -not $has.Count) { "FAIL" } else { "PASS" }) ("Choose Character screen: detail {0}; dump texts with class names: {1}" -f $m.detail, $(if ($dm.Count) { $has.Count } else { "no dump (movie not reachable by dump:)" })) "Frontend" }
    }
    $sel = @(Flow-Ev $F "match.characterSelected"); $bodies = @(Grep-Log $log 'skinned glb: .*?/Characters/(\S+?)/robot\.glb'); $spawnL = @(Grep-Log $log '\] MATCH spawn ')
    if ($sel.Count) {
        $chassis = @($spawnL | ForEach-Object { [regex]::Match($_.text, 'chassis=(\S+)').Groups[1].Value }); $body = @($bodies | ForEach-Object { [regex]::Match($_.text, 'Characters/(\S+?)/robot').Groups[1].Value } | Select-Object -Unique)
        Res "charselect.gameplay_receives" $(if ($chassis.Count -and $chassis[0] -eq $sel[0].chassis) { "PASS" } else { "FAIL" }) ("selected {0} ({1}); Gameplay spawn chassis {2}" -f $sel[0].name, $sel[0].chassis, ($chassis -join ",")) "Gameplay"
        Res "charselect.body_resolves" $(if ($body.Count -and $body -notcontains "Optimus") { "PASS" } elseif ($sel[0].chassis -match '^Truck') { "INFO" } else { "FAIL" }) ("selected chassis {0}; robot body loaded: {1} (Optimus for a non-Optimus selection = the selection is not drawn)" -f $sel[0].chassis, ($body -join ",")) "AssetTools/Gameplay"
    } elseif (-not $H.Contains("WFC_CHARSELECT")) { Res "charselect.gameplay_receives" "SKIP" "this build has no WFC_CHARSELECT hook: the match auto-selects; selection cannot be exercised" "Experimental" }
    else { Res "charselect.gameplay_receives" "FAIL" "no match.characterSelected: the selection never reached the match (soft lock or skipped screen)" "Frontend" }
    # --- gameplay world: coverage with the HUD band and the player character excluded
    foreach ($t in "a07_spawn", "a08_moving", "a09_moving2", "b07_spawn", "b08_moving", "b09_moving2") {
        $w = Present-World "$d\$t.bmp"; if (-not $w) { continue }; $v = Present-WorldVerdict $w; Row "route" $t "$t.bmp" $v $w; $routeWorld[$t] = $w
        Res "gameplay.world.$t" $v ("world region (HUD band and player excluded): textured detail {0} (PASS >= 0.15, FAIL < 0.10), black {1}, largest blank {2}, untextured {3} / {4}, noise {5}" -f $w.detail, $w.black, $w.maxFlat, $w.untexMax, $w.untexFrac, $w.noise) "Rendering/Integration"
    }
    # --- second match equivalent to the first (load / unload / load)
    foreach ($k in "07_spawn", "08_moving") { $wa = $routeWorld["a$k"]; $wb = $routeWorld["b$k"]
        if ($wa -and $wb) { $ratio = if ($wa.detail -gt 0.01) { [Math]::Round($wb.detail / $wa.detail, 2) } else { 0 }
            Res "transition.second_match.$k" $(if ($ratio -lt 0.6 -or $ratio -gt 1.7) { "FAIL" } elseif ((Present-WorldVerdict $wb) -eq "FAIL") { "FAIL" } else { "PASS" }) ("world detail match 1 {0} vs match 2 {1} (ratio {2}; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)" -f $wa.detail, $wb.detail, $ratio) "Integration/Rendering" } }
    # --- pause overlay cleared on resume (frontend state must disappear when gameplay owns presentation)
    foreach ($p in "a", "b") {
        $pa = "$d\${p}10_paused.bmp"; $re = "$d\${p}12_resumed_later.bmp"; $mv = "$d\${p}09_moving2.bmp"
        $sp = Present-Similar $re $pa; $sm = Present-Similar $re $mv
        if ($sp -and $sm) { $persist = $sp.grad -gt 0.55 -and $sp.grad -gt $sm.grad + 0.1
            Res "transition.pause_cleared.$p" $(if ($persist) { "FAIL" } else { "PASS" }) ("3.5 s after Resume: structural similarity to the PAUSE frame {0}, to the gameplay frame before pause {1} (FAIL: the frame still looks like the pause screen)" -f $sp.grad, $sm.grad) "Frontend" }
    }
    # --- exclusivity. NOTE: WFC_AUTOWALK is injected AFTER the product's menu input gate (Application.cpp: input is cleared
    # while the UI state is not InGame, then the test hook adds Forward), so pawn motion under a menu is NOT evidence of a
    # product defect. What is valid: the UI state the product reports. If the state is InGame (real input goes to gameplay)
    # while a menu is still drawn, gameplay accepts movement under a visible menu.
    $mot = Read-MotionByUiState $log
    $persistA = @($res.ToArray() | Where-Object { $_.id -like "present.transition.pause_cleared.*" -and $_.status -eq "FAIL" }).Count
    $resumedInGame = @(Flow-Ev $F "ui.state" | Where-Object { $_.from -eq "Paused" -and $_.to -eq "InGame" }).Count
    $movedAfter = @($mot | Where-Object { $_.state -eq "InGame" } | Select-Object -Skip 1 | Where-Object { $_.dist -gt 1.0 }).Count
    Res "exclusive.menu_visible_while_moving" $(if ($persistA -and $resumedInGame) { "FAIL" } else { "PASS" }) ("pause menu still drawn after Resume: {0}; product UI state after Resume = InGame (real input routed to gameplay): {1}; pawn moved after Resume: {2} -> a menu stays visible while gameplay accepts movement" -f [bool]$persistA, [bool]$resumedInGame, [bool]$movedAfter) "Frontend"
    Res "exclusive.no_movement_under_menu" "UNKNOWN" ("not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: " + (($mot | Where-Object { $_.frames -gt 0 } | ForEach-Object { "{0} {1:N1} m" -f $_.state, $_.dist }) -join "; ") + ". Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does") "Experimental/Frontend"
    $sp0 = @($spawnL)[0]; if ($sp0) { $routeSpawn = [regex]::Match($sp0.text, 'start=(\S+)').Groups[1].Value }
    Sheet $d "route"
}

# =========================================================================================== direct (same start, direct boot)
if ($Parts -contains "direct" -and -not $routeSpawn) { Res "gameplay.route_vs_direct" "SKIP" "no route spawn start was recorded (route part not run or no MATCH spawn line): the direct-boot comparison could not be made" "Experimental" }
if ($Parts -contains "direct" -and $routeSpawn) {
    $d = Join-Path $OutDir "direct"; New-Item -ItemType Directory -Force $d | Out-Null
    $ps = @((Get-Content -Raw "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$mapDir\gameplay.json" | ConvertFrom-Json).player_starts | Where-Object { @($_.location_gltf).Count -ge 3 })
    $idx = [Array]::IndexOf(@($ps | ForEach-Object { $_.actor }), $routeSpawn)
    $e = @{ WFC_BOOT = "match"; WFC_MAP = $mapDir; WFC_START = "$idx"; WFC_LOCKSTEP = "1"; WFC_SMOKE_FRAMES = "200"; WFC_SHOTEVERY = "$d,120,121"; WFC_LOGEVERY = "0" }; if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    if (-not $ReportOnly) { WaitGpu; $null = Invoke-WfcExe $exe $d $e "run.log" 300 }
    $f = @(Get-ChildItem $d -Filter "f0120.bmp")[0]; $wd = if ($f) { Present-World $f.FullName } else { $null }; $wr = $routeWorld["a07_spawn"]
    if ($wd -and $wr) { $ratio = [Math]::Round($wr.detail / [Math]::Max(0.01, $wd.detail), 2); Row "direct" "spawn_direct" $f.Name (Present-WorldVerdict $wd) $wd
        Res "gameplay.route_vs_direct" $(if ($ratio -lt 0.5) { "FAIL" } else { "PASS" }) ("same start ({0}) - direct boot world detail {1}, frontend-launched {2} (ratio {3}; FAIL < 0.5: the frontend-launched match loses world content that direct boot draws -> route-specific seam, not the map data)" -f $routeSpawn, $wd.detail, $wr.detail, $ratio) "Integration" }
    else { Res "gameplay.route_vs_direct" "SKIP" "no direct-boot frame (WFC_START / WFC_SHOTEVERY hooks) or no route spawn frame" "Experimental" }
}

# =========================================================================================== reference (known-good Streets cameras)
if ($Parts -contains "reference") {
    $d = Join-Path $OutDir "reference"; New-Item -ItemType Directory -Force $d | Out-Null
    $camFile = Join-Path $PSScriptRoot "references\streets_spawnviews.txt"
    $cams = @(Get-Content $camFile | ForEach-Object { $p = $_ -split ' '; $v = $p[1] -split ',' | ForEach-Object { [double]$_ }; @{ name = $p[0] -replace 'TnTeamPlayerStart_', 's'; c = @($v[0], $v[1], $v[2]); t = @($v[3], $v[4], $v[5]) } })
    $shots = @(); for ($w = 0; $w -lt 4; $w++) { $shots += @{ name = "warm$w"; c = $cams[0].c; t = $cams[0].t } }; $shots += $cams
    if (-not $ReportOnly) { WaitGpu; $null = Invoke-ShotList $exe $d $shots @{ WFC_BOOT = "match"; WFC_MAP = "MP_IAC_Streets" } $RenderData @($shots | ForEach-Object { $_.name }) }
    $cmp = @(); foreach ($c in $cams) { $a = Join-Path $d "$($c.name).jpg"; $b = Join-Path $Reference "$($c.name).jpg"
        if ((Test-Path $a) -and (Test-Path $b)) { $s = Present-Similar $a $b; $wa = Present-World $a; $wb = Present-World $b
            $cmp += [pscustomobject]@{ view = $c.name; grad = $s.grad; luma = $s.luma; iou = $s.iou; detail = $wa.detail; ref_detail = $wb.detail; ratio = [Math]::Round($wa.detail / [Math]::Max(0.01, $wb.detail), 2) } } }
    Write-WfcCsv $cmp (Join-Path $OutDir "reference_compare.csv")
    if ($cmp.Count) { $gm = ($cmp | Sort-Object grad)[[int]($cmp.Count / 2)].grad; $lost = @($cmp | Where-Object { $_.ratio -lt 0.5 -or $_.grad -lt 0.4 })
        Res "reference.streets_pipeline" $(if ($gm -lt 0.6 -or $lost.Count -gt [Math]::Floor($cmp.Count * 0.25)) { "FAIL" } else { "PASS" }) ("{0} fixed Streets cameras vs the known-good reference ({1}): median structural similarity {2}; views lost (detail ratio < 0.5 or similarity < 0.4): {3} {4}. FAIL flags the whole merged visual pipeline regardless of how many maps load" -f $cmp.Count, (Split-Path $Reference -Leaf), $gm, $lost.Count, (($lost | ForEach-Object { $_.view }) -join ",")) "Rendering/Integration" }
    else { $mine = @(Get-ChildItem $d -Filter "s*.jpg" -ErrorAction SilentlyContinue).Count; $refs = @(Get-ChildItem $Reference -Filter "s*.jpg" -ErrorAction SilentlyContinue).Count
           Res "reference.streets_pipeline" "SKIP" ("no comparison: {0} frames rendered by the build under test, {1} reference frames at {2}" -f $mine, $refs, $Reference) "Experimental" }
}

# =========================================================================================== watchdog (soft locks, text input, rebinding)
if ($Parts -contains "watchdog") {
    # PC main menu order (NOMOUSE, initial focus Campaign): Campaign, Multiplayer, Escalation, Settings, Extras, Accounts, Exit
    function Down($n) { return (@(1..$n) | ForEach-Object { "ui:Down;wait:t=0.5" }) -join ";" }
    $screens = [ordered]@{
        extras_movies   = @{ path = (Down 4) + ";ui:Accept;wait:t=2;" + (Down 1) + ";ui:Accept;wait:t=3;ui:Accept;wait:t=5"; exits = 3; owner = "Frontend"; expect = "movie" }
        extras_credits  = @{ path = (Down 4) + ";ui:Accept;wait:t=2;" + (Down 3) + ";shot:{D}\b0_list.bmp;ui:Accept;wait:t=4"; exits = 2; owner = "Frontend"; expect = "credits" }
        extras_concept  = @{ path = (Down 4) + ";ui:Accept;wait:t=2;ui:Accept;wait:t=3"; exits = 2; owner = "Frontend"; expect = "screen" }
        accounts_create = @{ path = (Down 5) + ";ui:Accept;wait:t=2;ui:Accept;wait:t=2.5"; exits = 3; owner = "Frontend"; expect = "text"; typed = "WFCQA" }
        settings_controls = @{ path = (Down 3) + ";ui:Accept;wait:t=2;" + (Down 2) + ";ui:Accept;wait:t=2.5;clickclip:_root.settingsMenuLoader_mc.settingsMenu_mc.subMenu_mc.listItem1_mc;wait:t=2.5"; exits = 3; owner = "Frontend"; expect = "rebind" }
        customization   = @{ path = "call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;" + (Down 2) + ";ui:Accept;wait:t=3;ui:Accept;wait:t=4"; exits = 3; owner = "Frontend/AssetTools"; expect = "preview" }
        # back_forward: focus resets to Campaign after every return (Campaign itself is offline / out of scope)
        back_forward    = @{ path = ((1..3 | ForEach-Object { (Down 1) + ";ui:Accept;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1.5;ui:Back;" + $(if ($quitBox) { "wait:t=1.5;ui:Accept;" } else { "" }) + "wait:level=FrontEnd;wait:ui=FrontEnd;wait:t=2" }) -join ";") + ";" + (Down 3) + ";ui:Accept;wait:t=2;ui:Back;wait:t=2;" + (Down 1) + ";ui:Accept;wait:t=2;ui:Back;wait:t=2"; exits = 0; owner = "Frontend"; expect = "cycle" }
    }
    foreach ($k in $screens.Keys) {
        $sc = $screens[$k]; $d = Join-Path $OutDir "watchdog_$k"; New-Item -ItemType Directory -Force $d | Out-Null
        $typing = ""; if ($sc.typed) { $typing = $(if ($typeStep) { "type:$($sc.typed);wait:t=0.5" } else { (($sc.typed.ToCharArray() | ForEach-Object { "key:$([int][char]$_);wait:t=0.25" }) -join ";") }) + ";wait:t=1;shot:$d\c_typed.bmp;dump:FrontEnd" }
        $rebind = ""; if ($sc.expect -eq "rebind") { $rebind = "dump:FrontEnd;ui:Accept;wait:t=1;key:75;wait:t=1.5;shot:$d\c_rebound.bmp;dump:FrontEnd" }
        $exitSteps = (@(1..[Math]::Max(1, $sc.exits)) | ForEach-Object { "ui:Back;wait:t=2;shot:$d\d_exit$_.bmp;snapshot:exit$_;dump:FrontEnd" }) -join ";"
        # customization is two levels deep (character detail -> Custom Character list -> party lobby); with the pass-4
        # routing the party lobby Back asks Quit? and Yes returns to the title
        if ($quitBox -and $k -eq "customization") { $exitSteps = "ui:Back;wait:t=2;shot:$d\d_exit1.bmp;snapshot:exit1;dump:FrontEnd;ui:Back;wait:t=2;shot:$d\d_exit2.bmp;snapshot:exit2;dump:FrontEnd;ui:Back;wait:t=1.5;ui:Accept;wait:level=FrontEnd;wait:ui=FrontEnd;wait:t=2;shot:$d\d_exit3.bmp;snapshot:exit3;dump:FrontEnd" }
        # alternative exits a player would try when Back does nothing: the dialog's Cancel (Right + Accept), Start, Esc
        $altSteps = "ui:Right;wait:t=0.4;ui:Accept;wait:t=2;shot:$d\e_alt1_cancel.bmp;dump:FrontEnd;ui:Start;wait:t=2;shot:$d\e_alt2_start.bmp;dump:FrontEnd;key:27;wait:t=2;shot:$d\e_alt3_esc.bmp;dump:FrontEnd;snapshot:alt"
        if ($sc.exits -eq 0) { $exitSteps = "shot:$d\d_exit1.bmp;snapshot:exit1" }
        $s = @("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=3", "shot:$d\a_main.bmp", "dump:FrontEnd", $sc.path.Replace("{D}", $d), "shot:$d\b_inside.bmp", "snapshot:inside", "dump:FrontEnd", "dump:Lobbies", "dump:Customize", $typing, $rebind, $exitSteps, $(if ($sc.exits -gt 0) { $altSteps }), "wait:t=1", "quit") | Where-Object { $_ }
        if (-not $ReportOnly) { WaitGpu; $r = Invoke-WfcSampled $exe $d (BaseEnv $d @{ WFC_FRONTEND_SCRIPT = ($s -join ";"); WFC_FLOW_TIMEOUT = "150" }) 240 1.0 }
        $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $log = Join-Path $d "wfc.log"; $ls = LastStep $d; $finished = @(Flow-Ev $F "snapshot" | Where-Object { $_.why -like "exit*" }).Count -gt 0
        $dumps = @(Read-GfxDumps $log); $mainDump = @($dumps | Where-Object { $_.movie -like "*FrontEnd_GFX*" } | Select-Object -First 1)[0]
        $insideDumps = @($dumps | Select-Object -Skip 1)
        $inside = "$d\b_inside.bmp"; $main = "$d\a_main.bmp"; $last = @(Get-ChildItem $d -Filter "d_exit*.bmp" | Sort-Object Name | Select-Object -Last 1)[0]
        $mi = Present-Measure $inside $script:PresentRegions.full
        $simInsideMain = Present-Similar $inside $main; $present = $mi -and $simInsideMain -and $simInsideMain.grad -lt 0.9
        $display = $present -and (Present-UiVerdict $mi) -ne "FAIL"
        $exitOk = $false; $simExitMain = $null; $simExitInside = $null
        if ($last) { $simExitMain = Present-Similar $last.FullName $main $script:PresentRegions.title_scene; $simExitInside = Present-Similar $last.FullName $inside; $exitOk = $simExitMain.grad -gt 0.5 -and $simExitMain.grad -gt $simExitInside.grad - 0.05 }
        # screen identity from the GFx dumps: the main menu is open exactly when menuMain_mc is visible (HIDDEN while a submenu is up)
        $feDumps = @($dumps | Where-Object { $_.movie -like "*FrontEnd_GFX*" }); $mainVis = @($feDumps | ForEach-Object { [bool](@($_.clips | Where-Object { $_ -match "menuMain_mc$" }).Count) })
        $nIn = 2   # dump 0 = main menu before entering, dump 1 = inside; then (typing / rebind dumps), then one per exit attempt
        $exitDumpVis = @($mainVis | Select-Object -Skip ([Math]::Max(1, $feDumps.Count - $sc.exits - 3)))
        if ($feDumps.Count -ge 2 -and $mainVis[0] -and -not $mainVis[1]) {
            $postExit = @($mainVis | Select-Object -Skip ($feDumps.Count - $sc.exits - 3) | Select-Object -First $sc.exits)
            $postAlt = @($mainVis | Select-Object -Last 3)
            $exitOk = $postExit -contains $true; $altDump = $postAlt -contains $true
        } else { $altDump = $false }
        if ($sc.exits -eq 0) { $exitOk = $finished }
        $altOk = $false; $altName = ""
        if (-not $exitOk -and $altDump) { $altOk = $true; $altName = "Cancel / Start / Esc (dump)" }
        if (-not $exitOk -and -not $altOk -and $sc.exits -gt 0 -and $feDumps.Count -lt 2) { foreach ($a in "e_alt1_cancel", "e_alt2_start", "e_alt3_esc") { $af = "$d\$a.bmp"; if (Test-Path $af) { $sa = Present-Similar $af $main $script:PresentRegions.title_scene; $si = Present-Similar $af $inside; if ($sa.grad -gt 0.5 -and $sa.grad -gt $si.grad - 0.05) { $altOk = $true; $altName = $a -replace "e_alt\d_", ""; break } } } }
        # focus: a visible button with focus must exist in the screen's dumps
        $focusOk = @($insideDumps | Where-Object { $_.focused.Count -gt 0 }).Count -gt 0
        $inter = $null; $interNote = ""
        switch ($sc.expect) {
            "text" { $tx = @($dumps | ForEach-Object { $_.texts } | Where-Object { $_ -match [regex]::Escape($sc.typed) }); $px = Present-Similar "$d\c_typed.bmp" $inside
                     $inter = $tx.Count -gt 0; $interNote = "typed '$($sc.typed)' with ordinary key events: text in a dump $($tx.Count -gt 0); frame changed after typing $([bool]($px -and $px.grad -lt 0.97))" }
            "rebind" { # the shipped Mouse/Keyboard Layout page is a read-only reference card (Frontend pass 4: no rebinding calls in any movie,
                     # CONFIRMED ORIGINAL per Frontend pass 4: SettingsMenu_GFX only calls Console.GetKeyDescription -> TnPlayerInput.GetKeyDescription,\n                     # decompiled): it is functional when it LISTS the bindings for the selected form (Robot 24 rows, Car 13); a K rebind is INFO only
                     $layoutTexts = @(@($dumps | Select-Object -Skip 1 | Select-Object -First 1) | ForEach-Object { $_.texts } | Where-Object { $_ -and $_ -notmatch "^(Truck|Tank|Robot|Car|Jet|Back|Mouse/Keyboard Layout|MOUSE/KEYBOARD LAYOUT|Controls|CONTROLS)$" })
                     $inter = $layoutTexts.Count -ge 6; $interNote = "Mouse / Keyboard Layout card (read-only by design, CONFIRMED ORIGINAL per Frontend decompile): $($layoutTexts.Count) binding description texts visible (need >= 6; e.g. $(($layoutTexts | Select-Object -First 4) -join ' / '))" }
            "movie"  { $mp = @(Flow-Ev $F "movie.play") + @(Flow-Ev $F "movie.open" | Where-Object { $_.movie -notlike "TF_*" }); $inter = $mp.Count -gt 0; $interNote = "a movie started: $($mp.Count -gt 0)" }
            "credits" { $c1 = Present-Similar $inside "$d\b0_list.bmp"; $inter = $c1 -and $c1.grad -lt 0.8; $interNote = "after Accept on Credits the screen differs from the Extras list: $inter (similarity $(if ($c1) { $c1.grad }))" }
            "preview" { # preview bodies: Rendering loadPreviewBody (Cust_Idle) via FrontendSceneGL logs "preview body <gltf>"; older builds "skinned glb: .../Characters/"
                     $bodyL = @(Grep-Log $log 'preview body content/|skinned glb: .*?/Characters/'); $loadingTxt = @($dumps | ForEach-Object { $_.texts } | Where-Object { $_ -match '(?i)^loading' }).Count
                     $pv = @(Flow-Ev $F "customize.preview"); $pvOwner = @($pv | ForEach-Object { "$($_.owner)" } | Select-Object -Unique)
                     $ownerOk = (-not $pv.Count) -or ($pvOwner -contains "renderer")
                     $inter = $bodyL.Count -gt 0 -and $ownerOk -and -not $loadingTxt; $interNote = "preview body loaded: $($bodyL.Count -gt 0) ($((@($bodyL | ForEach-Object { [regex]::Match($_.text, '(?:content/|Characters/)([^/]+)').Groups[1].Value } | Select-Object -Unique)) -join ', ')); customize.preview owner: $(if ($pv.Count) { $pvOwner -join ',' } else { 'no trace' }); 'LOADING' still shown: $([bool]$loadingTxt); the posed body on screen: HUMAN (sheet)" }
            default  { $inter = $present; $interNote = "" }
        }
        $state = if (-not $present) { "SCREEN NOT PRESENT" } elseif (-not $display) { "SCREEN PRESENT" } elseif (-not $inter) { "DISPLAY CORRECT (automated part)" } elseif (-not $exitOk) { "INTERACTION WORKING" } else { "FULLY FUNCTIONAL" }
        $soft = -not $finished -or ($sc.exits -gt 0 -and -not $exitOk -and -not $altOk)
        Res "watchdog.$k.softlock" $(if ($soft) { "FAIL" } elseif (-not $exitOk -and $sc.exits -gt 0) { "PARTIAL" } else { "PASS" }) ("{0}: script {1}; alternative exit (Cancel / Start / Esc) reaches the main menu: {7}; after {2} Back press(es) the frame matches the main menu {3} (scene similarity {4}, still-inside similarity {5}); focus present in the screen's dumps {6}" -f $k, $(if ($finished) { "finished" } else { "STUCK at '$($ls.cond)$($ls.action)'" }), $sc.exits, $exitOk, $(if ($simExitMain) { $simExitMain.grad }), $(if ($simExitInside) { $simExitInside.grad }), $focusOk, $(if ($altOk) { $altName } elseif ($exitOk) { "not needed" } else { "none" })) $sc.owner
        $clickMiss = @(Flow-Ev $F "script.clickclip" | Where-Object { "$($_.found)" -eq "False" })
        if ($clickMiss.Count) { $state = "NOT REACHED BY THE TEST"; $interNote = "the test could not open the target ($($clickMiss[0].path) not found) - test fault, not a product verdict" }
        Res "watchdog.$k.classification" $(if ($clickMiss.Count) { "UNKNOWN" } elseif ($state -eq "FULLY FUNCTIONAL") { "PASS" } elseif ($state -like "SCREEN NOT PRESENT") { "FAIL" } elseif ($sc.expect -in "text", "rebind", "preview", "movie", "credits" -and -not $inter) { "FAIL" } else { "PARTIAL" }) ("{0}: {1}. {2}" -f $k, $state, $interNote) $sc.owner
        # lobby emblem glow / fade STATE (Frontend a72befa+: FLOW scene.emblem actor= param=Highlighted|Opacity state=on|off).
        # Autobot icon / glow 8803 / 16331, Decepticon 5865 / 1120. Opening Create a Character -> Opacity on; the focused
        # faction's chassis button -> Highlighted on; hidePlayer on the way out -> every Opacity off. Trace = state only: the
        # visible glow needs Rendering's material parameters (setFrontendMaterialParam), so the image stays a HUMAN check.
        if ($k -eq "customization") {
            $emSrc = [bool](Get-ChildItem (Join-Path $Root "src\frontend") -Recurse -Include *.cpp -ErrorAction SilentlyContinue | Select-String -Pattern '"scene.emblem"' -SimpleMatch -List | Select-Object -First 1)
            $em = @(Flow-Ev $F "scene.emblem" | Sort-Object { [int]$_.seq })
            if (-not $emSrc) { Res "watchdog.customization.emblem_state" "SKIP" "build has no scene.emblem trace (Frontend a72befa+): emblem glow / fade not checkable; HUMAN-CHECK-M07 item 2" "Experimental" }
            else {
                $opOn = @($em | Where-Object { $_.param -eq "Opacity" -and $_.state -eq "on" }); $hiOn = @($em | Where-Object { $_.param -eq "Highlighted" -and $_.state -eq "on" })
                $last = @{}; foreach ($e in $em) { if ($e.param -eq "Opacity") { $last["$($e.actor)"] = "$($e.state)" } }
                $stillOn = @($last.Keys | Where-Object { $last[$_] -eq "on" })
                $st = if (-not $em.Count) { "FAIL" } elseif (-not $opOn.Count -or -not $hiOn.Count) { "FAIL" } elseif ($stillOn.Count) { "PARTIAL" } else { "PASS" }
                $rmp = [bool](Get-ChildItem (Join-Path $Root "src") -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue | Select-String -Pattern "setFrontendMaterialParam" -SimpleMatch -List | Select-Object -First 1)
                Res "watchdog.customization.emblem_state" $st ("STATE ONLY (trace): {0} emblem transitions; Opacity on {1} ({2}); Highlighted on {3} ({4}); Opacity still on after leaving Create a Character: {5}. Visible glow: {6}" -f $em.Count, $opOn.Count, ((@($opOn | ForEach-Object { $_.actor -replace 'MaterialInstanceActor_', '' } | Select-Object -Unique)) -join ","), $hiOn.Count, ((@($hiOn | ForEach-Object { $_.actor -replace 'MaterialInstanceActor_', '' } | Select-Object -Unique)) -join ","), $(if ($stillOn.Count) { $stillOn -join "," } else { "none" }), $(if ($rmp) { "renderer has setFrontendMaterialParam - HUMAN check (item 2)" } else { "NOT YET DRAWN (no setFrontendMaterialParam in this build - Rendering); HUMAN check (item 2)" })) "Frontend"
            }
        }
        if ($mi) { Row "watchdog" $k "b_inside.bmp" $(if ($display) { "PASS" } else { "FAIL" }) $mi }
        Sheet $d "watchdog_$k"
    }
}

Write-WfcCsv $rows (Join-Path $OutDir "frames.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$fails = @($res.ToArray() | Where-Object status -eq "FAIL")
if ($fails.Count -ne [int]$sum.fail) { throw "verdict bookkeeping mismatch: report says $($sum.fail) FAIL, verdict sees $($fails.Count) - refusing to print a verdict" }
$verdict = if (@($fails | Where-Object { $_.id -match 'gameplay\.world|route_vs_direct|reference\.streets|frontend\.scene' }).Count) { "VISUALLY BROKEN" } elseif ($fails.Count) { "VISUAL DEFECTS" } else { "NO CATASTROPHIC PRESENTATION FAILURE FOUND (human look still required)" }
$rdText = if ($RenderData) { $RenderData } else { "product default (<exe>\..\..\work\render)" }
$sumText = ($sum.Keys | ForEach-Object { "$_ " + $sum[$_] }) -join " / "
$md = @("# Presentation gate - $Label $Config $sha", "", "Verdict: **$verdict**", "", "| | |", "|---|---|", ("| exe | " + $exe + " |"), ("| render data | " + $rdText + " |"), ("| result | " + $sumText + " |"), "")
$md += "## FAIL"; $md += ""; foreach ($f in $fails) { $md += "- **$($f.id)** [$($f.owner)]: $($f.note)" }; $md += ""
$md += "## Other"; $md += ""; foreach ($f in @($res.ToArray() | Where-Object status -ne "FAIL")) { $md += "- $($f.status) **$($f.id)**: $($f.note)" }
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "PRESENTATION.md")
"PRESENTATION GATE ($Config): $verdict - " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ") + " -> $OutDir"
