# GROSS MAP-LOSS GATE: the environment must not disappear while the HUD / player / effects remain.
#
#   .\tools\fidelity\map-loss-gate.ps1 -Exe <the exe AS THE USER RUNS IT> -OutDir <dir> [-Configs normal,w1280,w1920,lowtex]
#                                      [-RefExe <known-good exe> -RefRenderData <its data>] [-AltRefExe <exe for maps the
#                                      known-good build cannot load>]
#
# Built after the M06 / M06b human playtest (Streets and Berth mostly missing behind the HUD), which every earlier gate
# passed. Rules of this gate:
#   * The exe is run from ITS OWN location with NO WFC_RENDER_DATA: the product resolves its data exactly as for the
#     user (earlier gates exported the tree to work\ab\<n>\build-release\bin and/or passed WFC_RENDER_DATA).
#   * The map is reached through the frontend (lobby map selection), never by direct boot, and the chain is
#     Streets -> lobby -> Berth -> lobby -> Streets in ONE process.
#   * Display state is part of the test: fresh profile, saved 1280x720, saved 1920x1080, saved low texture quality.
#   * Expectations do not come from the pipeline under test: world coverage in pixels with the HUD band and the player
#     excluded, and a reference view of the SAME start rendered by a KNOWN-GOOD runtime (default: M05 1e14900, the
#     last human-confirmed Streets). Product draw counts (WFC_VISUALCHECK) are recorded, compared with a direct boot of
#     the same exe at the same start, and never accepted on their own.
#   * One graphical WFC process at a time. Each run records exit / hang / abnormal end and Windows display-driver
#     reset events (System log, Display 4101 / dxgkrnl) in its time window.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [string[]]$Configs = @(),
      [string]$RefExe = "", [string]$RefRenderData = "", [string]$AltRefExe = "", [string]$AltRefRenderData = "", [switch]$ReportOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Shots.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Configs = @($Configs | ForEach-Object { $_ -split "," } | Where-Object { $_ }); if (-not $Configs.Count) { $Configs = @("normal", "w1280", "w1920", "lowtex") }
$root = Get-WfcRoot
if (-not $RefExe) { $RefExe = Join-Path $root "work\ab\m5int\build-release\bin\wfc_rebuild.exe"; $RefRenderData = Join-Path $root "work\ab\m5int\work\render" }
if (-not $AltRefExe) { $AltRefExe = Join-Path $root "work\ab\m6int\build-release\bin\wfc_rebuild.exe"; $AltRefRenderData = Join-Path $root "work\ab\m6int\work\render" }
$Exe = (Resolve-Path $Exe).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$H = Get-ExeHooks $Exe
$res = New-WfcResults
function Res($id, $status, $note, $owner = "", $m = $null) { Add-WfcResult $res "maploss.$id" $status $m $note $owner }
function NoGui { # wait (up to 30 min) until no wfc_rebuild.exe from ANY session is running: one graphical instance at a time
    $deadline = (Get-Date).AddMinutes([int]$(if ($env:WFC_GATE_GPU_WAIT_MIN) { $env:WFC_GATE_GPU_WAIT_MIN } else { 240 })); while ((Get-Date) -lt $deadline) { if (-not @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue).Count) { Start-Sleep 3; if (-not @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue).Count) { return 0 } }; Start-Sleep 10 }
    return @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue).Count }
function DriverEvents([datetime]$from, [datetime]$to) {
    try { return @(Get-WinEvent -FilterHashtable @{ LogName = "System"; StartTime = $from; EndTime = $to } -ErrorAction Stop | Where-Object { $_.Id -eq 4101 -or $_.ProviderName -match 'dxgkrnl|Display|amdkmdag|nvlddmkm|igfx|LiveKernelEvent' -or ($_.ProviderName -eq "Application Error" -and $_.Message -match 'wfc_rebuild') } | ForEach-Object { "{0:HH:mm:ss} {1} {2}" -f $_.TimeCreated, $_.ProviderName, $_.Id }) }
    catch { return @() }
}
$profiles = @{
    normal = $null
    w1280  = "[PCSettings]`r`nWidth=1280`r`nHeight=720`r`nFullscreen=0`r`nTextureQuality=2`r`nVSync=0`r`n"
    w1920  = "[PCSettings]`r`nWidth=1920`r`nHeight=1080`r`nFullscreen=0`r`nTextureQuality=2`r`nVSync=0`r`n"
    lowtex = "[PCSettings]`r`nWidth=1280`r`nHeight=720`r`nFullscreen=0`r`nTextureQuality=0`r`nVSync=1`r`n"
}
$maps = @(@{ key = "s1"; id = 508; dir = "MP_IAC_Streets" }, @{ key = "b"; id = 502; dir = "MP_IAC_Berth" }, @{ key = "s2"; id = 508; dir = "MP_IAC_Streets" })
$rows = New-Object System.Collections.Generic.List[object]; $starts = @{}

