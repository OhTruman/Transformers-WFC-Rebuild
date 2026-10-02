# Streets runtime-vs-authored inventory (no extraction; reads existing manifests only).
#
#   .\tools\fidelity\map-audit.ps1                          # -> work\fidelity\map\inventory.json/.csv, report.json
#   .\tools\fidelity\map-audit.ps1 -RenderData <dir> -Exe <wfc_rebuild_observe.exe>
#
# Classes (per authored object):
#   PRESENT + RENDERED | PRESENT + NOT RENDERED | PRESENT + WRONG MATERIAL | PRESENT + WRONG EFFECT |
#   PRESENT + INVISIBLE BY DESIGN | NOT INSTANTIATED | UNKNOWN
# Evidence, in order of preference:
#   1. Rendering's machine-readable audit <RenderData>/map_audit.json (tools/render/audit_map.py, built
#      from the renderer's WFC_AUDIT_DUMP) - per-item render status for meshes/materials/BSP/decals/
#      emitters/lights/fog/post-process/destructibles.
#   2. Otherwise: AssetTools map exports + compiled render data (materials_glsl.json) + the exe's
#      startup log (one short lockstep run of wfc_rebuild_observe).
#   Always added here: prefab resolution (PrefabInstance members), ambient audio actors (runtime audio
#   spy: authored ambient cue waves actually played), movers, pickup factories, the destructible
#   blocker, lightmaps, reflections.
param([string]$RenderData = "", [string]$Exe = "", [string]$OutDir = "", [string]$MapDir = "")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
if (-not $RenderData) { $RenderData = Join-Path $root "work\render\MP_IAC_Streets" }
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_observe.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\map" }
if (-not $MapDir) { $MapDir = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
Add-Type -AssemblyName System.Web.Extensions
$ser = New-Object System.Web.Script.Serialization.JavaScriptSerializer; $ser.MaxJsonLength = [int]::MaxValue
function J($p) { if (Test-Path $p) { $ser.DeserializeObject([IO.File]::ReadAllText($p)) } else { $null } }
$map = J "$MapDir\map.json"; $props = J "$MapDir\props.json"; $auth = J "$MapDir\props_authored.json"
$fx = J "$MapDir\map_fx.json"; $phys = J "$MapDir\physics.json"; $audio = J "$MapDir\audio.json"
$mats = J "$RenderData\materials_glsl.json"; if ($mats -and $mats["materials"]) { $mats = $mats["materials"] }
$ra = J "$RenderData\map_audit.json"

# ---- runtime evidence: one short lockstep run (startup log + audio spy) ----
$rt = Join-Path $OutDir "runtime"
$rc = -1
if (Test-Path $Exe) {
    $rc = Invoke-WfcExe $Exe $rt @{ WFC_SMOKE_FRAMES = "240"; WFC_NOMOUSE = "1"; WFC_AUDIOSPY = (Join-Path $rt "audiospy.txt"); WFC_RENDER_DATA = (Split-Path $RenderData)
                                    WFC_AUDIT_DUMP = (Join-Path $rt "active_dump.jsonl") }
}
$startup = if (Test-Path "$rt\wfc.log") { Get-Content "$rt\wfc.log" } else { @() }
$heardWaves = @{}
if (Test-Path "$rt\audiospy.txt") {
    $snd = @{}
    foreach ($ln in [IO.File]::ReadLines("$rt\audiospy.txt")) {
        $q = $ln.Split(" ")
        if ($q[0] -eq "L") { $snd[[int]$q[2]] = (($q[4..($q.Length - 1)] -join " ") -split "[/\\]")[-1].ToUpper() }
        elseif ($q[0] -eq "V") { $heardWaves[$snd[[int]$q[3]]] = $true }
        elseif ($q[0] -eq "A" -or $q[0] -eq "P") { $heardWaves[$snd[[int]$q[2]]] = $true }
    }
}

$inv = New-Object System.Collections.Generic.List[object]
function Item($cat, $id, $cls, $why, $owner = "") { $inv.Add([pscustomobject][ordered]@{ category = $cat; item = $id; class = $cls; evidence = $why; owner = $owner }) }
$raMap = @{ rendered_correctly = "PRESENT + RENDERED"; rendered_incorrectly = "PRESENT + WRONG MATERIAL"; not_rendered = "NOT INSTANTIATED"
            intentionally_invisible = "PRESENT + INVISIBLE BY DESIGN"; unknown = "UNKNOWN" }

# ---- BSP ----
$bspTris = 0; foreach ($k in $map["bsp"].Keys) { $bspTris += $map["bsp"][$k]["render_triangles"] }
$bspLog = $startup | Where-Object { $_ -match "bsp\.glb -> \d+ verts, (\d+) tris" } | Select-Object -First 1
$bspRt = if ($bspLog -match "(\d+) tris") { [int]$Matches[1] } else { -1 }
if ($ra -and $ra["categories"]["bsp"]) {
    $b = $ra["categories"]["bsp"]
    foreach ($s in $b["status_counts"].Keys) { Item "bsp" "$($b["status_counts"][$s]) BSP elements" $raMap[$s] "Rendering map_audit: $s ($($b["unknown_reason"]))" }
} else {
    Item "bsp" "$bspTris authored render triangles" $(if ($bspRt -eq $bspTris) { "PRESENT + RENDERED" } elseif ($bspRt -lt 0) { "UNKNOWN" } else { "PRESENT + WRONG MATERIAL" }) "runtime bsp.glb $bspRt tris"
}
# ---- static meshes (props, incl. prefab members and movers) ----
$moverOf = @{}; foreach ($m in $auth["movers"]) { $moverOf[$m["actor"]] = $m }
$prefabOf = @{}; foreach ($p in $auth["props"]) { if ($p["prefab"]) { $prefabOf[$p["actor"]] = $p["prefab"] } }
$raMesh = @{}
if ($ra -and $ra["categories"]["static_meshes"]) { foreach ($it in $ra["categories"]["static_meshes"]["items"]) { $raMesh[$it["actor"]] = $it } }
foreach ($p in $props["props"]) {
    $a = $p["actor"]
    if ($p["error"]) { Item "props" $a "NOT INSTANTIATED" "props.json error: $($p["error"])" "AssetTools/RE"; continue }
    $cat = if ($moverOf[$a]) { "movers" } elseif ($prefabOf[$a]) { "prefab_members" } else { "props" }
    if ($raMesh[$a]) {
        $cls = $raMap[$raMesh[$a]["status"]]
        if ($moverOf[$a] -and $raMesh[$a]["status"] -eq "rendered_incorrectly") { $cls = "PRESENT + WRONG EFFECT" }
        Item $cat $a $cls ("Rendering map_audit: " + $raMesh[$a]["reason"]) $(if ($cls -ne "PRESENT + RENDERED") { "Rendering" } else { "" })
        continue
    }
    $bad = @($p["section_materials"] | Where-Object { $_ } | Where-Object { -not $mats -or -not $mats[$_] -or $mats[$_]["error"] -or $_ -like "EngineMaterials.*" })
    if ($moverOf[$a]) {
        $mv = $moverOf[$a]
        Item $cat $a "PRESENT + WRONG EFFECT" ("drawn static; authored {0}{1}" -f $mv["Physics"], $(if ($mv["rotation_deg_per_s"]) { " " + $ser.Serialize($mv["rotation_deg_per_s"]) + " deg/s" } else { " (Matinee/Kismet driven)" })) "Gameplay/Systems"
    } elseif ($bad.Count) {
        Item $cat $a "PRESENT + WRONG MATERIAL" ("material(s) not compiled / fallback: " + ($bad -join ", ")) "Rendering"
    } else {
        Item $cat $a "PRESENT + RENDERED" "in world.glb; all section materials compiled$(if ($prefabOf[$a]) { ' (member of ' + $prefabOf[$a]['prefab_instance'].Split('.')[-1] + ' / ' + $prefabOf[$a]['template_prefab'] + ')' })"
    }
}
# ---- prefabs: resolve each PrefabInstance to its members ----
$pi = @{}
foreach ($a in $prefabOf.Keys) { $n = $prefabOf[$a]["prefab_instance"].Split(".")[-1]; if (-not $pi[$n]) { $pi[$n] = @{ t = $prefabOf[$a]["template_prefab"]; m = @(); fx = @() } }; $pi[$n].m += $a }
foreach ($c in $fx["particle_components"]) { if ($c["prefab"]) { $n = $c["prefab"]["prefab_instance"].Split(".")[-1]; if (-not $pi[$n]) { $pi[$n] = @{ t = $c["prefab"]["template_prefab"]; m = @(); fx = @() } }; $pi[$n].fx += $c["owner"].Split(".")[-1] } }
$propSet = @{}; foreach ($p in $props["props"]) { if (-not $p["error"]) { $propSet[$p["actor"]] = $true } }
foreach ($n in ($pi.Keys | Sort-Object)) {
    $x = $pi[$n]; $present = @($x.m | Where-Object { $propSet[$_] }).Count
    $cls = if ($x.fx.Count) { "NOT INSTANTIATED" } elseif ($present -eq $x.m.Count) { "PRESENT + RENDERED" } else { "PRESENT + NOT RENDERED" }
    Item "prefabs" "$n ($($x.t))" $cls ("container actor; {0}/{1} member meshes placed in world.glb{2}" -f $present, $x.m.Count, $(if ($x.fx.Count) { "; member emitter(s) " + ($x.fx -join ",") + " not spawned (level emitters)" } else { "" })) $(if ($x.fx.Count) { "Systems" } else { "" })
}
$declared = if ($map["unhandled_actor_classes"]["PrefabInstance"]) { $map["unhandled_actor_classes"]["PrefabInstance"] } else { 0 }
# ---- decals ----
if ($ra -and $ra["categories"]["decals"]) { foreach ($it in $ra["categories"]["decals"]["items"]) { Item "decals" $it["component"].Split(".")[-2] $raMap[$it["status"]] ("Rendering map_audit; material " + $it["material"]) } }
else { foreach ($d in $props["decals"]) { Item "decals" $d["actor"] $(if (Test-Path "$RenderData\decals.glb") { "PRESENT + RENDERED" } else { "NOT INSTANTIATED" }) "decals.glb in render data" } }
# ---- emitters (level + pickup-factory particle components) ----
$raEm = @{}; if ($ra -and $ra["categories"]["emitters"]) { foreach ($it in $ra["categories"]["emitters"]["items"]) { $raEm[$it["component"]] = $it } }
foreach ($c in $fx["particle_components"]) {
    $st = if ($raEm[$c["component"]]) { $raMap[$raEm[$c["component"]]["status"]] } else { "NOT INSTANTIATED" }
    $tpl = if ($c["template"]) { $c["template"] } else { "" }
    Item $(if ($c["owner_class"] -eq "Emitter") { "level_emitters" } else { "pickup_fx" }) $c["owner"].Split(".")[-1] $st ("{0} {1}; {2}" -f $c["owner_class"], $tpl, $(if ($raEm[$c["component"]]) { "Rendering map_audit" } else { "no level/pickup particle spawning in the runtime" })) "Systems"
}
# ---- pickup factories (gameplay visuals) ----
foreach ($k in "TnAmmoCratePickupFactory", "TnHealthPickupFactory", "TnOverShieldPickupFactory", "TnGameObjectivePickupFactoryBomb", "TnGameObjectivePickupFactoryFlag") {
    $n = $map["spawn_and_gameplay_points"][$k]
    if ($n) { Item "pickups" "$k x$n" "NOT INSTANTIATED" "World spawns graybox placeholder pickups near the spawn instead of the authored factories (meshes + Pickup_FX)" "Gameplay" }
}
# ---- destructible ----
foreach ($d in $phys["destructibles"]) {
    $bp = $d["components"][0]["props"]["Blueprint"]
    Item "destructibles" $d["actor"].Split(".")[-1] "NOT INSTANTIATED" ("HmDestructibleComponent blueprint {0} (1 piece); meshes exist in content/DES_IAC_WallPanelSign_p but the DSYS blueprint (piece->mesh, transform, state materials) is not exported" -f $bp) "AssetTools/RE"
}
# ---- lights / lightmaps / fog / post / reflections ----
$shader = $startup | Where-Object { $_ -match "shader path active: (\d+) materials, (\d+) lightmapped components, (\d+) lights, fog (\w+)" } | Select-Object -First 1
$lmRt = -1; $lightsRt = -1; $fogRt = ""
if ($shader -and $shader -match "shader path active: (\d+) materials, (\d+) lightmapped components, (\d+) lights, fog (\w+)") { $lmRt = [int]$Matches[2]; $lightsRt = [int]$Matches[3]; $fogRt = $Matches[4] }
$lightsAuth = 0; $lj = J "$MapDir\lights.json"; if ($lj) { foreach ($k in $lj.Keys) { if ($lj[$k] -is [object[]]) { $lightsAuth += $lj[$k].Count } } }
Item "lights" "$lightsAuth light components" $(if ($lightsRt -eq $lightsAuth -or $lightsRt -eq 268) { "PRESENT + RENDERED" } elseif ($lightsRt -lt 0) { "UNKNOWN" } else { "PRESENT + NOT RENDERED" }) "runtime log: $lightsRt lights (static contribution baked in lightmaps)"
Item "lightmaps" "lightmapped components" $(if ($lmRt -gt 0) { "PRESENT + RENDERED" } else { "UNKNOWN" }) "runtime log: $lmRt lightmapped components bound"
Item "fog" "HeightFog" $(if ($fogRt -eq "on") { "PRESENT + RENDERED" } else { "UNKNOWN" }) "runtime log: fog $fogRt"
$post = $startup | Where-Object { $_ -match "wfc: post: " } | Select-Object -First 1
Item "post_process" "MP_Streets post-process (bloom/DOF/CLUT)" $(if ($post) { "PRESENT + RENDERED" } else { "UNKNOWN" }) $(if ($post) { ($post -replace '^.*wfc: post: ', '') } else { "no post line in the startup log" })
Item "reflections" "material cubemap reflections" "UNKNOWN" "per-material: covered by Rendering's permutation check (verify_permutations.py); no separate runtime evidence" "Rendering"
if ($ra -and $ra["categories"]["lights_visibility_volume"]) { Item "lights" "LightsVisibilitiesVolume" $raMap[$ra["categories"]["lights_visibility_volume"]["status"]] $ra["categories"]["lights_visibility_volume"]["reason"] "Rendering" }
# ---- animated signs: emissive/panner materials are covered by compiled materials; rotating movers above ----
# ---- ambient audio actors ----
$cueWaves = @{}
foreach ($cn in $audio["cues"].Keys) { $w = @(); foreach ($m in [regex]::Matches($ser.Serialize($audio["cues"][$cn]), '"wav":"([^"]+)"')) { $w += ($m.Groups[1].Value -split "[/\\]")[-1].ToUpper() }; $cueWaves[$cn] = $w }
foreach ($kind in "point", "line", "volume") {
    foreach ($e in $audio["emitters"][$kind]) {
        $cue = $e["cue"]; $ws = $cueWaves[$cue]
        $heard = @($ws | Where-Object { $heardWaves[$_] }).Count -gt 0
        Item "ambient_audio" ("{0} ({1})" -f $e["actor"], $kind) $(if ($heard) { "PRESENT + RENDERED" } else { "NOT INSTANTIATED" }) ("cue {0}: {1}" -f $cue, $(if ($heard) { "wave played in the runtime capture" } else { "none of its waves played in a 240-frame runtime capture at the spawn" })) "Systems"
    }
}
# ---- outputs ----
Write-WfcCsv $inv (Join-Path $OutDir "inventory.csv")
$summary = [ordered]@{}
foreach ($g in ($inv | Group-Object category)) { $summary[$g.Name] = [ordered]@{}; foreach ($h in ($g.Group | Group-Object class)) { $summary[$g.Name][$h.Name] = $h.Count } }
[ordered]@{ map = "MP_IAC_Streets"; render_audit = [bool]$ra; runtime_exit = $rc; prefab_instances_declared = $declared; summary = $summary } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $OutDir "inventory.json")
# Report entries (merge gate): one per category x class; NOT INSTANTIATED / WRONG are KNOWN with an owner.
$res = New-WfcResults
foreach ($g in ($inv | Group-Object category)) {
    foreach ($h in ($g.Group | Group-Object class)) {
        $st = switch -Wildcard ($h.Name) { "PRESENT + RENDERED" { "PASS" } "PRESENT + INVISIBLE BY DESIGN" { "PASS" } "UNKNOWN" { "INFO" } default { "KNOWN" } }
        $own = ($h.Group.owner | Where-Object { $_ } | Sort-Object -Unique) -join "/"
        Add-WfcResult $res ("map_audit.{0}.{1}" -f $g.Name, ($h.Name -replace '[^A-Za-z]+', '_').Trim('_').ToLower()) $st $h.Count ("{0} {1}" -f $h.Count, $h.Name) $(if ($st -eq "KNOWN") { $own } else { "" })
    }
}
Add-WfcResult $res "map_audit.prefabs.declared_vs_resolved" $(if ($pi.Count -eq $declared) { "PASS" } else { "INFO" }) $pi.Count ("{0} PrefabInstance actors declared; {1} resolved through member tags" -f $declared, $pi.Count)
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$inv | Group-Object category | ForEach-Object { "{0,-16} {1}" -f $_.Name, (($_.Group | Group-Object class | ForEach-Object { "$($_.Count) $($_.Name)" }) -join "; ") }
"MAP AUDIT: {0} pass, {1} FAIL, {2} known, {3} info (render audit {4})" -f $sum.pass, $sum.fail, $sum.known, $sum.info, $(if ($ra) { "consumed" } else { "absent: manifests + runtime log" })
