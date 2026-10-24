# MIN-FPS BASELINE (tier TARGETED, timed - run only in a perf slot with PERF_HOLD; Integration 2026-10-24, the user's target:
# campaign and Escalation at >= 300 fps minimum at the original settings, the Off look, native resolution). Phase-0 baseline on the
# final build; Rendering / Systems diff later builds against its CSV.
#
# Rows (one windowed exe at a time, the display's native 2560x1440 window, never a mode change; 4K renders offscreen through
# WFC_RENDERSIZE=3840x2160 in the same window):
#   camp-play   per campaign chapter (A1_IAC .. D5_OME, the kChapters table): direct boot at the chapter start, the bot-driven player
#               (WFC_PLAYERBOT=1), free-running real time, -PlaySeconds (60) of match time (WFC_MATCH_SECONDS ends the run).
#   camp-sweep  per chapter: a fixed-camera sweep at the chapter's start (the 'campaign: start at' position of its play row, eye
#               +1.7 m): 8 headings x 2 pitches (0, -20 deg) through WFC_SHOTLIST (each view held 8 frames; the view's first frame
#               - the camera cut - and its capture frame are left out of the stats; the views before the match is in progress too).
#   esc         per Escalation map (505 BrokenHope, 506 Remnant + the DLC ESC maps): ESC with 4 friendly bots + the bot-driven
#               player, -EscSeconds (240) of match time; frames tagged by wave from 'escalation: wave N begins' (WFC_ESCLOG) and
#               reported for waves 1-3 (the waves a run reaches are listed: bots rarely clear wave 1 - see FINDINGS below).
# Settings: the Off look = a fresh profile with RayTracing 0, HD 0, VSync 0, FrameLimit 0 (lib\M05.ps1 Get-BotProfile), WFC_FPS_LIMIT=0,
# auto-detect off; WFC_BOTAI empty = the build's own default (Smart), not the harness's Classic pin.
# Per frame: WFC_SLOWFRAME=0.01 prints every frame's wall interval, the CPU render split (world / chars / fx / transl / post) and
# outside (sim + the rest), and the same frame's GPU time and pass split (world / chars / fx / transl / post).
# Each play / esc row also has a 'steady (from 2 s)' row: the first 2 s after the match starts hold the first-use hitches
# (program / texture first binds), which the full row keeps - the minimum the user sees includes them.
# Per row: frames, average fps (frames / summed interval), 1% low (the fps of the 99th-percentile frame time), 0.1% low, the worst
# frame (ms + fps) and its split: GPU-bound when its GPU time >= 90 % of its interval (the pass = the largest GPU pass), else
# CPU-bound (the part = the largest of the CPU render parts and outside). Target 300 fps = 3.33 ms (Integration 2026-10-24: the user
# said minimum): PASS when the WORST frame of a steady / wave / sweep row is >= 300 fps; the 1% low is reported beside it. The full rows
# (from the match start) carry no verdict: their frames over 3.33 ms in the first 2 s are the load / first-use hitch list (HITCHES.md).
#
#   .\tools\fidelity\minfps.ps1 -Root work\ab\fast_48e42af -OutDir work\minfps\48e42af [-Rows camp-play,camp-sweep,esc]
#        [-Res 2560x1440,3840x2160] [-Chapters A1_IAC,...] [-EscMaps 505,...] [-PlaySeconds 60] [-EscSeconds 240] [-ReportOnly]
#        [-Assets <WFC_ASSETS for the DLC maps>] [-RenderData <WFC_RENDER_DATA>]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Rows = @("camp-play", "camp-sweep", "esc"),
      [string[]]$Res = @("2560x1440", "3840x2160"),
      [string[]]$Chapters = @("A1_IAC", "A2_KON", "A3_COR", "A4_ORB", "A5_TRY", "D1_ORB", "D2_COR", "D3A_IAC", "D3B_IAC", "D5_OME"),
      [string[]]$EscMaps = @("505", "506", "MP_ESC_Tranquillity", "MP_ESC_District", "MP_ESC_Pulse", "MP_ESC_Static"),
      [int]$PlaySeconds = 60, [int]$EscSeconds = 240, [double]$TargetFps = 300, [switch]$ReportOnly,
      [string]$Assets = "F:\Transformers Rebuild\Rebuild\work\assets_dlc\VerticalSlice",
      [string]$RenderData = "F:\Transformers Rebuild\Rebuild\work\render")   # (Integration 2026-10-24: one root incl. the nine DLC maps)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Rows = @($Rows | % { "$_" -split ',' } | ? { $_ }); $Res = @($Res | % { "$_" -split ',' } | ? { $_ })
