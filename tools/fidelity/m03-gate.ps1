# MILESTONE 03 ACCEPTANCE GATE - one command for the Integration agent after merging the branches.
#
#   .\tools\fidelity\m03-gate.ps1                 # full gate -> work\fidelity\gate\<stamp>\M03-GATE.md (+ summary.json)
#   .\tools\fidelity\m03-gate.ps1 -Quick          # skip the slow instrumented counters and the long perf runs
#   .\tools\fidelity\m03-gate.ps1 -SkipBuild      # reuse build\ (already configured with -DWFC_BUILD_MEASURE=ON)
#
# Steps (all output inside this worktree's build\ and work\):
#   1. build: .\build.ps1 -Jobs N, then the measurement targets (prof / observe / count)
#   2. render data: tools\render\build_render_data.ps1 when work\render\<map> is missing
#   3. suites: wfc_fidelity --map (all harness suites incl. transform_analyzer / vehicle_profiles /
#      fine_aim_probe / trace_cost), transform-capture, audio-attach, vehicle-visual, map-audit
#      (consumes Rendering's map_audit.json when the tree produces one), perf-profile + perf-report,
#      perf-counters
#   4. compare every report with the committed baseline (tools\fidelity\results\m03-baseline, the
#      milestone-02 measurements) and classify:
#        REGRESSED  status got worse (PASS -> KNOWN/FAIL, KNOWN/INFO -> FAIL) or a new FAIL
#        IMPROVED   status got better (KNOWN -> PASS ...)
#        perf       phase frame time > baseline x 1.15 + 1 ms = REGRESSED, < x 0.85 - 1 ms = IMPROVED
#        audio      a cue that newly stops following its owner = REGRESSED
#        map        a category's NOT INSTANTIATED / WRONG count grew = REGRESSED
#        visual     a still whose image differs from the baseline still = CHANGED (human review;
#                   expected when visual fixes land)
#   5. write M03-GATE.md (concise) + summary.json; exit 1 when any product FAIL exists, 2 when only
#      tool errors (a suite that did not complete), else 0. Tool errors are never counted as FAILs.
# Machine PASS does not resolve the HUMAN CHECK list (HUMAN-CHECK.md); the gate prints it.
param([switch]$Quick, [switch]$SkipBuild, [switch]$WriteBaseline, [switch]$ReportOnly, [int]$CounterTimeoutSec = 1800, [int]$Jobs = 2, [string]$Baseline = "", [string]$OutDir = "", [string]$Map = "MP_IAC_Streets")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
Set-Location $root
if (-not $Baseline) { $Baseline = Join-Path $PSScriptRoot "results\m03-baseline" }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\gate\$stamp" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir "gate.log"
function Step($msg) { $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $msg; $line; Add-Content $log $line }
$steps = [ordered]@{}
function RunStep($name, [scriptblock]$body) {
    if ($ReportOnly) { $steps[$name] = "reused (report only)"; return }
    Step "== $name"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    # Native tools (cmake, compilers, python) write warnings to stderr; under "Stop" PowerShell turns those
    # into terminating errors. Steps judge native tools by $LASTEXITCODE and throw explicitly instead.
    $ErrorActionPreference = "Continue"
    try { & $body *>&1 | ForEach-Object { Add-Content $log "    $_" }; $steps[$name] = "ok ({0:F0} s)" -f $sw.Elapsed.TotalSeconds }
    catch { $steps[$name] = "ERROR: " + $_.Exception.Message; Step "   $name failed: $($_.Exception.Message)" }
}
$git = (git rev-parse --short HEAD 2>$null); $branch = (git rev-parse --abbrev-ref HEAD 2>$null)

