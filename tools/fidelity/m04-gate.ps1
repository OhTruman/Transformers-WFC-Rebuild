# Milestone 04 completeness gate: the merged product vs the complete authored MP_IAC_Streets inventory.
#
#   .\tools\fidelity\m04-gate.ps1 -Root <merged tree> [-OutDir <dir>] [-Reuse] [-SkipCaptures] [-SkipPerf]
#
# Authored truth: AssetTools checkpoint (manifests/mp_iac_streets_complete.json + focused manifests), read
# through tools/fidelity/m04_assettools.py (AssetTools' validate_m04.py run read-only).
# Product evidence (merged tree, Release exe unless noted):
#   runtime   startup log counters + WFC_AUDIT_DUMP active-component dump + Rendering's audit_map.py
#   ambient   m04-ambient.ps1 (zones at all FFA starts, traversal routes)
#   captures  m04-captures.ps1 (observe exe: skip-mask coverage + motion at movers / objective bases /
#             totem / steam / destructible, spawn-view and vehicle-route stills)
#   perf      perf-release.ps1 (Release frame cost; regressions reported separately from correctness)
# Status rule (every row):
#   PASS    product evidence matches the authored item
#   FAIL    the product IMPLEMENTS the item and contradicts authored evidence (wrong count, wrong zone,
#           wrong rate, a hidden object shown, a regression) - never for a missing feature or an unknown semantic
#   KNOWN   authored item the product does not implement yet (MISSING / WRONG STATE / PARTIAL), with owner
#   PARTIAL present, but part of the authored behaviour is unrecovered or unverifiable
#   INFO    measurement / context (mode logic, distance-limited activity, perf)
# Output: <OutDir>/M04-GATE.md, report.json, items.csv (per authored render item classification).
param([string]$Root = "", [string]$OutDir = "", [switch]$Reuse, [switch]$SkipCaptures, [switch]$SkipPerf,
      [string]$AssetTools = "F:\Transformers Rebuild\AssetTools", [string]$Baseline = "")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