foreach ($cfg in $Configs) {
    $d = Join-Path $OutDir $cfg; New-Item -ItemType Directory -Force $d | Out-Null
    # ---- script: intro (frames at the side edges), frontend, Streets -> lobby -> Berth -> lobby -> Streets
    $seg = foreach ($m in $maps) {
        $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
        "call:Online.SetSelectedMapID,$($m.id);wait:t=1.5;shot:$d\$($m.key)_0lobby.bmp;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:t=1.5;shot:$d\$($m.key)_1spawn.bmp;wait:t=1.5;shot:$d\$($m.key)_2move.bmp;wait:t=1.5;shot:$d\$($m.key)_3move.bmp;snapshot:$($m.key)_ingame;wait:ui=GameEnded;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;shot:$d\$($m.key)_4lobby_after.bmp"
    }
    $s = @("wait:t=2", "shot:$d\i1_intro_2s.bmp", "wait:t=4", "shot:$d\i2_intro_6s.bmp", "ui:Accept", "wait:t=3", "shot:$d\i3_intro_next.bmp", "ui:Accept", "wait:t=2", "ui:Accept", "wait:t=2", "ui:Accept",
           "wait:frontend", (Get-MousePark $root), "wait:ui=FrontEnd", "wait:t=3", "shot:$d\f0_title.bmp", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=2",
           "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2") + @($seg) + @("quit")
    $e = @{ WFC_FRONTEND_SCRIPT = ($s -join ";"); WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "600"; WFC_NOMOUSE = "1"; WFC_VISUALCHECK = "1"; WFC_LIFECYCLE = "2"
            WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "30" }
    if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
    $run = $null
    if (-not $ReportOnly) {
        if (NoGui) { throw "another wfc_rebuild.exe is running: this gate runs ONE graphical instance at a time" }
        Get-ChildItem $d -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in ".bmp", ".log", ".jsonl", ".ini", ".err", ".csv" } | ForEach-Object { [IO.File]::Delete($_.FullName) }
        if ($profiles[$cfg]) { [IO.File]::WriteAllText((Join-Path $d "wfc_profile.ini"), $profiles[$cfg]) }
        $t0 = Get-Date
        $run = Invoke-WfcSampled $Exe $d $e 900 1.0
        $t1 = Get-Date
        $ev = DriverEvents $t0.AddSeconds(-5) $t1.AddSeconds(10)
        [pscustomobject]@{ rc = $run.rc; timedOut = $run.timedOut; start = $t0.ToString("s"); end = $t1.ToString("s"); driverEvents = ($ev -join " | ") } | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $d "process.json")
    }
    $pj = Get-Content -Raw (Join-Path $d "process.json") -ErrorAction SilentlyContinue | ConvertFrom-Json
    $log = Join-Path $d "wfc.log"; $F = Read-FlowLog (Join-Path $d "flow.jsonl")
    $clean = (Test-Path $log) -and @(Grep-Log $log 'Shutdown complete').Count -gt 0
    $outcome = if ($pj.driverEvents) { "DRIVER EVENT" } elseif ("$($pj.timedOut)" -eq "True") { "HANG / TIMEOUT" } elseif (-not $clean) { "ABNORMAL EXIT (rc $($pj.rc))" } else { "clean exit" }
    Res "$cfg.process" $(if ($outcome -eq "clean exit") { "PASS" } else { "FAIL" }) ("process outcome: {0}; display-driver / crash events in the run window: {1}" -f $outcome, $(if ($pj.driverEvents) { $pj.driverEvents } else { "none" })) "Rendering/Integration"
    $root0 = @(Grep-Log $log 'render data root|NO RENDER DATA')[0]
    Res "$cfg.render_data_root" $(if ($root0 -and $root0.text -match 'NO RENDER DATA') { "FAIL" } else { "INFO" }) ("render data resolved by the product: " + $(if ($root0) { $root0.text -replace '^.*\] ', '' } else { "no root log line (build predates the M06b root search; it uses <exe>\..\..\work\render)" })) "Rendering/Integration"
    # ---- intro: the frontend must not show beside the movie (side columns)
    foreach ($i in "i1_intro_2s", "i2_intro_6s") { $f = "$d\$i.bmp"; if (-not (Test-Path $f)) { continue }
        $l = Present-Measure $f @(0.0, 0.1, 0.08, 0.9); $r = Present-Measure $f @(0.92, 0.1, 1.0, 0.9); $c = Present-Measure $f @(0.3, 0.3, 0.7, 0.7)
        $side = [Math]::Max($l.detail, $r.detail)
        Res "$cfg.intro_edges.$i" $(if ($side -gt 0.05 -and $l.black -lt 0.9) { "FAIL" } else { "PASS" }) ("intro frame side columns (8 % each): detail {0} / {1}, black {2} / {3}; centre detail {4} (FAIL: content beside the movie - the frontend shows through)" -f $l.detail, $r.detail, $l.black, $r.black, $c.detail) "Frontend/Rendering" }
    # ---- VISUALCHECK draw counts per state (product counters, recorded - never sufficient)
    $vc = New-Object System.Collections.Generic.List[object]; $state = "boot"; $visitN = 0; $ui = ""
    foreach ($ln in [IO.File]::ReadLines($log)) {
        if ($ln -match 'FLOW level\.begin level=(\S+) map=(\S+)') { $visitN++; $state = "{0:D2}:{1}:{2}" -f $visitN, $Matches[1], $Matches[2] }
        if ($ln -match 'FLOW ui\.state from=\S+ to=(\S+)') { $ui = $Matches[1] }
        if ($state -like "*:Match:*" -and $ui -ne "InGame") { continue }   # in-match counts only while the player is in play
        if ($ln -match 'VISUALCHECK frame (\d+) .*draws=(\d+) world=(\d+) bsp=(\d+) materials=(\d+)') { $vc.Add([pscustomobject]@{ state = $state; frame = [int]$Matches[1]; draws = [int]$Matches[2]; world = [int]$Matches[3]; bsp = [int]$Matches[4]; materials = [int]$Matches[5] }) }
    }
    $vcs = @($vc | Group-Object state | ForEach-Object { $g = $_.Group; [pscustomobject]@{ state = $_.Name; samples = $g.Count; world_spawn = (@($g | Select-Object -First 3 | ForEach-Object { $_.world }) | Sort-Object)[1]; world_med = ($g | ForEach-Object { $_.world } | Sort-Object)[[int]($g.Count / 2)]; bsp_med = ($g | ForEach-Object { $_.bsp } | Sort-Object)[[int]($g.Count / 2)]; materials_med = ($g | ForEach-Object { $_.materials } | Sort-Object)[[int]($g.Count / 2)] } })
    Write-WfcCsv $vcs (Join-Path $d "visualcheck_by_state.csv")
    # ---- per map visit: world coverage (pixels), reference from a KNOWN-GOOD runtime at the same start, draw counts
    $spawnLines = @(Grep-Log $log '\] MATCH spawn ')
    $mi = 0
    foreach ($m in $maps) {
        $ws = @("1spawn", "2move", "3move" | ForEach-Object { Present-World "$d\$($m.key)_$_.bmp" } | Where-Object { $_ })
        $set = Present-WorldSetVerdict $ws
        $start = if ($mi -lt $spawnLines.Count) { [regex]::Match($spawnLines[$mi].text, 'start=(\S+)').Groups[1].Value } else { "" }; $mi++
        $matchStates = @($vcs | Where-Object { $_.state -like "*:Match:*" } | Sort-Object state); $vcm = if ($mi - 1 -lt $matchStates.Count) { $matchStates[$mi - 1] } else { $null }
        $row = [pscustomobject][ordered]@{ config = $cfg; visit = $m.key; map = $m.dir; start = $start; frames = $ws.Count; world_detail = (($ws | ForEach-Object { $_.detail }) -join "/"); black = (($ws | ForEach-Object { $_.black }) -join "/"); verdict = $set
            vc_world = $(if ($vcm) { $vcm.world_med }); vc_world_spawn = $(if ($vcm) { $vcm.world_spawn }); vc_bsp = $(if ($vcm) { $vcm.bsp_med }); vc_materials = $(if ($vcm) { $vcm.materials_med }); ref_detail = $null; ref_exe = $null; ratio = $null }
        if ($start) { $starts["$($m.dir)|$start"] = $true; $refJpg = Join-Path $OutDir ("ref\{0}_{1}.jpg" -f $m.dir, $start); if (Test-Path $refJpg) { $rw = Present-World $refJpg; $row.ref_detail = $rw.detail } }
        $rows.Add($row)
    }
}