$Chapters = @($Chapters | % { "$_" -split ',' } | ? { $_ }); $EscMaps = @($EscMaps | % { "$_" -split ',' } | ? { $_ })
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path (Resolve-Path $Root).Path "build-release\bin\wfc_rebuild.exe"
if (-not (Test-Path $exe)) { throw "no exe at $exe" }
$mapIds = @{ "A1_IAC" = 96; "A2_KON" = 97; "A3_COR" = 98; "A4_ORB" = 99; "A5_TRY" = 100; "D1_ORB" = 101; "D2_COR" = 102; "D3A_IAC" = 103; "D3B_IAC" = 104; "D5_OME" = 105 }
$escLevels = @{ "505" = "MP_ESC_BrokenHope"; "506" = "MP_ESC_Remnant" }
$sfRe = 'SLOWFRAME f(\d+) interval ([\d.]+) ms: render ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)\), outside ([-\d.]+); (?:gpu ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)|gpu n/a)'
$cpuParts = "world", "chars", "fx", "transl", "post"

function Run-Row([string]$dir, [hashtable]$extra, [int]$w, [int]$h, [string]$rs, [int]$timeoutS) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    ((Get-BotProfile 4 0 1 -Width $w -Height $h -RayTracing 0) + "Upscaling=0`nFrameGeneration=0`n") | Set-Content -Encoding ASCII (Join-Path $dir "wfc_profile.ini")
    $e = @{ WFC_BOOT = "match"; WFC_LOGEVERY = "0"; WFC_SLOWFRAME = "0.01"; WFC_FPS_LIMIT = "0"; WFC_BOTAI = ""; WFC_HDTEXTURES = "0"
            WFC_RENDER_DATA = $RenderData; WFC_ASSETS = $Assets
            # Rendering's phase-0 parser reads the same logs (2026-10-24): timestamps, FRAME lines, the sim / draw profilers (not CPUPROF)
            WFC_LOGTIME = "1"; WFC_FRAMELOG = "1"; WFC_TICKPROF = "1"; WFC_DRAWPROF = "1"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1" }
    if ($rs -ne "${w}x${h}") { $e.WFC_RENDERSIZE = $rs }
    foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] }
    if (-not (Wait-WfcGpu)) { "GPU busy" | Set-Content (Join-Path $dir "NOTRUN.txt"); return }
    $null = Invoke-WfcExe $exe $dir $e "run.log" $timeoutS
}

# frames of one log: @{ f; ms; cpu = @{world..post, outside}; gpu (or -1); gp = @{world..post}; wave; inProgress }
function Read-Frames([string]$log) {
    $out = New-Object System.Collections.Generic.List[object]; $wave = 0; $live = $false; $start = $null; $t = 0.0
    if (-not (Test-Path $log)) { return @{ frames = $out; start = $null } }
    foreach ($l in [IO.File]::ReadLines((Resolve-Path $log).Path)) {
        if (-not $live -and $l.Contains('MATCH start')) { $live = $true }   # (the match in progress; 'match: launched' precedes the load)
        if ($l -match 'campaign: start at \S+ \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)') { $start = @([double]$Matches[1], [double]$Matches[2], [double]$Matches[3]) }
        if ($l -match 'escalation: wave (\d+) begins') { $wave = [int]$Matches[1] }
        if (-not $l.Contains('SLOWFRAME f')) { continue }
        $m = [regex]::Match($l, $sfRe); if (-not $m.Success) { continue }
        $g = $m.Groups
        if ($live) { $t += [double]$g[2].Value }
        $fr = [ordered]@{ f = [int]$g[1].Value; ms = [double]$g[2].Value; live = $live; wave = $wave; t = $t / 1000.0
                          cpu = [ordered]@{ world = [double]$g[4].Value; chars = [double]$g[5].Value; fx = [double]$g[6].Value; transl = [double]$g[7].Value; post = [double]$g[8].Value; outside = [double]$g[9].Value }
                          gpu = -1.0; gp = $null }
        if ($g[10].Success) { $fr.gpu = [double]$g[10].Value; $fr.gp = [ordered]@{ world = [double]$g[11].Value; chars = [double]$g[12].Value; fx = [double]$g[13].Value; transl = [double]$g[14].Value; post = [double]$g[15].Value } }
        $out.Add([pscustomobject]$fr)
    }
    return @{ frames = $out; start = $start }
}

