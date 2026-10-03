# Milestone 04 ambient audio on the real exe (lockstep; WFC_AMBLOG every 0.5 s).
#
#   .\tools\fidelity\m04-ambient.ps1 -Exe <wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir>
#
# 1. Zone at every FFA start: the exe spawns at FFA start k (WFC_SPAWN_INDEX); its logged spawn position is
#    matched to the authored start (gameplay.json), and the first zone the product enters is compared with
#    audio.json player_start_zones (authored trigger-volume containment). Mismatch = FAIL (the product
#    implements zones; this contradicts authored data). Authored "no zone" starts expect no zone entry.
# 2. Traversal routes (vehicle boost + slow turn from several starts): distinct zones / reverb presets
#    entered, max active emitters, one-shots played, level-FX particles. INFO against the authored counts.
# Requires a product with the ambient runtime ("ambient: N map cues, M emitters, Z zones"); SKIP otherwise.
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir,
      [int]$IdleFrames = 90, [int[]]$Routes = @(0, 6, 12, 18), [int]$RouteFrames = 1800)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$slice = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets"
$au = Get-Content -Raw "$slice\audio.json" | ConvertFrom-Json
$gp = Get-Content -Raw "$slice\gameplay.json" | ConvertFrom-Json
$zoneOf = @{}; foreach ($s in $au.player_start_zones.starts_detail) { $zoneOf[$s.actor] = @($s.zones) }
$ffa = @($gp.player_starts | Where-Object { $_.class -eq "TnFreeForAllPlayerStart" })
$res = New-WfcResults
function Run($name, $envs, $frames) {
    $dir = Join-Path $OutDir $name
    $e = @{ WFC_SMOKE_FRAMES = "$frames"; WFC_LOCKSTEP = "1"; WFC_AMBLOG = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0" } + $envs
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    $rc = Invoke-WfcExe $Exe $dir $e "run.log" 900
    return @{ rc = $rc; lines = @(Get-Content (Join-Path $dir "wfc.log") -ErrorAction SilentlyContinue) }
}
function Parse($lines) {
    $o = @{ zones = @(); presets = @(); maxActive = 0; total = -1; oneShots = 0; levelfx = 0; spawn = $null; inst = $null }
    foreach ($l in $lines) {
        if ($l -match "World: spawn at (-?[\d.]+), (-?[\d.]+), (-?[\d.]+)") { $o.spawn = @([double]$Matches[1], [double]$Matches[2], [double]$Matches[3]) }
        elseif ($l -match "ambient: (\d+) map cues, (\d+) emitters, (\d+) zones") { $o.inst = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3]) }
        elseif ($l -match "ambient: entered zone (\S+) \(from [^;]*; preset (\S+?),") { $o.zones += $Matches[1]; $o.presets += $Matches[2] }
        elseif ($l -match "AMB zone=\S* emitters=(\d+)/(\d+) oneShots=(\d+) .* levelfx=(\d+)") {
            $o.maxActive = [Math]::Max($o.maxActive, [int]$Matches[1]); $o.total = [int]$Matches[2]; $o.oneShots = [Math]::Max($o.oneShots, [int]$Matches[3]); $o.levelfx = [Math]::Max($o.levelfx, [int]$Matches[4])
        }
    }
    return $o
}
# ---- 1. zone at each FFA start ----
$rows = @(); $inst = $null
for ($k = 0; $k -lt $ffa.Count; $k++) {
    $r = Run "start_$k" @{ WFC_SPAWN_INDEX = "$k" } $IdleFrames
    $p = Parse $r.lines
    if ($p.inst) { $inst = $p.inst }
    if (-not $p.inst) { Add-WfcResult $res "m04_ambient.start_$k" "SKIP" $null "no ambient runtime line (exit $($r.rc))"; continue }
    # nearest authored FFA start to the logged spawn (the spawn is lifted onto the floor: compare XZ)
    $best = $null; $bd = 1e9
    foreach ($s in $ffa) { $L = $s.location_gltf; $d = [Math]::Sqrt([Math]::Pow($L[0] - $p.spawn[0], 2) + [Math]::Pow($L[2] - $p.spawn[2], 2)); if ($d -lt $bd) { $bd = $d; $best = $s } }
    $want = @($zoneOf[$best.actor]); $got = if ($p.zones.Count) { $p.zones[0] } else { "NONE" }
    $ok = if ($want.Count) { $want -contains $got } else { $got -eq "NONE" }
    $rows += [pscustomobject][ordered]@{ index = $k; start = $best.actor; match_m = [Math]::Round($bd, 2); authored_zone = $(if ($want.Count) { $want -join "|" } else { "NONE" }); product_zone = $got; preset = $(if ($p.presets.Count) { $p.presets[0] } else { "" }); ok = $ok }
    Add-WfcResult $res "m04_ambient.zone_at_start.$($best.actor)" $(if ($ok) { "PASS" } else { "FAIL" }) $null ("FFA {0}: product zone {1}, authored {2} (start matched within {3:F2} m)" -f $k, $got, $(if ($want.Count) { $want -join "|" } else { "NONE" }), $bd)
}
Write-WfcCsv $rows (Join-Path $OutDir "zones_at_starts.csv")
if ($inst) {
    Add-WfcResult $res "m04_ambient.emitters_instantiated" $(if ($inst[1] -eq 70) { "PASS" } else { "FAIL" }) $inst[1] "ambient emitters created by the runtime (authored 40 point + 13 line + 17 volume = 70)" "" 70
    Add-WfcResult $res "m04_ambient.zones_instantiated" $(if ($inst[2] -eq 9) { "PASS" } else { "FAIL" }) $inst[2] "audio zones (authored 9 SeqEvent_Touch zones)" "" 9
    Add-WfcResult $res "m04_ambient.map_cues" "INFO" $inst[0] "map ambient cues loaded (authored 32)" "" 32
}
# ---- 2. traversal routes ----
$allZones = @{}; $allPresets = @{}; $maxActive = 0; $shots = 0; $lfx = 0
foreach ($k in $Routes) {
    $r = Run "route_$k" @{ WFC_SPAWN_INDEX = "$k"; WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.25" } $RouteFrames
    $p = Parse $r.lines
    foreach ($z in $p.zones) { $allZones[$z] = $true }; foreach ($z in $p.presets) { $allPresets[$z] = $true }
    $maxActive = [Math]::Max($maxActive, $p.maxActive); $shots += $p.oneShots; $lfx = [Math]::Max($lfx, $p.levelfx)
    Add-WfcResult $res "m04_ambient.route_$k" "INFO" $p.zones.Count ("route from FFA {0} ({1:F0} s vehicle boost): zones entered {2}; max active emitters {3}/{4}; one-shots {5}; level-FX particles max {6}" -f $k, ($RouteFrames / 60.0), (($p.zones | Select-Object -Unique) -join " > "), $p.maxActive, $p.total, $p.oneShots, $p.levelfx)
}
foreach ($rr in $rows) { $allZones[$rr.product_zone] = $true; if ($rr.preset) { $allPresets[$rr.preset] = $true } }
$allZones.Remove("NONE")
Add-WfcResult $res "m04_ambient.zones_reached" "INFO" $allZones.Count ("distinct zones entered over the starts + routes (authored 9): " + (($allZones.Keys | Sort-Object) -join ", ")) "" 9
Add-WfcResult $res "m04_ambient.reverb_presets_reached" "INFO" $allPresets.Count ("distinct reverb presets applied (authored 10 incl. default): " + (($allPresets.Keys | Sort-Object) -join ", ")) "" 10
Add-WfcResult $res "m04_ambient.max_active_emitters" "INFO" $maxActive "most emitters audible at once on the routes (distance-limited by design; 70 instantiated)"
Add-WfcResult $res "m04_ambient.one_shots_played" $(if ($shots -gt 0) { "PASS" } else { "KNOWN" }) $shots "positional one-shots from the 11 authored timed pools, summed over the routes" $(if ($shots -gt 0) { "" } else { "Systems" })
Add-WfcResult $res "m04_ambient.level_fx_particles_max" "INFO" $lfx "level steam emitter particles alive (Systems LevelFx)"
$null = Write-WfcReport $res (Join-Path $OutDir "report.json")
"M04 AMBIENT: {0}/{1} starts in the authored zone; zones {2}; presets {3}; max active {4}; one-shots {5}" -f @($rows | Where-Object ok).Count, $rows.Count, $allZones.Count, $allPresets.Count, $maxActive, $shots