# ---- references: a camera behind each start actually used, rendered by the KNOWN-GOOD runtime (Streets: M05 1e14900;
# maps M05 cannot load: the M06 runtime by DIRECT boot, which is unaffected by the frontend path)
$refDir = Join-Path $OutDir "ref"; New-Item -ItemType Directory -Force $refDir | Out-Null
foreach ($k in @($starts.Keys)) {
    $mapDir, $actor = $k -split '\|'; $jpg = Join-Path $refDir "${mapDir}_$actor.jpg"; if ((Test-Path $jpg) -or $ReportOnly) { continue }
    $gp = Get-Content -Raw "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$mapDir\gameplay.json" | ConvertFrom-Json
    $p = @($gp.player_starts | Where-Object { $_.actor -eq $actor })[0]; if (-not $p) { continue }
    $x = [double]$p.location_gltf[0]; $y = [double]$p.location_gltf[1]; $z = [double]$p.location_gltf[2]; $yw = [double]$p.yaw_deg * [Math]::PI / 180; $fx = [Math]::Cos($yw); $fz = [Math]::Sin($yw)
    $cam = @{ name = "${mapDir}_$actor"; c = @(($x - 5.5 * $fx), ($y + 2.6), ($z - 5.5 * $fz)); t = @(($x + 12 * $fx), ($y + 1.0), ($z + 12 * $fz)) }
    $shots = @(); for ($w = 0; $w -lt 4; $w++) { $shots += @{ name = "warm$w"; c = $cam.c; t = $cam.t } }; $shots += $cam
    $useAlt = $mapDir -ne "MP_IAC_Streets"; $rx = if ($useAlt) { $AltRefExe } else { $RefExe }; $rr = if ($useAlt) { $AltRefRenderData } else { $RefRenderData }
    if (NoGui) { throw "another wfc_rebuild.exe is running" }
    $tmp = Join-Path $refDir "tmp_$mapDir"; $null = Invoke-ShotList $rx $tmp $shots @{ WFC_BOOT = "match"; WFC_MAP = $mapDir } $rr @($shots | ForEach-Object { $_.name })
    $src = Join-Path $tmp "${mapDir}_$actor.jpg"; if (Test-Path $src) { Copy-Item $src $jpg; "{0} <- {1}" -f $jpg, $rx | Add-Content (Join-Path $refDir "provenance.txt") }
}
foreach ($r in $rows) { $jpg = Join-Path $refDir ("{0}_{1}.jpg" -f $r.map, $r.start); if (Test-Path $jpg) { $rw = Present-World $jpg; $r.ref_detail = $rw.detail; $r.ref_exe = $(if ($r.map -eq "MP_IAC_Streets") { "known-good M05" } else { "M06 direct boot" })
    $med = @($r.world_detail -split "/" | Where-Object { $_ } | ForEach-Object { [double]$_ } | Sort-Object); if ($med.Count) { $r.ratio = [Math]::Round($med[[int]($med.Count / 2)] / [Math]::Max(0.01, $rw.detail), 2) } } }