function Stats($frames, [string]$kind, [string]$map, [string]$rs, [string]$scope, [string]$note = "") {
    $fs = @($frames); $n = $fs.Count
    $row = [ordered]@{ kind = $kind; map = $map; res = $rs; scope = $scope; frames = $n; avg_fps = ""; low1_fps = ""; low01_fps = ""; worst_ms = ""; worst_fps = ""
                       worst_frame = ""; worst_bound = ""; worst_where = ""; worst_cpu_ms = ""; worst_gpu_ms = ""; verdict = "UNKNOWN"; note = $note }
    if ($n -lt 30) { $row.note = (($note, "only $n frames") | ? { $_ }) -join '; '; return [pscustomobject]$row }
    $ms = @($fs | % { $_.ms } | Sort-Object); $sum = ($ms | Measure-Object -Sum).Sum
    $p99 = $ms[[Math]::Min($n - 1, [int][Math]::Floor(0.99 * $n))]; $p999 = $ms[[Math]::Min($n - 1, [int][Math]::Floor(0.999 * $n))]
    $w = $fs | Sort-Object ms -Descending | Select-Object -First 1
    $cpuTot = ($w.cpu.Values | ? { $_ -gt 0 } | Measure-Object -Sum).Sum
    if ($w.gpu -ge 0 -and $w.gpu -ge 0.9 * $w.ms) {
        $bound = "GPU"; $where = ($w.gp.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key
    } else {
        $bound = if ($w.gpu -lt 0) { "CPU? (gpu n/a)" } else { "CPU" }; $where = ($w.cpu.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key
    }
    $row.avg_fps = [Math]::Round(1000.0 * $n / $sum, 1); $row.low1_fps = [Math]::Round(1000.0 / $p99, 1); $row.low01_fps = [Math]::Round(1000.0 / $p999, 1)
    $row.worst_ms = [Math]::Round($w.ms, 2); $row.worst_fps = [Math]::Round(1000.0 / $w.ms, 1); $row.worst_frame = $w.f
    $row.worst_bound = $bound; $row.worst_where = $where; $row.worst_cpu_ms = [Math]::Round($cpuTot, 2); $row.worst_gpu_ms = if ($w.gpu -ge 0) { [Math]::Round($w.gpu, 2) } else { "n/a" }
    $row.verdict = if ($scope -notlike "steady*" -and $scope -notlike "wave*" -and $kind -ne "camp-sweep") { "-" } elseif ($row.worst_fps -ge $TargetFps) { "PASS" } else { "FAIL" }
    return [pscustomobject]$row
}

$results = New-Object System.Collections.Generic.List[object]
foreach ($rs in $Res) {
    $w, $h = 2560, 1440   # the window: the display's native size (no mode change); 4K renders offscreen in it
    $tag = $rs -replace 'x', '_'
    foreach ($ch in $Chapters) {
        $url = "{0}_BASE_m?GameModeTag=SOLO?MapId={1}?WfcDifficulty=1" -f $ch, $mapIds[$ch]
        $dPlay = Join-Path $OutDir "$tag\camp-play_$ch"
        if ($Rows -contains "camp-play") {
            if (-not $ReportOnly) { Run-Row $dPlay @{ WFC_MAP = $ch; WFC_MATCH_URL = $url; WFC_PLAYERBOT = "1"; WFC_MATCH_SECONDS = "$PlaySeconds" } $w $h $rs (300 + $PlaySeconds) }
            $r = Read-Frames (Join-Path $dPlay "wfc.log")
            $results.Add((Stats @($r.frames | ? { $_.live }) "camp-play" $ch $rs "chapter start + $PlaySeconds s"))
            $results.Add((Stats @($r.frames | ? { $_.live -and $_.t -ge 2.0 }) "camp-play" $ch $rs "steady (from 2 s)"))
        }
        if ($Rows -contains "camp-sweep") {
            $r = Read-Frames (Join-Path $dPlay "wfc.log"); $dSw = Join-Path $OutDir "$tag\camp-sweep_$ch"
            if (-not $r.start) { $results.Add((Stats @() "camp-sweep" $ch $rs "sweep" "no start position (run camp-play first)")); continue }
            $x, $y, $z = $r.start; $y += 1.7; $views = New-Object System.Collections.Generic.List[string]
            foreach ($p in 0, -20) { foreach ($k in 0..7) {
                $a = $k * [Math]::PI / 4; $pr = $p * [Math]::PI / 180
                $t = "{0:F2},{1:F2},{2:F2}" -f ($x + 10 * [Math]::Sin($a) * [Math]::Cos($pr)), ($y + 10 * [Math]::Sin($pr)), ($z - 10 * [Math]::Cos($a) * [Math]::Cos($pr))
                $views.Add(("h{0:D3}p{1:D2} {2:F2},{3:F2},{4:F2},{5}" -f ($k * 45), [Math]::Abs($p), $x, $y, $z, $t)) } }
            if (-not $ReportOnly) {
                New-Item -ItemType Directory -Force $dSw | Out-Null; $views | Set-Content -Encoding ASCII (Join-Path $dSw "views.txt")
                Run-Row $dSw @{ WFC_MAP = $ch; WFC_MATCH_URL = $url; WFC_SHOTLIST = (Join-Path $dSw "views.txt"); WFC_SHOTDIR = $dSw } $w $h $rs 300
            }
            $s = Read-Frames (Join-Path $dSw "wfc.log")
            $keep = @($s.frames | ? { $_.live -and (($_.f - 1) % 8) -ne 0 -and (($_.f - 1) % 8) -ne 7 })
            $results.Add((Stats $keep "camp-sweep" $ch $rs "16 views at the start (8 headings x 2 pitches)"))
        }
    }
    if ($Rows -contains "esc") {
        foreach ($m in $EscMaps) {
            $lvl = if ($escLevels[$m]) { $escLevels[$m] } else { $m }
            $d = Join-Path $OutDir "$tag\esc_$lvl"
            if (-not $ReportOnly) { Run-Row $d @{ WFC_MAP = $lvl; WFC_MATCH_URL = "${lvl}_BASE_m?GameModeTag=ESC?BotsFriendly=4"; WFC_PLAYERBOT = "1"; WFC_ESCLOG = "1"; WFC_MATCH_SECONDS = "$EscSeconds" } $w $h $rs (300 + $EscSeconds) }
            $r = Read-Frames (Join-Path $d "wfc.log"); $live = @($r.frames | ? { $_.live })
            $reached = (@($live | % { $_.wave }) | Sort-Object -Unique) -join ','
            $results.Add((Stats $live "esc" $lvl $rs "match ($EscSeconds s)" "waves seen: $reached"))
            $results.Add((Stats @($live | ? { $_.t -ge 2.0 }) "esc" $lvl $rs "steady (from 2 s)"))
            foreach ($wv in 1..3) { $results.Add((Stats @($live | ? { $_.wave -eq $wv }) "esc" $lvl $rs "wave $wv")) }
        }
    }
}
# the load / first-use hitch list: per play / esc row, the frames over the target in the first 2 s after the match start
$hit = @("# Load / first-use hitches - frames over $([Math]::Round(1000 / $TargetFps, 2)) ms in the first 2 s after the match start", "",
         "| Row | Map | Res | Frames over | Worst 5 (frame: ms, bound / where) |", "|---|---|---|---|---|")
foreach ($rs in $Res) { $tag = $rs -replace 'x', '_'
    foreach ($dd in Get-ChildItem (Join-Path $OutDir $tag) -Directory -ErrorAction SilentlyContinue | ? { $_.Name -like 'camp-play_*' -or $_.Name -like 'esc_*' }) {
        $fr = @((Read-Frames (Join-Path $dd.FullName "wfc.log")).frames | ? { $_.live -and $_.t -lt 2.0 -and $_.ms -gt 1000 / $TargetFps })
        $top = @($fr | Sort-Object ms -Descending | Select-Object -First 5 | % {
            $b = if ($_.gpu -ge 0.9 * $_.ms) { "GPU / " + ($_.gp.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key } else { "CPU / " + ($_.cpu.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key }
            "f$($_.f): $([Math]::Round($_.ms, 1)) ms, $b" }) -join '; '
        $kind, $map = $dd.Name -split '_', 2
        $hit += "| $kind | $map | $rs | $($fr.Count) | $top |" } }
$hit | Set-Content -Encoding UTF8 (Join-Path $OutDir "HITCHES.md")
$csv = Join-Path $OutDir "minfps.csv"; $results | Export-Csv -NoTypeInformation -Encoding UTF8 $csv
$md = @("# Min-fps baseline - $(Split-Path $Root -Leaf) - $(Get-Date -Format 'yyyy-MM-dd HH:mm')", "",
        "Target $TargetFps fps (3.33 ms) on the 1% low; the Off look (RT 0, HD 0, VSync 0, no frame limit); window 2560x1440, 4K offscreen. Columns: avg / 1% low / 0.1% low fps, the worst frame (ms, fps, frame no.) and where it lands (GPU-bound when its GPU time >= 90 % of the interval: the largest GPU pass; else CPU: the largest CPU part, 'outside' = sim + the rest).", "",
        "| Row | Map | Res | Scope | Frames | Avg | 1% low | 0.1% low | Worst ms (fps) | Worst: bound / where (CPU ms, GPU ms) | Verdict | Note |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|")
foreach ($r in $results) { $md += "| $($r.kind) | $($r.map) | $($r.res) | $($r.scope) | $($r.frames) | $($r.avg_fps) | $($r.low1_fps) | $($r.low01_fps) | $($r.worst_ms) ($($r.worst_fps)) f$($r.worst_frame) | $($r.worst_bound) / $($r.worst_where) ($($r.worst_cpu_ms), $($r.worst_gpu_ms)) | $($r.verdict) | $($r.note) |" }
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "MINFPS.md")
Write-Host "wrote $OutDir\MINFPS.md ($($results.Count) rows)"
Write-Host ("own exes left: " + @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | ? { $_.Path -eq $exe }).Count)