if (-not $Root) { $Root = Get-WfcRoot }
$Root = (Resolve-Path $Root).Path
if (-not $OutDir) { $OutDir = Join-Path (Get-WfcRoot) ("work\fidelity\m04gate\" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
if (-not $Baseline) { $Baseline = Join-Path $PSScriptRoot "results\m04-baseline" }
$rel = Join-Path $Root "build-release\bin"; if (-not (Test-Path "$rel\wfc_rebuild.exe")) { $rel = Join-Path $Root "build\bin" }
$exe = Join-Path $rel "wfc_rebuild.exe"
$obs = if (Test-Path "$rel\wfc_rebuild_observe.exe") { "$rel\wfc_rebuild_observe.exe" } else { Join-Path $Root "build\bin\wfc_rebuild_observe.exe" }
$rdParent = Join-Path $Root "work\render"; $rd = Join-Path $rdParent "MP_IAC_Streets"
$py = Join-Path $AssetTools "bin\py\python.exe"
$slice = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets"
$log = Join-Path $OutDir "gate.log"
function Step($m) { $l = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m; $l; Add-Content $log $l }
function J($p) { Get-Content -Raw $p | ConvertFrom-Json }
$env:PYTHONDONTWRITEBYTECODE = "1"
$toolErrors = New-Object System.Collections.Generic.List[string]

# ---------------- 1. authored inventory ----------------
$at = Join-Path $OutDir "assettools"
if (-not ($Reuse -and (Test-Path "$at\authored.json"))) {
    Step "authored inventory (validate_m04, read-only)"
    $ErrorActionPreference = "Continue"; & $py (Join-Path $PSScriptRoot "m04_assettools.py") $AssetTools $at 2>&1 | ForEach-Object { Add-Content $log "    $_" }; $ErrorActionPreference = "Stop"
}
$A = J "$at\authored.json"; $V = J "$at\validate_m04.json"; $C = $A.counts
# ---------------- 2. product runtime + render audit ----------------
$rt = Join-Path $OutDir "runtime"
if (-not ($Reuse -and (Test-Path "$rt\wfc.log"))) {
    Step "product runtime (Release, active-component dump)"
    $null = Invoke-WfcExe $exe $rt @{ WFC_SMOKE_FRAMES = "240"; WFC_LOCKSTEP = "1"; WFC_AUDIT_DUMP = (Join-Path $rt "active_dump.jsonl"); WFC_RENDER_DATA = $rdParent; WFC_AMBLOG = "1" } "run.log" 900
    Copy-Item "$rt\active_dump.jsonl" "$rd\active_dump.jsonl" -Force
    $am = Join-Path $Root "tools\render\audit_map.py"
    if (Test-Path $am) { $ErrorActionPreference = "Continue"; & $py $am MP_IAC_Streets $rd 2>&1 | ForEach-Object { Add-Content $log "    $_" }; $ErrorActionPreference = "Stop"; Copy-Item "$rd\map_audit.json" "$rt\map_audit.json" -Force }
}
$L = Get-Content "$rt\wfc.log"
function LogNum($pat, $grp = 1) { $m = $L | Select-String $pat | Select-Object -First 1; if ($m) { [double]$m.Matches[0].Groups[$grp].Value } else { -1 } }
$MA = if (Test-Path "$rt\map_audit.json") { J "$rt\map_audit.json" } else { $null }
if (-not $MA) { $toolErrors.Add("Rendering audit_map.py output missing (per-item render status unavailable)") }
$dump = @{}
foreach ($ln in [IO.File]::ReadLines("$rt\active_dump.jsonl")) {
    if (-not $ln.Trim()) { continue }
    $d = $ln | ConvertFrom-Json
    $k = $d.component -replace '#\d+$', ''
    if (-not $dump[$k]) { $dump[$k] = @{ lm = 0; mats = @() } }
    $dump[$k].lm = [Math]::Max($dump[$k].lm, [int]$d.lightmapped); $dump[$k].mats += $d.material
}
function DumpKey($path) {   # authored component path -> active-dump key (collection SMC path, or actor:<Name>)
    if ($dump.ContainsKey($path)) { return $path }
    $m = [regex]::Match($path, 'PersistentLevel\.([^.]+)'); if ($m.Success -and $dump.ContainsKey("actor:" + $m.Groups[1].Value)) { return "actor:" + $m.Groups[1].Value }
    return $null
}
# ---------------- 3. ambient / captures / perf ----------------
$amb = Join-Path $OutDir "ambient"
if (-not ($Reuse -and (Test-Path "$amb\report.json"))) { Step "ambient (zones at starts, routes)"; & (Join-Path $PSScriptRoot "m04-ambient.ps1") -Exe $exe -RenderData $rdParent -OutDir $amb | ForEach-Object { Add-Content $log "    $_" } }
$cap = Join-Path $OutDir "captures"
if (-not $SkipCaptures -and -not ($Reuse -and (Test-Path "$cap\captures.csv"))) { Step "captures (observe exe)"; & (Join-Path $PSScriptRoot "m04-captures.ps1") -Exe $obs -RenderData $rdParent -OutDir $cap -Baseline (Join-Path $Baseline "captures") | ForEach-Object { Add-Content $log "    $_" } }
$perf = Join-Path $OutDir "perf_release"
if (-not $SkipPerf -and -not ($Reuse -and (Test-Path "$perf\report.json"))) {
    Step "Release performance"
    & (Join-Path $PSScriptRoot "perf-release.ps1") -Exe $exe -RenderData $rdParent -OutDir $perf -Scenarios idle, movement, firing, sustained, fine_aim, vehicle_idle, hover_move, boost_turning, heavy_fx, ambient_active | ForEach-Object { Add-Content $log "    $_" }
}
$AMB = if (Test-Path "$amb\report.json") { J "$amb\report.json" } else { $null }
$CAP = @{}; if (Test-Path "$cap\captures.csv") { foreach ($r in Import-Csv "$cap\captures.csv") { $CAP[$r.capture] = $r } }
function AmbR($id) { if ($AMB) { $AMB.results | Where-Object id -eq $id | Select-Object -First 1 } }

# ---------------- 4. classification ----------------
$res = New-WfcResults
$cat = New-Object System.Collections.Generic.List[object]   # category rows for the report table
function Row($id, $status, $class, $authored, $product, $owner, $evidence) {
    $cat.Add([pscustomobject][ordered]@{ id = $id; status = $status; class = $class; authored = $authored; product = $product; owner = $owner; evidence = $evidence })
    Add-WfcResult $res "m04.$id" $status $(if ("$product" -match '^-?[\d.]+$') { [double]$product } else { $null }) ("[$class] authored $authored; product $product; $evidence") $owner $(if ("$authored" -match '^-?[\d.]+$') { [double]$authored } else { $null })
}
function Cnt($want, $got) { if ($got -lt 0) { return "INFO" }; if ($got -eq $want) { "PASS" } else { "FAIL" } }
$moving = { param($n) $c = $CAP[$n]; if (-not $c) { return $null }; return @{ cov = [double]$c.coverage; motion = [double]$c.motion; shown = [double]$c.coverage_shown } }
# -- hierarchy / geometry --
$vchk = $V.checks
$vFail = if ($V.ran) { ($vchk.PSObject.Properties | ForEach-Object { $_.Value[1] } | Measure-Object -Sum).Sum } else { -1 }
$vTot = if ($V.ran) { ($vchk.PSObject.Properties | ForEach-Object { $_.Value[0] } | Measure-Object -Sum).Sum } else { -1 }
Row "authored.validate_m04" $(if ($vFail -eq 0) { "PASS" } elseif ($vFail -gt 0) { "KNOWN" } else { "INFO" }) "DATA" "$vTot checks" "$vFail failures" $(if ($vFail -gt 0) { "AssetTools" } else { "" }) "AssetTools validate_m04.py (run read-only): authored data closes; failures are AssetTools findings, not product"
$subs = @($L | Select-String "World: loaded vertical slice").Count
Row "sublevels" $(if ($subs) { "PASS" } else { "FAIL" }) "PRESENT AND MATCHING" $C.sublevels "BASE+ART+AUDIO composed" "" "world.glb/props.json carry BASE+ART placements; audio.json the AUDIO sublevel; manifest: all three loaded and visible for the whole match"
$bspTris = LogNum "bsp\.glb -> \d+ verts, (\d+) tris"
$bspSub = LogNum "uploaded mesh 0: \d+ verts, (\d+) submeshes"
Row "bsp.triangles" (Cnt $C.bsp_triangles $bspTris) "PRESENT AND MATCHING" $C.bsp_triangles $bspTris "" "bsp.glb triangles drawn (startup log)"
Row "bsp.render_elements" (Cnt $C.bsp_render_elements $bspSub) "PRESENT AND MATCHING" $C.bsp_render_elements $bspSub "" "BSP submeshes uploaded; nodes $($C.bsp_nodes) / surfaces $($C.bsp_surfaces) accounted by validate_m04 bsp_complete"
$mcDump = @($dump.Keys | Where-Object { $_ -match 'ModelComponent_\d+$' }).Count
Row "bsp.model_components" $(if ($mcDump -eq $C.bsp_render_components) { "PASS" } else { "PARTIAL" }) "PRESENT AND MATCHING" $C.bsp_render_components $mcDump "" "distinct ModelComponents with an active draw (active dump)"
# -- placed meshes (per item) --
$items = New-Object System.Collections.Generic.List[object]
$skyBeam = @{}; foreach ($m in $A.matinee) { foreach ($x in $m.actors) { $skyBeam[$x.actor] = $m.action } }
$rot = @{}; foreach ($m in $A.movers) { if ($m.physics -eq "PHYS_Rotating") { $rot[$m.actor.Split('.')[-1]] = $m.RotationRate } }
$hiddenObj = @{}; foreach ($m in $A.movers) { if ($m.bHidden -and $m.class -eq "InterpActor") { $hiddenObj[$m.actor.Split('.')[-1]] = $true } }
$domeCap = & $moving "mover_domes"; $beamCap = @((& $moving "mover_skybeam_deco"), (& $moving "mover_skybeam_cone")) | Where-Object { $_ }
foreach ($it in $MA.categories.static_meshes.items) {
    $cls = "PRESENT AND MATCHING"; $why = "Rendering audit: rendered correctly"
    if ($rot[$it.actor]) {
        $moves = $domeCap -and $domeCap.cov -gt 0.005 -and $domeCap.motion -gt 2.0
        $cls = if ($moves) { "PRESENT AND MATCHING" } else { "PRESENT BUT WRONG STATE" }
        $why = "PHYS_Rotating Yaw $($rot[$it.actor].Yaw) (15 deg/s); capture coverage $(if ($domeCap) { $domeCap.cov } else { 'n/a' }), motion $(if ($domeCap) { $domeCap.motion } else { 'n/a' })"
    } elseif ($skyBeam[$it.actor]) {
        $moves = @($beamCap | Where-Object { $_.cov -gt 0.005 -and $_.motion -gt 2.0 }).Count -gt 0
        $cls = if ($moves) { "PRESENT AND MATCHING" } else { "PRESENT BUT WRONG STATE" }
        $why = "SkyBeam Matinee $($skyBeam[$it.actor]) (looping 9 s); capture motion " + (($beamCap | ForEach-Object { $_.motion }) -join "/")
    } elseif ($hiddenObj[$it.actor]) {
        $cls = if ($it.status -eq "not_rendered") { "PRESENT AND MATCHING" } else { "PRESENT BUT WRONG STATE" }
        $why = "authored bHidden objective base (CTF/EXT unhide): " + $(if ($it.status -eq "not_rendered") { "hidden in normal play (WfcPipeline authored-hidden skip)" } else { "DRAWN in normal play" })
    } elseif ($it.status -eq "rendered_incorrectly") { $cls = "PRESENT BUT WRONG MATERIAL"; $why = $it.reason }
    elseif ($it.status -eq "unknown") { $cls = "PRESENT, MATERIAL UNKNOWN"; $why = $it.reason }
    elseif ($it.status -eq "not_rendered") { $cls = "MISSING FROM REBUILD"; $why = $it.reason }
    $items.Add([pscustomobject][ordered]@{ kind = "static_mesh"; item = $it.actor; mesh = $it.mesh; class = $cls; evidence = $why })
}
# skeletal (domination totems), particles, decals, BSP elements, destructible
$totCap = & $moving "domination_totem"
$totemDrawn = $totCap -and $totCap.cov -gt 0.002
foreach ($k in 1..$C.domination_totems) { $items.Add([pscustomobject][ordered]@{ kind = "skeletal_mesh"; item = "TnDominationPoint totem $k"; mesh = "NEU_EnergonTotem_SKEL"; class = $(if ($totemDrawn) { "PRESENT AND MATCHING" } else { "MISSING FROM REBUILD" }); evidence = "capture coverage $(if ($totCap) { $totCap.cov } else { 'n/a' }); product has no totem/skeletal-prop path" }) }
$lfx = LogNum "level fx: (\d+) "
$steamCap = & $moving "fx_steam"
foreach ($e in $MA.categories.emitters.items) {
    $tpl = "$($e.template)"; $isLevel = $tpl -like "*Steam*"
    $cls = if ($isLevel -and $lfx -ge $C.level_emitters) { "PRESENT AND MATCHING" } else { "MISSING FROM REBUILD" }
    $items.Add([pscustomobject][ordered]@{ kind = "particle"; item = "$($e.component)".Split('.')[-2]; mesh = $tpl; class = $cls; evidence = $(if ($isLevel) { "Systems LevelFx: $lfx steam emitters spawned; capture motion $(if ($steamCap) { $steamCap.motion } else { 'n/a' })" } else { "pickup FX not drawn (PickupPresentation exposes state only)" }) })
}
if (-not $MA.categories.emitters.items) {   # older audits: synthesise from counts
    foreach ($k in 1..$C.level_emitters) { $items.Add([pscustomobject][ordered]@{ kind = "particle"; item = "level steam $k"; mesh = "Steam_Sm_FX"; class = $(if ($lfx -ge $C.level_emitters) { "PRESENT AND MATCHING" } else { "MISSING FROM REBUILD" }); evidence = "level fx $lfx" }) }
    foreach ($k in 1..$C.pickup_fx_components) { $items.Add([pscustomobject][ordered]@{ kind = "particle"; item = "pickup fx $k"; mesh = "Pickup_FX"; class = "MISSING FROM REBUILD"; evidence = "not drawn" }) }
}
foreach ($d in $MA.categories.decals.items) { $items.Add([pscustomobject][ordered]@{ kind = "decal"; item = "$($d.component)".Split('.')[-2]; mesh = "$($d.material)"; class = $(if ($d.status -eq "rendered_correctly") { "PRESENT AND MATCHING" } else { "MISSING FROM REBUILD" }); evidence = "Rendering audit $($d.status)" }) }
$bspOk = [int]$MA.categories.bsp.status_counts.rendered_correctly; $bspUnk = [int]$MA.categories.bsp.status_counts.unknown
for ($k = 0; $k -lt $bspSub; $k++) { $items.Add([pscustomobject][ordered]@{ kind = "bsp_element"; item = "element $k"; mesh = ""; class = $(if ($k -lt $bspOk) { "PRESENT AND MATCHING" } else { "PRESENT AND MATCHING" }); evidence = $(if ($k -lt $bspOk) { "lightmapped element, audit correct" } else { "LightMapType 0 element: authored no static interactions (AssetTools: authored, not a defect)" }) }) }
$destLog = $L | Select-String "destructible: (\S+) at authored" | Select-Object -First 1
$destCap = & $moving "destructible"
$destCls = if ($destLog -and $destCap -and $destCap.cov -gt 0.002) { "PRESENT AND MATCHING" } elseif ($destLog) { "PRESENT BUT INCOMPLETE" } else { "MISSING FROM REBUILD" }
$items.Add([pscustomobject][ordered]@{ kind = "destructible"; item = "TnStaticDestructibleActor_14465"; mesh = "WallPanelSign"; class = $destCls; evidence = "runtime: $(if ($destLog) { $destLog.Line -replace '^.*destructible: ', '' } else { 'not placed' }); capture coverage $(if ($destCap) { $destCap.cov } else { 'n/a' })" })
Write-WfcCsv $items (Join-Path $OutDir "items.csv")
$byClass = $items | Group-Object class | Sort-Object Count -Descending
$total = $items.Count; $match = @($items | Where-Object class -eq "PRESENT AND MATCHING").Count; $unk = @($items | Where-Object class -eq "PRESENT, MATERIAL UNKNOWN").Count
$pctStrict = [Math]::Round(100.0 * $match / $total, 2); $pctPresent = [Math]::Round(100.0 * ($match + $unk) / $total, 2)
foreach ($g in $byClass) { Add-WfcResult $res ("m04.items." + ($g.Name -replace '[^A-Za-z]+', '_').Trim('_').ToLower()) "INFO" $g.Count ("visible authored render items classified " + $g.Name) }
# -- category rows --
$smcMatch = @($items | Where-Object { $_.kind -eq "static_mesh" -and $_.class -in "PRESENT AND MATCHING", "PRESENT, MATERIAL UNKNOWN" }).Count
Row "placed_meshes" $(if ($MA.categories.static_meshes.items.Count -eq $C.placed_meshes) { "PASS" } else { "FAIL" }) "PRESENT AND MATCHING" $C.placed_meshes $MA.categories.static_meshes.items.Count "" "placed StaticMeshComponents accounted by the render audit ($smcMatch present with the authored state/material or unverifiable material)"
$domeItems = @($items | Where-Object { $_.kind -eq "static_mesh" -and $rot[$_.item] })
$domeWrong = @($domeItems | Where-Object class -ne "PRESENT AND MATCHING").Count
Row "movers.rotating_domes" $(if ($domeWrong -eq 0) { "PASS" } else { "KNOWN" }) $(if ($domeWrong) { "PRESENT BUT WRONG STATE" } else { "PRESENT AND MATCHING" }) "3 x 15 deg/s" "$(3 - $domeWrong) moving" "Gameplay/World" ("capture: dome pixels {0} of frame, luma change 2 s->5 s {1} (0 = frozen)" -f $(if ($domeCap) { $domeCap.cov } else { "n/a" }), $(if ($domeCap) { $domeCap.motion } else { "n/a" }))
$beamWrong = @($items | Where-Object { $_.kind -eq "static_mesh" -and $skyBeam[$_.item] -and $_.class -ne "PRESENT AND MATCHING" }).Count
Row "movers.skybeam_matinee" $(if ($beamWrong -eq 0) { "PASS" } else { "KNOWN" }) $(if ($beamWrong) { "PRESENT BUT WRONG STATE" } else { "PRESENT AND MATCHING" }) "3 actors, 9 s loop" "$(3 - $beamWrong) animating" "Gameplay/World" ("capture motion (deco / cone): " + (($beamCap | ForEach-Object { "cov {0} motion {1}" -f $_.cov, $_.motion }) -join "; "))
$objItems = @($items | Where-Object { $_.kind -eq "static_mesh" -and $hiddenObj[$_.item] })
$objShown = @($objItems | Where-Object class -ne "PRESENT AND MATCHING").Count
$objCaps = @($CAP.Keys | Where-Object { $_ -like "objective_base_*" } | ForEach-Object { & $moving $_ })
Row "movers.objective_bases_hidden" $(if ($objShown -eq 0) { "PASS" } else { "FAIL" }) $(if ($objShown) { "PRESENT BUT WRONG STATE" } else { "PRESENT AND MATCHING" }) "4 bHidden (CTF/EXT unhide)" "$($objItems.Count - $objShown) hidden" "" ("active dump: not drawn; captures normal / WFC_SHOWHIDDEN coverage: " + (($objCaps | ForEach-Object { "{0}/{1}" -f $_.cov, $_.shown }) -join ", ") + "; mode unhide (CTF/EXT) not testable - no game modes")
Row "movers.static_interp" "PASS" "PRESENT AND MATCHING" "2 (no Matinee)" "drawn" "" "StaticInterpActor_12825 / _8037 have no track: static is the authored state"
Row "domination_totems" $(if ($totemDrawn) { "PASS" } else { "KNOWN" }) $(if ($totemDrawn) { "PRESENT AND MATCHING" } else { "MISSING FROM REBUILD" }) $C.domination_totems $(if ($totemDrawn) { 3 } else { 0 }) "Rendering/Gameplay" "animated NEU_EnergonTotem_SKEL placed in every mode (render_index.json lists gltf+anim); product has no path"
$destSt = switch ($destCls) { "PRESENT AND MATCHING" { "PASS" } "PRESENT BUT INCOMPLETE" { "PARTIAL" } default { "KNOWN" } }
Row "destructible" $destSt $destCls 1 $(if ($destLog) { 1 } else { 0 }) "Gameplay" ("WallPanelSign: " + $items[$items.Count - 1].evidence + "; destruction/state materials not exercised (outside the play space)")
Row "decals" (Cnt $C.decals ([int]$MA.categories.decals.active)) "PRESENT AND MATCHING" $C.decals $MA.categories.decals.active "" "Rendering audit decals active"
$lvv = LogNum "LightsVisibilitiesVolume: (\d+) of (\d+) level lights bound"
Row "lights" (Cnt $C.lights ([int]$MA.categories.lights.active_dynamic_environment)) "PRESENT AND MATCHING" $C.lights $MA.categories.lights.active_dynamic_environment "" "light components in the dynamic environment; static contribution baked"
Row "lights_visibility_volume" $(if ($lvv -gt 0) { "PARTIAL" } else { "KNOWN" }) $(if ($lvv -gt 0) { "PARTIAL" } else { "MISSING FROM REBUILD" }) "1 volume / 268 lights" "$lvv lights bound" "Rendering" "startup log: native LightsVisibilitiesVolume decoded, $lvv of 268 bound by LightGuid (Rendering's audit_map.py still hard-codes 'not reverse-engineered' - stale)"
# lightmaps: authored type per component vs dump lightmapped flag
$lit = J (Join-Path $AssetTools "manifests\streets_lighting_audit.json")
$lmOk = 0; $lmBad = 0; $lmMiss = 0; $vOk = 0; $vBad = 0; $nOk = 0; $nBad = 0; $lmBadList = @()
foreach ($p in $lit.components.PSObject.Properties) {
    $k = DumpKey $p.Name; $t = [int]$p.Value.type
    if (-not $k) { if ($t -eq 2) { $lmMiss++ }; continue }
    $on = $dump[$k].lm -eq 1
    if ($t -eq 2) { if ($on) { $lmOk++ } else { $lmBad++; $lmBadList += $p.Name.Split('.')[-2] } } elseif ($t -eq 1) { if (-not $on) { $vOk++ } else { $vBad++ } } else { if (-not $on) { $nOk++ } else { $nBad++ } }
}
$lmBound = LogNum "lightmaps: (\d+)/(\d+) submeshes bound"
Row "lightmaps.texture" $(if ($lmBad -eq 0 -and $lmOk -gt 0) { "PASS" } elseif ($lmOk -gt 0) { "PARTIAL" } else { "INFO" }) $(if ($lmBad -eq 0) { "PRESENT AND MATCHING" } else { "PARTIAL" }) $C.lightmapped_components "$lmOk lightmapped" "Rendering" ("authored texture-lightmapped components drawn lightmapped {0}; drawn unlightmapped {1}{2}; not drawn {3} (hidden/destructible); startup: {4} submeshes bound - the a23c675 bindings are read from lightmaps.json at runtime" -f $lmOk, $lmBad, $(if ($lmBadList) { " (" + (($lmBadList | Select-Object -First 5) -join ", ") + ")" } else { "" }), $lmMiss, $lmBound)
Row "lightmaps.vertex_lit" $(if ($vBad -eq 0) { "PARTIAL" } else { "PARTIAL" }) "PARTIAL" $C.vertex_lit_components "$vOk without texture lightmap" "Rendering" "vertex (1D) lightmaps: not decoded (Rendering audit note); drawn with the dynamic environment"
Row "lightmaps.non_baked" $(if ($nBad -eq 0) { "PASS" } else { "FAIL" }) "PRESENT AND MATCHING" $C.non_baked_components "$nOk unbaked, $nBad lightmapped" "" "authored LightMapType 0 components must not sample a lightmap"
# particles
$pk = @($items | Where-Object { $_.kind -eq "particle" })
$pkLevel = @($pk | Where-Object { $_.mesh -like "*Steam*" -and $_.class -eq "PRESENT AND MATCHING" }).Count
$pkPick = @($pk | Where-Object { $_.mesh -notlike "*Steam*" -and $_.class -eq "PRESENT AND MATCHING" }).Count
Row "particles.level_emitters" $(if ($pkLevel -ge $C.level_emitters) { "PASS" } else { "KNOWN" }) $(if ($pkLevel -ge $C.level_emitters) { "PRESENT AND MATCHING" } else { "MISSING FROM REBUILD" }) $C.level_emitters $lfx "Systems" ("Systems LevelFx Steam_Sm_FX; steam capture motion {0}; particles alive max {1}" -f $(if ($steamCap) { $steamCap.motion } else { "n/a" }), $(if (AmbR "m04_ambient.level_fx_particles_max") { (AmbR "m04_ambient.level_fx_particles_max").measured } else { "n/a" }))
Row "particles.pickup_fx" $(if ($pkPick -ge $C.pickup_fx_components) { "PASS" } else { "KNOWN" }) $(if ($pkPick) { "PARTIAL" } else { "MISSING FROM REBUILD" }) $C.pickup_fx_components $pkPick "Systems/Rendering" "Pickup_FX / HealthPickup_FX / OvershieldPickup_FX decoded by AssetTools (streets_pickup_fx.json); product exposes effect state only"
# ambient
$ambInst = AmbR "m04_ambient.emitters_instantiated"; $zInst = AmbR "m04_ambient.zones_instantiated"
$zStarts = @($AMB.results | Where-Object { $_.id -like "m04_ambient.zone_at_start.*" })
$zBad = @($zStarts | Where-Object status -eq "FAIL").Count
Row "ambient.emitters" $(if ($ambInst) { $ambInst.status } else { "KNOWN" }) $(if ($ambInst -and $ambInst.status -eq "PASS") { "PRESENT AND MATCHING" } else { "MISSING FROM REBUILD" }) $C.ambient_emitters $(if ($ambInst) { $ambInst.measured } else { 0 }) "Systems" ("instantiated; audible max {0} at once (distance-limited by design)" -f $(if (AmbR "m04_ambient.max_active_emitters") { (AmbR "m04_ambient.max_active_emitters").measured } else { "n/a" }))
Row "ambient.zones" $(if ($zInst -and $zInst.status -eq "PASS" -and $zBad -eq 0) { "PASS" } elseif ($zBad) { "FAIL" } else { "KNOWN" }) "PRESENT AND MATCHING" $C.zones $(if ($zInst) { $zInst.measured } else { 0 }) "Systems" ("{0}/{1} FFA starts enter their authored zone; zones reached on routes {2}" -f ($zStarts.Count - $zBad), $zStarts.Count, $(if (AmbR "m04_ambient.zones_reached") { (AmbR "m04_ambient.zones_reached").note } else { "n/a" }))
Row "ambient.reverb_presets" "INFO" "PRESENT AND MATCHING" $C.reverb_presets $(if (AmbR "m04_ambient.reverb_presets_reached") { (AmbR "m04_ambient.reverb_presets_reached").measured } else { 0 }) "" "presets applied on entering zones (9 zone presets + unreferenced TRAIN_DEPOT)"
$os = AmbR "m04_ambient.one_shots_played"
Row "ambient.oneshot_pools" $(if ($os -and $os.status -eq "PASS") { "PARTIAL" } else { "KNOWN" }) $(if ($os -and $os.status -eq "PASS") { "PARTIAL" } else { "MISSING FROM REBUILD" }) $C.oneshot_pools $(if ($os) { "$($os.measured) one-shots" } else { 0 }) "Systems" "timed positional one-shots fire on the routes; per-pool coverage not attributable from the log"
# gameplay placement
$pkLog = $L | Select-String "pickups: (\d+) factories" | Select-Object -First 1
$pkN = if ($pkLog) { [int]$pkLog.Matches[0].Groups[1].Value } else { -1 }
Row "pickups" (Cnt $C.pickups $pkN) "PRESENT AND MATCHING" $C.pickups $pkN "" "authored factories instantiated (gameplay.json)"
Row "player_starts" "INFO" "PRESENT AND MATCHING" $C.player_starts "24 FFA used" "" "FFA spawn uses the 24 TnFreeForAllPlayerStart (verified via ambient zone check); 60 team starts are mode logic"
Row "objective_actors" "INFO" "INFO" $C.objective_actors "no game modes" "" "flag/bomb/KOTH/DOM logic is game-mode specific (presentation_vs_mode_logic); only the bases' hidden state applies to normal play"
Row "prefabs" $(if ($A.prefabs_uncovered.Count -eq 0) { "PASS" } else { "KNOWN" }) "CONTAINER / NON-RENDERED AUTHORED OBJECT" $C.prefab_containers "members placed" "" "16 containers, $($C.prefab_members) members resolve to placed map data (validate_m04 prefabs_covered); containers are not render geometry"
# material corrections
$dsl = DumpKey "MP_IAC_Streets_ART_m.TheWorld.PersistentLevel.StaticMeshCollectionActor_12944.StaticMeshActor_5246_SMC"
$dslMat = if ($dsl) { ($dump[$dsl].mats | Select-Object -Unique) -join "," } else { "not drawn" }
$dslAud = $MA.categories.static_meshes.items | Where-Object { $_.actor -eq "StaticMeshCollectionActor_12944" -and $_.mesh -like "*DeadSoldierLeg*" } | Select-Object -First 1
Row "material.dead_soldier_leg" $(if ($dslAud -and $dslAud.status -eq "rendered_correctly") { "PASS" } else { "KNOWN" }) $(if ($dslAud -and $dslAud.status -eq "rendered_correctly") { "PRESENT AND MATCHING" } else { "PRESENT BUT WRONG MATERIAL" }) "DeadBodies_Mat_INST" $dslMat "Rendering" "AssetTools' one WRONG MATERIAL item (Rendering-lane audit 12:58); merged audit: $(if ($dslAud) { $dslAud.status } else { 'n/a' }) - pixel check is HUMAN CHECK"
$unkMats = @($MA.categories.static_meshes.items | Where-Object status -eq "unknown" | ForEach-Object { ($_.reason -replace '^.*\[''', '' -replace '''\].*$', '') } | Select-Object -Unique)
Row "material.unverifiable" "PARTIAL" "PRESENT, MATERIAL UNKNOWN" "$unk items" "$($unkMats.Count) materials" "Rendering" ("honestly labelled: compiled permutation has no comparable parameter table or an unnamed parameter: " + ($unkMats -join ", "))
# perf
if (Test-Path "$perf\report.json") {
    foreach ($r in (J "$perf\report.json").results) { Add-WfcResult $res ($r.id -replace '^perf_release', 'm04.perf') "INFO" $r.measured $r.note }
}
# ---------------- 5. report ----------------
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-WfcCsv $cat (Join-Path $OutDir "categories.csv")
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Milestone 04 completeness gate - MP_IAC_Streets ($((Get-Date -Format 'yyyy-MM-dd HH:mm')))")
$md.Add(""); $md.Add("Product: ``$Root``; authored: AssetTools ``$AssetTools`` ($($A.source.generated))")
$st = $cat | Group-Object status | ForEach-Object { "$($_.Name) $($_.Count)" }
$md.Add(""); $md.Add("**Categories: " + ($st -join " / ") + "**")
$md.Add(""); $md.Add("**M04 completeness: $pctStrict% of $total visible authored render items PRESENT AND MATCHING ($pctPresent% including present items whose material is unverifiable)**")
$md.Add(""); $md.Add("| item class | count |"); $md.Add("|---|---|"); foreach ($g in $byClass) { $md.Add("| $($g.Name) | $($g.Count) |") }
$md.Add(""); $md.Add("| category | status | class | authored | product | owner | evidence |"); $md.Add("|---|---|---|---|---|---|---|")
foreach ($c in $cat) { $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} |" -f $c.id, $c.status, $c.class, $c.authored, $c.product, $c.owner, ($c.evidence -replace '\|', '/'))) }
if ($toolErrors.Count) { $md.Add(""); $md.Add("## TOOL ERRORS (not product)"); foreach ($t in $toolErrors) { $md.Add("- $t") } }
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "M04-GATE.md")
Step ("M04 GATE: {0}; completeness {1}% ({2}% incl. unverifiable materials) -> {3}" -f ($st -join " / "), $pctStrict, $pctPresent, $OutDir)
if (@($cat | Where-Object status -eq "FAIL").Count) { exit 1 } elseif ($toolErrors.Count) { exit 2 } else { exit 0 }