# ---- draw expectation from the SAME exe by direct boot at the SAME start (unaffected by the frontend route): the
# frontend-launched in-play world draws must reach at least half of it (M06b user exe: Streets 35 vs 1641, Berth 152 vs 1087)
$dcDir = Join-Path $OutDir "direct_counts"; New-Item -ItemType Directory -Force $dcDir | Out-Null; $dc = @{}
if ($H.Contains("WFC_VISUALCHECK")) {
    foreach ($k in @($starts.Keys)) { $mapDir, $actor = $k -split '\|'; $dd = Join-Path $dcDir "${mapDir}_$actor"
        if (-not $ReportOnly -and -not (Test-Path (Join-Path $dd "wfc.log"))) {
            $ps = @((Get-Content -Raw "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$mapDir\gameplay.json" | ConvertFrom-Json).player_starts | Where-Object { @($_.location_gltf).Count -ge 3 }); $idx = [Array]::IndexOf(@($ps | ForEach-Object { $_.actor }), $actor)
            if (NoGui) { throw "another wfc_rebuild.exe is running" }
            $null = Invoke-WfcExe $Exe $dd @{ WFC_BOOT = "match"; WFC_MAP = $mapDir; WFC_START = "$idx"; WFC_SMOKE_FRAMES = "600"; WFC_VISUALCHECK = "1"; WFC_LOGEVERY = "0" } "run.log" 300 }
        $lg = Join-Path $dd "wfc.log"; if (Test-Path $lg) { $w = @(Select-String $lg -Pattern 'VISUALCHECK .*world=(\d+) bsp=(\d+) materials=(\d+)' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Sort-Object); if ($w.Count) { $dc[$k] = $w[[int]($w.Count / 2)] } } }
}
# Compare like with like: the direct boot stands at the start, so the route's first in-play samples (at the spawn,
# before the scripted walk turns the camera into nearby geometry) are compared - the walking median only for the note.
# The M06 world loss is flat from the spawn (35 vs 1641), so it still FAILs here.
foreach ($r in $rows) { $k = "$($r.map)|$($r.start)"; $sv = if ($r.vc_world_spawn) { $r.vc_world_spawn } else { $r.vc_world }; if ($dc.ContainsKey($k) -and $sv) { $q = [Math]::Round([double]$sv / [Math]::Max(1, $dc[$k]), 3)
    # INFO only: the M06b user exe (world LOST on pixels) issued the same draws at the spawn (1276 vs 1641) as healthy
    # builds - the loss was GL state (depth test off), not missing draws. Pixels + GL entry state decide; this corroborates.
    Res "$($r.config).$($r.visit).$($r.map).draws_vs_direct" "INFO" ("draw counts do not distinguish a lost world from a healthy one (M06b control: same spawn draws); in-play world draws via the lobby at the spawn {0} (walking median {3}) vs direct boot of the same exe at the same start {1} (ratio {2}; FAIL < 0.5: the environment is not drawn on the frontend route)" -f $sv, $dc[$k], $q, $r.vc_world) "Rendering/Frontend/Integration" } }