# ---- 1. build ----
$cmake = Join-Path $root ".toolchain\cmake-4.4.3-windows-x86_64\bin\cmake.exe"
if (-not $SkipBuild) {
    RunStep "build" {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "build.ps1") -Jobs $Jobs
        if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed ($LASTEXITCODE)" }
    }
    RunStep "build_measure" {
        & $cmake -S $root -B (Join-Path $root "build") -DWFC_BUILD_MEASURE=ON 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
        $t = @("wfc_rebuild_prof", "wfc_rebuild_observe"); if (-not $Quick) { $t += "wfc_rebuild_count" }
        & $cmake --build (Join-Path $root "build") --target $t -j $Jobs
        if ($LASTEXITCODE -ne 0) { throw "measurement build failed" }
    }
}
# ---- 2. render data ----
$rd = Join-Path $root "work\render\$Map"
if (-not (Test-Path (Join-Path $rd "materials_glsl.json"))) {
    RunStep "render_data" { & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\render\build_render_data.ps1") -Map $Map }
}
$env:WFC_RENDER_DATA = Split-Path $rd
# ---- 3. suites ----
$Reports = [ordered]@{}   # suite -> report.json path ($Reports, not $R: PowerShell names are case-insensitive)
RunStep "harness" {
    $tdir = Join-Path $OutDir "harness_traces"; New-Item -ItemType Directory -Force $tdir | Out-Null
    & (Join-Path $root "build\bin\wfc_fidelity.exe") --map --json (Join-Path $OutDir "harness.json") --trace-dir $tdir | Out-File -Encoding UTF8 (Join-Path $OutDir "harness.txt")
}
$Reports.harness = Join-Path $OutDir "harness.json"
RunStep "transform_capture" { & (Join-Path $PSScriptRoot "transform-capture.ps1") -OutDir (Join-Path $OutDir "transform") }
$Reports.transform = Join-Path $OutDir "transform\report.json"
RunStep "audio_attach" { & (Join-Path $PSScriptRoot "audio-attach.ps1") -OutDir (Join-Path $OutDir "audio") }
$Reports.audio = Join-Path $OutDir "audio\report.json"
RunStep "vehicle_visual" { & (Join-Path $PSScriptRoot "vehicle-visual.ps1") -OutDir (Join-Path $OutDir "vehicle_visual") -RenderData $rd }
$Reports.vehicle_visual = Join-Path $OutDir "vehicle_visual\report.json"
RunStep "map_audit" {
    # Rendering's audit (tools/render/audit_map.py) when the merged tree has it and python works.
    $am = Join-Path $root "tools\render\audit_map.py"
    & (Join-Path $PSScriptRoot "map-audit.ps1") -OutDir (Join-Path $OutDir "map") -RenderData $rd
    $dump = Join-Path $OutDir "map\runtime\active_dump.jsonl"
    # Same interpreter tools/render/build_render_data.ps1 uses (AssetTools ships it; only executed).
    $py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"; if (-not (Test-Path $py)) { $py = "python" }
    if ((Test-Path $am) -and (Test-Path $dump)) {
        $ErrorActionPreference = "Continue"   # python writes progress to stderr
        Copy-Item $dump (Join-Path $rd "active_dump.jsonl") -Force
        $vp = Join-Path $root "tools\render\verify_permutations.py"
        if ((Test-Path $vp) -and -not (Test-Path (Join-Path $rd "permutation_check.json"))) { & $py $vp $rd $Map 2>&1 | Out-Null }   # usage: verify_permutations.py <render_data_dir> [MAP]
        & $py $am $Map $rd 2>&1
        if (Test-Path (Join-Path $rd "map_audit.json")) { & (Join-Path $PSScriptRoot "map-audit.ps1") -OutDir (Join-Path $OutDir "map") -RenderData $rd }
    }
}
$Reports.map = Join-Path $OutDir "map\report.json"
# Real exe, scripted scenarios: every scenario runs under WFC_LOCKSTEP (frames = simulated 1/60 s).
RunStep "runtime_probe" {
    & (Join-Path $PSScriptRoot "runtime-probe.ps1") -Exe (Join-Path $root "build\bin\wfc_rebuild.exe") -RenderData (Split-Path $rd) -Name "gate"
    Copy-Item (Join-Path $root "work\fidelity\probe\gate\report.json") (Join-Path $OutDir "probe_report.json") -Force
}
$Reports.probe = Join-Path $OutDir "probe_report.json"
RunStep "render_selftests" { & (Join-Path $PSScriptRoot "render-selftests.ps1") -RenderData (Split-Path $rd) -OutDir (Join-Path $OutDir "selftests") }
$Reports.selftests = Join-Path $OutDir "selftests\report.json"
# Release frame cost (INFO; regression reference). Only when a Release build exists next to build\.
$relExe = Join-Path $root "build-release\bin\wfc_rebuild.exe"
if (-not $Quick -and (Test-Path $relExe)) {
    RunStep "perf_release" { & (Join-Path $PSScriptRoot "perf-release.ps1") -Exe $relExe -RenderData (Split-Path $rd) -OutDir (Join-Path $OutDir "perf_release") }
    $Reports.perf_release = Join-Path $OutDir "perf_release\report.json"
}
RunStep "perf_profile" {
    $sc = if ($Quick) { @("idle", "held_fire") } else { @("idle", "walk", "fineaim", "burst", "held_fire") }
    & (Join-Path $PSScriptRoot "perf-profile.ps1") -Scenarios $sc -OutDir (Join-Path $OutDir "perf")
    & (Join-Path $PSScriptRoot "perf-report.ps1") -PerfDir (Join-Path $OutDir "perf")
}
if (-not $Quick) {
    RunStep "perf_counters" { & (Join-Path $PSScriptRoot "perf-counters.ps1") -OutDir (Join-Path $OutDir "counters") -TimeoutSec $CounterTimeoutSec }
    $Reports.counters = Join-Path $OutDir "counters\report.json"
}

# ---- 4. compare ----
function LoadReport($p) { if ($p -and (Test-Path $p)) { (Get-Content -Raw $p | ConvertFrom-Json) } else { $null } }
$rank = @{ PASS = 0; INFO = 0; SKIP = 0; KNOWN = 1; FAIL = 2 }
$sectionOf = {
    param($id)
    switch -Regex ($id) {
        '^(transform_timeline|transform_analyzer|transform_capture|transform_momentum|transform)\.' { "TRANSFORMATION"; break }
        '^(vehicle_feel|vehicle_profiles|boost|fast_movement)\.|^movement\.vehicle|^orientation\.vehicle|^constants\.vehicle' { "VEHICLE"; break }
        '^vehicle_visual\.|^vehicle_materials\.|^material_consistency\.' { "VISUAL"; break }
        '^audio_attach\.' { "AUDIO ATTACHMENT"; break }
        '^(map_audit|map_content|map)\.' { "MAP COMPLETENESS"; break }
        '^(trace_cost|perf_counters|performance|perf_release)\.' { "PERFORMANCE"; break }
        '^(render_selftest|rendering|known_translator_error)\.' { "RENDERING"; break }
        '^(weapon|weapon_presentation|input_edges)\.' { "WEAPON"; break }
        '^fine_aim' { "FINE AIM"; break }
        default { "OTHER" }
    }
}
$all = New-Object System.Collections.Generic.List[object]
$counts = [ordered]@{ pass = 0; fail = 0; known = 0; info = 0; skip = 0 }
$reg = [ordered]@{}; $imp = [ordered]@{}
foreach ($s in "PERFORMANCE", "VISUAL", "RENDERING", "WEAPON", "AUDIO ATTACHMENT", "TRANSFORMATION", "VEHICLE", "MAP COMPLETENESS", "FINE AIM", "OTHER") { $reg[$s] = New-Object System.Collections.Generic.List[string]; $imp[$s] = New-Object System.Collections.Generic.List[string] }
$fails = New-Object System.Collections.Generic.List[string]
$toolErrors = New-Object System.Collections.Generic.List[string]   # gate/suite tool problems: never product FAILs
$stepOf = @{ harness = "harness"; transform = "transform_capture"; audio = "audio_attach"; vehicle_visual = "vehicle_visual"; map = "map_audit"; counters = "perf_counters"; probe = "runtime_probe"; selftests = "render_selftests"; perf_release = "perf_release" }
$baseFile = @{ harness = "harness.json"; transform = "transform.json"; audio = "audio.json"; vehicle_visual = "vehicle_visual.json"; map = "map.json"; counters = "counters_report.json"; probe = "probe.json"; selftests = "selftests.json"; perf_release = "perf_release.json" }
foreach ($k in $Reports.Keys) {
    $new = LoadReport $Reports[$k]
    if (-not $new) { $toolErrors.Add("$k : report missing - the suite did not complete ($($steps[$k] + $steps[$stepOf[$k]]))"); continue }   # tooling, not product
    foreach ($s in "pass", "fail", "known", "info", "skip") { $counts[$s] += [int]$new.summary.$s }
    $old = LoadReport (Join-Path $Baseline $baseFile[$k])
    $oldById = @{}; if ($old) { foreach ($r in $old.results) { $oldById[$r.id] = $r } }
    foreach ($r in $new.results) {
        $sec = & $sectionOf $r.id
        if ($r.status -eq "FAIL") { $fails.Add("$($r.id) : $($r.note)") }
        $o = $oldById[$r.id]
        if (-not $o) { if ($r.status -eq "FAIL") { $reg[$sec].Add("NEW FAIL $($r.id)") }; continue }
        $dn = $rank[[string]$r.status] - $rank[[string]$o.status]
        if ($dn -gt 0) { $reg[$sec].Add(("{0}: {1} -> {2} ({3})" -f $r.id, $o.status, $r.status, $r.note)) }
        elseif ($dn -lt 0) { $imp[$sec].Add(("{0}: {1} -> {2}" -f $r.id, $o.status, $r.status)) }
    }
}
# perf phases
$perfNow = Join-Path $OutDir "perf\attribution.csv"; $perfOld = Join-Path $Baseline "perf_attribution.csv"
$perfRows = @()
if ((Test-Path $perfNow) -and (Test-Path $perfOld)) {
    $po = @{}; foreach ($r in (Import-Csv $perfOld)) { $po[$r.phase] = $r }
    foreach ($r in (Import-Csv $perfNow)) {
        if ($r.phase -notmatch "steady|firing|after_|settled|mag1|held.all") { continue }
        $o = $po[$r.phase]; if (-not $o) { continue }
        $a = [double]$o.frame_ms; $b = [double]$r.frame_ms
        $perfRows += "{0,-22} {1,8:F2} -> {2,8:F2} ms" -f $r.phase, $a, $b
        if ($b -gt $a * 1.15 + 1) { $reg["PERFORMANCE"].Add(("{0}: {1:F2} -> {2:F2} ms/frame" -f $r.phase, $a, $b)) }
        elseif ($b -lt $a * 0.85 - 1) { $imp["PERFORMANCE"].Add(("{0}: {1:F2} -> {2:F2} ms/frame" -f $r.phase, $a, $b)) }
    }
}
# audio: newly offending cues
$aoNow = Join-Path $OutDir "audio\offending_cues.txt"; $aoOld = Join-Path $Baseline "offending_cues.txt"
if ((Test-Path $aoNow) -and (Test-Path $aoOld)) {
    $key = { param($l) $q = $l.Split("`t"); if ($q.Length -ge 3) { "$($q[0])|$($q[2])" } else { $l } }
    $oldSet = @{}; foreach ($l in (Get-Content $aoOld | Select-Object -Skip 1)) { $oldSet[(& $key $l)] = $true }
    $newSet = @{}
    foreach ($l in (Get-Content $aoNow | Select-Object -Skip 1)) { $kk = & $key $l; $newSet[$kk] = $true; if (-not $oldSet[$kk]) { $reg["AUDIO ATTACHMENT"].Add("newly stops following: " + ($l -replace "`t", " | ")) } }
    foreach ($kk in $oldSet.Keys) { if (-not $newSet[$kk]) { $imp["AUDIO ATTACHMENT"].Add("now follows its owner: $kk") } }
}
# map: class counts per category
$mNow = Join-Path $OutDir "map\inventory.json"; $mOld = Join-Path $Baseline "map_inventory.json"
if ((Test-Path $mNow) -and (Test-Path $mOld)) {
    $a = (Get-Content -Raw $mOld | ConvertFrom-Json).summary; $b = (Get-Content -Raw $mNow | ConvertFrom-Json).summary
    foreach ($cat in $b.PSObject.Properties.Name) {
        foreach ($cls in "NOT INSTANTIATED", "PRESENT + WRONG MATERIAL", "PRESENT + WRONG EFFECT", "PRESENT + NOT RENDERED") {
            $x = if ($a.$cat) { [int]$a.$cat.$cls } else { 0 }; $y = [int]$b.$cat.$cls
            if ($y -gt $x) { $reg["MAP COMPLETENESS"].Add(("{0}: {1} {2} -> {3}" -f $cat, $cls, $x, $y)) }
            elseif ($y -lt $x) { $imp["MAP COMPLETENESS"].Add(("{0}: {1} {2} -> {3}" -f $cat, $cls, $x, $y)) }
        }
    }
}
# visual: stills vs baseline thumbnails
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$visual = @()
$thumbs = Join-Path $Baseline "stills"
if (Test-Path $thumbs) {
    foreach ($t in Get-ChildItem $thumbs -Filter *.png) {
        $rel = $t.BaseName -replace "__", "\"
        $cur = Join-Path $OutDir ($rel + ".png")
        if (-not (Test-Path $cur)) { $visual += "{0}: missing in this run" -f $rel; continue }
        $tmp = Join-Path $OutDir "_thumb.png"
        $img = [System.Drawing.Image]::FromFile($cur); $bm = New-Object System.Drawing.Bitmap $img, 320, 180; $img.Dispose(); $bm.Save($tmp); $bm.Dispose()
        $d = [WfcImage]::Diff([WfcImage]::Luma($t.FullName, 4), [WfcImage]::Luma($tmp, 4), 16.0)
        Remove-Item $tmp
        if ($d[0] -gt 3.0) { $visual += "{0}: CHANGED (mean luma diff {1:F1}, {2:P0} of cells) - review" -f $rel, $d[0], $d[1] }
    }
    foreach ($v in $visual) { $reg["VISUAL"].Add($v) }
}
# ---- baseline writer (Experimental): reports + thumbnails of the comparison stills ----
if ($WriteBaseline) {
    New-Item -ItemType Directory -Force $Baseline, (Join-Path $Baseline "stills") | Out-Null
    foreach ($k in $Reports.Keys) { if (Test-Path $Reports[$k]) { Copy-Item $Reports[$k] (Join-Path $Baseline $baseFile[$k]) -Force } }
    foreach ($p in @(@("perf\attribution.csv", "perf_attribution.csv"), @("audio\offending_cues.txt", "offending_cues.txt"), @("map\inventory.json", "map_inventory.json"),
                     @("harness_traces\vehicle_profiles.json", "vehicle_profiles.json"), @("counters\counters.json", "counters.json"), @("vehicle_visual\materials.json", "vehicle_materials.json"))) {
        $src = Join-Path $OutDir $p[0]; if (Test-Path $src) { Copy-Item $src (Join-Path $Baseline $p[1]) -Force }
    }
    $stillList = @(Get-ChildItem (Join-Path $OutDir "vehicle_visual") -Recurse -Filter *.png | Where-Object { $_.Name -ne "sheet.png" -and $_.FullName -notmatch "_spawn_probe" })
    $stillList += @(Get-ChildItem (Join-Path $OutDir "transform") -Recurse -Filter "f*.png" | Where-Object { $_.Directory.Name -like "*_side" })
    foreach ($s in $stillList) {
        $rel = $s.FullName.Substring($OutDir.Length + 1) -replace '\.png$', ''
        $img = [System.Drawing.Image]::FromFile($s.FullName); $bm = New-Object System.Drawing.Bitmap $img, 320, 180; $img.Dispose()
        $bm.Save((Join-Path $Baseline ("stills\" + ($rel -replace '\\', '__') + ".png")), [System.Drawing.Imaging.ImageFormat]::Png); $bm.Dispose()
    }
    Step "baseline written: $Baseline ($($stillList.Count) thumbnails)"
}
# ---- 5. report ----
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Milestone 03 gate - $branch @ $git ($stamp)")
$md.Add("")
$md.Add(("**PASS {0} / FAIL {1} / KNOWN {2} / INFO {3} / SKIP {4}**  (baseline: {5})" -f $counts.pass, $counts.fail, $counts.known, $counts.info, $counts.skip, (Split-Path $Baseline -Leaf)))
$md.Add("")
$md.Add("Steps: " + (($steps.Keys | ForEach-Object { "$_ $($steps[$_])" }) -join "; "))
foreach ($s in "PERFORMANCE", "VISUAL", "RENDERING", "WEAPON", "AUDIO ATTACHMENT", "TRANSFORMATION", "VEHICLE", "MAP COMPLETENESS", "FINE AIM", "OTHER") {
    $md.Add(""); $md.Add("## $s REGRESSIONS ($($reg[$s].Count))")
    if ($reg[$s].Count) { foreach ($x in ($reg[$s] | Select-Object -First 40)) { $md.Add("- $x") } } else { $md.Add("- none") }
    if ($imp[$s].Count) { $md.Add("Improved ($($imp[$s].Count)): " + (($imp[$s] | Select-Object -First 12) -join "; ") + $(if ($imp[$s].Count -gt 12) { " ..." } else { "" })) }
}
if ($perfRows.Count) { $md.Add(""); $md.Add("## Performance phases (baseline -> now)"); $md.Add('```'); foreach ($p in $perfRows) { $md.Add($p) }; $md.Add('```') }
$md.Add(""); $md.Add("## TOOL ERRORS ($($toolErrors.Count)) - gate/suite problems, not product results"); if ($toolErrors.Count) { foreach ($x in $toolErrors) { $md.Add("- $x") } } else { $md.Add("- none") }
$md.Add(""); $md.Add("## FAIL ($($fails.Count))"); if ($fails.Count) { foreach ($f in ($fails | Select-Object -First 40)) { $md.Add("- $f") } } else { $md.Add("- none") }
$md.Add(""); $md.Add("## HUMAN CHECK (machine PASS does not resolve these)")
$hc = Join-Path $PSScriptRoot "HUMAN-CHECK.md"
if (Test-Path $hc) { foreach ($l in (Get-Content $hc | Where-Object { $_ -match '^\s*- \[' })) { $md.Add($l) } }
$md.Add(""); $md.Add("Artifacts: $OutDir (harness.json, transform\*\sheet.png, audio\offending_cues.txt, vehicle_visual\sheet.png, map\inventory.csv, perf\attribution.csv)")
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "M03-GATE.md")
[ordered]@{ branch = $branch; commit = $git; counts = $counts; regressions = $reg; improvements = $imp; fails = $fails; tool_errors = $toolErrors; steps = $steps } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $OutDir "summary.json")
Remove-Item Env:WFC_RENDER_DATA -ErrorAction SilentlyContinue
Get-Content (Join-Path $OutDir "M03-GATE.md")
if ($counts.fail -gt 0) { exit 1 } elseif ($toolErrors.Count -gt 0) { exit 2 } else { exit 0 }