Write-WfcCsv $rows (Join-Path $OutDir "map_visits.csv")
foreach ($r in $rows) {
    $refNote = if ($r.ref_detail -ne $null) { "; same start rendered by the {0} runtime: detail {1} (median ratio {2})" -f $r.ref_exe, $r.ref_detail, $r.ratio } else { "" }
    # pixels decide; the reference view (a different camera framing) only corroborates a weak (PARTIAL) result
    $st = if ($r.verdict -eq "FAIL") { "FAIL" } elseif ($r.verdict -eq "SKIP") { "SKIP" } elseif ($r.verdict -eq "PARTIAL") { if ($r.ratio -ne $null -and $r.ratio -lt 0.5) { "FAIL" } else { "PARTIAL" } } else { "PASS" }
    Res "$($r.config).$($r.visit).$($r.map).environment" $st ("{0} via the lobby, start {1}: world detail (HUD band and player excluded) {2}, black {3}; product VISUALCHECK median world draws {4}, BSP draws {5}, materials {6}{7}. FAIL = the environment is missing although the match runs (counts alone never pass this check)" -f $r.map, $r.start, $r.world_detail, $r.black, $r.vc_world, $r.vc_bsp, $r.vc_materials, $refNote) "Rendering/Frontend/Integration"
}
# ---- consistency of counts through the chain (Streets first visit vs return)
foreach ($cfg in $Configs) { $a = @($rows | Where-Object { $_.config -eq $cfg -and $_.visit -eq "s1" })[0]; $b = @($rows | Where-Object { $_.config -eq $cfg -and $_.visit -eq "s2" })[0]
    if ($a -and $b -and $a.vc_world -and $b.vc_world) { $q = [Math]::Round([double]$b.vc_world / [Math]::Max(1, [double]$a.vc_world), 2)
        Res "$cfg.streets_return_counts" $(if ($q -lt 0.8 -or $q -gt 1.25) { "FAIL" } else { "PASS" }) ("Streets world draws first visit {0} vs after Berth {1} (ratio {2}); BSP {3} vs {4}" -f $a.vc_world, $b.vc_world, $q, $a.vc_bsp, $b.vc_bsp) "Rendering" } }
# ---- HUD widget scale: the top-left health widget's bright-pixel box must keep its share of the screen at 720p and 1080p
function HudBox($f) { if (-not (Test-Path $f)) { return $null }; Add-Type -AssemblyName System.Drawing; $b = [System.Drawing.Bitmap]::FromFile($f); try { $w = $b.Width; $h = $b.Height; $x0 = $w; $x1 = 0; $y0 = $h; $y1 = 0
    for ($y = 0; $y -lt [int]($h * 0.2); $y += 2) { for ($x = 0; $x -lt [int]($w * 0.3); $x += 2) { $c = $b.GetPixel($x, $y); if ($c.R -gt 230 -and $c.G -gt 230 -and $c.B -gt 230) { if ($x -lt $x0) { $x0 = $x }; if ($x -gt $x1) { $x1 = $x }; if ($y -lt $y0) { $y0 = $y }; if ($y -gt $y1) { $y1 = $y } } } }
    if ($x1 -le $x0) { return $null }; return [pscustomobject]@{ w = [Math]::Round(($x1 - $x0) / $w, 3); h = [Math]::Round(($y1 - $y0) / $h, 3); px = "$($x1 - $x0)x$($y1 - $y0) of ${w}x$h" } } finally { $b.Dispose() } }
$h720 = HudBox (Join-Path $OutDir "w1280\s1_1spawn.bmp"); $h1080 = HudBox (Join-Path $OutDir "w1920\s1_1spawn.bmp")
if ($h720 -and $h1080) { $q = [Math]::Round($h1080.w / [Math]::Max(0.001, $h720.w), 2)
    Res "hud_scale_720_vs_1080" $(if ($q -lt 0.8 -or $q -gt 1.25) { "FAIL" } else { "PASS" }) ("health widget bright box: 720p {0} ({1} of width), 1080p {2} ({3} of width); ratio {4} (FAIL: the HUD does not scale with the viewport)" -f $h720.px, $h720.w, $h1080.px, $h1080.w, $q) "Frontend/Rendering" }
else { Res "hud_scale_720_vs_1080" "SKIP" "health widget not found in both spawn frames" "Experimental" }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$fails = @($res.ToArray() | Where-Object status -eq "FAIL"); if ($fails.Count -ne [int]$sum.fail) { throw "bookkeeping mismatch" }
$env = @($fails | Where-Object { $_.id -like "*.environment" })
$verdict = if ($env.Count) { "MAP PRESENTATION LOSS DETECTED ($($env.Count) map visits)" } elseif ($fails.Count) { "DEFECTS (no gross map loss)" } else { "NO GROSS MAP LOSS (human look still required)" }
$md = @("# Map-loss gate", "", "exe: ``$Exe`` (run from its own location, no WFC_RENDER_DATA)", "", "Verdict: **$verdict**", "", "| config | visit | map | start | world detail | VISUALCHECK world / bsp / materials | same start, reference runtime | verdict |", "|---|---|---|---|---|---|---|---|")
foreach ($r in $rows) { $md += "| $($r.config) | $($r.visit) | $($r.map) | $($r.start) | $($r.world_detail) | $($r.vc_world) / $($r.vc_bsp) / $($r.vc_materials) | $(if ($r.ref_detail -ne $null) { "$($r.ref_detail) ($($r.ref_exe), ratio $($r.ratio))" }) | $($r.verdict) |" }
$md += ""; $md += "## All checks"; $md += ""; foreach ($f in $res.ToArray()) { $md += "- $($f.status) **$($f.id)**: $($f.note)" }
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "MAP-LOSS.md")
"MAP-LOSS GATE: $verdict - " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
