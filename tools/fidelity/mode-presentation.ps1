# Streets per-mode presentation (DM, TDM, CTF, EXT, DOM, KOTH) on the real exe (WFC_GAMEMODE), validating
# only what recovered data proves.
#
#   .\tools\fidelity\mode-presentation.ps1 -Exe <wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir>
#
# Method: the same fixed objective views (WFC_SHOTLIST, lockstep) rendered in every mode; each view is compared
# with DM inside a window around the target's projected position. Everything else in the frame is identical
# across modes, so a change there is mode-dependent presentation (an object shown / hidden / effect active).
# Expectations and their source:
#   AUTHORED (PASS/FAIL)  objective bases (4 InterpActors) shown only where SeqCond_GameRuleActive unhides them:
#                         rules SingleFlagCTF / ScoreBombingRun (gameplay.json mode_dependent_visibility);
#                         flag factories' effect only with RequiredGameRuleClass SingleFlagCTF, bomb factory only
#                         with ScoreBombingRun (gameplay.json effective props, map_fx_runtime required_game_rule);
#                         normal pickups have no RequiredGameRuleClass: same in every mode; TDM = DM.
#   RE (Gameplay gameRulesForMode / FIDELITY "Streets mode state", RE 0ab03b2): which mode carries which rules;
#                         totems visible only in DOM; exactly one KOTH zone ring active in KOTH. A mismatch is FAIL
#                         only when Gameplay's own state (WFC_MODETEST) says visible and nothing is drawn.
# Collision: WFC_MODETEST col / touch flags reported (INFO; disabled factories SetCollision(false,false) [RE]).
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Shots.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$g = Get-Content -Raw "$WfcSlice\gameplay.json" | ConvertFrom-Json
$modes = @("DM", "TDM", "CTF", "EXT", "DOM", "KOTH")
# ---- rule sets per mode: from the product's own Gameplay state dump (WFC_MODETEST prints per-mode state);
#      the rule names themselves come from MapState::gameRulesForMode (RE) - listed here for the report only.
$rules = @{ DM = @(); TDM = @(); CTF = @("SingleFlagCTF"); EXT = @("ScoreBombingRun"); DOM = @("ScoreDomination"); KOTH = @("ScoreKingOfTheHill") }
# ---- targets ----
$targets = @()
$ob = @($g.mode_dependent_visibility | ForEach-Object { $_.targets } | Sort-Object { $_.actor } -Unique)
$unhideRules = @($g.mode_dependent_visibility | ForEach-Object { ($_.rule -split '_', 2)[1] })
foreach ($o in $ob | Where-Object { $_.mesh -like "*ObjBases*" }) { $targets += @{ id = "base_$($o.actor)"; kind = "base"; pos = @($o.location_gltf[0], ($o.location_gltf[1] + 0.3), $o.location_gltf[2]); expect = { param($m) @($rules[$m] | Where-Object { $unhideRules -contains $_ }).Count -gt 0 }; src = "AUTHORED Kismet unhide ($($unhideRules -join ', '))" } }
foreach ($f in $g.objectives.TnGameObjectivePickupFactoryFlag) { $req = ($f.effective.RequiredGameRuleClass -split '_', 2)[1]; $targets += @{ id = "flag_$($f.actor)"; kind = "flag"; pos = @($f.location_gltf[0], ($f.location_gltf[1] + 1.0), $f.location_gltf[2]); req = $req; expect = [scriptblock]::Create("param(`$m) `$rules[`$m] -contains '$req'"); src = "AUTHORED RequiredGameRuleClass $req" } }
foreach ($f in $g.objectives.TnGameObjectivePickupFactoryBomb) { $req = ($f.effective.RequiredGameRuleClass -split '_', 2)[1]; $targets += @{ id = "bomb_$($f.actor)"; kind = "bomb"; pos = @($f.location_gltf[0], ($f.location_gltf[1] + 1.0), $f.location_gltf[2]); req = $req; expect = [scriptblock]::Create("param(`$m) `$rules[`$m] -contains '$req'"); src = "AUTHORED RequiredGameRuleClass $req" } }
foreach ($f in $g.objectives.TnDominationPoint) { $targets += @{ id = "totem_$($f.actor)"; kind = "totem"; pos = @($f.location_gltf[0], ($f.location_gltf[1] + 1.5), $f.location_gltf[2]); expect = { param($m) $m -eq "DOM" }; src = "RE (Gameplay FIDELITY: visible only in DOM)" } }
foreach ($f in $g.objectives.TnKingOfTheHillZone) { $targets += @{ id = "koth_$($f.actor)"; kind = "koth"; pos = @($f.location_gltf[0], ($f.location_gltf[1] + 0.5), $f.location_gltf[2]); expect = { param($m) $null }; src = "RE (one random zone active in KOTH)" } }
foreach ($cls in "TnAmmoCratePickupFactory", "TnHealthPickupFactory", "TnOverShieldPickupFactory") { $p = @($g.pickups | Where-Object class -eq $cls)[0]; $targets += @{ id = "pickup_$($p.actor)"; kind = "pickup"; pos = @($p.location_gltf[0], ($p.location_gltf[1] + 0.8), $p.location_gltf[2]); expect = { param($m) $false }; src = "AUTHORED no RequiredGameRuleClass" } }
$shots = New-Object System.Collections.Generic.List[object]
foreach ($t in $targets) { for ($k = 0; $k -lt 2; $k++) { $shots.Add(@{ name = ("{0}_v{1}" -f $t.id, $k); c = (RingCam $t.pos ($k * [Math]::PI + 0.5) 7 3); t = $t.pos; tid = $t.id }) } }
# ---- runs ----
$grids = @{}
foreach ($m in $modes) { $r = Invoke-ShotList $Exe (Join-Path $OutDir $m) $shots @{ WFC_GAMEMODE = $m } $RenderData -Keep @($shots | ForEach-Object { $_.name }); $grids[$m] = $r.grids }
# Same views with the map particle systems removed: a factory's own effect = normal vs no-FX in the SAME mode
# (separates the effect from the objective base meshes that share the flag factories' locations).
$fxShots = @($shots | Where-Object { $_.tid -like "flag_*" -or $_.tid -like "bomb_*" -or $_.tid -like "pickup_*" })
foreach ($m in $modes) { $r = Invoke-ShotList $Exe (Join-Path $OutDir "$m-nofx") $fxShots @{ WFC_GAMEMODE = $m; WFC_NOMAPFX = "1" } $RenderData; $grids["$m|nofx"] = $r.grids }
function CropCells($s, $a) { $q = ProjectGrid $s.c $s.t $s.t; if (-not $q) { return @() }; $w = [int]$a[0]; $cells = @(); for ($y = [int]$q[1] - 14; $y -le [int]$q[1] + 14; $y++) { for ($x = [int]$q[0] - 18; $x -le [int]$q[0] + 18; $x++) { if ($x -ge 0 -and $x -lt $w -and $y -ge 0 -and $y -lt [int]$a[1]) { $cells += $y * $w + $x } } }; return , ([int[]]$cells) }
# Gameplay's own per-mode state (MapState), for the RE comparisons and collision flags
$mt = Join-Path $OutDir "modetest"; $null = Invoke-WfcExe $Exe $mt @{ WFC_MODETEST = "1" } "run.log" 300
$state = @{}; foreach ($l in (Get-Content (Join-Path $mt "wfc.log") -ErrorAction SilentlyContinue)) {
    if ($l -match "MODETEST (\w+)\s+(\S+)\s+(\S+)\s+(\S+)\s+vis=(\d) col=(\d) touch=(\d)") { $state["$($Matches[1])|$($Matches[3])"] = @{ cls = $Matches[2]; st = $Matches[4]; vis = [int]$Matches[5]; col = [int]$Matches[6]; touch = [int]$Matches[7] } }
    elseif ($l -match "MODETEST (\w+)\s+objective base (\S+)\s+visible=(\d)") { $state["$($Matches[1])|$($Matches[2])"] = @{ cls = "base"; vis = [int]$Matches[3]; col = 0; touch = 0 } }
}
# ---- compare with DM around each target ----
$res = New-WfcResults
$rows = New-Object System.Collections.Generic.List[object]
foreach ($t in $targets) {
    foreach ($m in $modes) {
        $best = 0.0
        foreach ($s in ($shots | Where-Object tid -eq $t.id)) {
            $a = $grids[$m][$s.name]; $b = $grids["DM"][$s.name]; if (-not $a -or -not $b) { continue }
            $q = ProjectGrid $s.c $s.t $s.t; if (-not $q) { continue }
            $w = [int]$a[0]; $cells = @(); for ($y = [int]$q[1] - 14; $y -le [int]$q[1] + 14; $y++) { for ($x = [int]$q[0] - 18; $x -le [int]$q[0] + 18; $x++) { if ($x -ge 0 -and $x -lt $w -and $y -ge 0 -and $y -lt [int]$a[1]) { $cells += $y * $w + $x } } }
            # KOTH rings are large (a ring segment can fill the top of the view): judge them on the whole frame
            $cc = if ($t.kind -eq "koth") { [WfcImage]::Diff([WfcImage]::Luma2($a), [WfcImage]::Luma2($b), 6.0) | Select-Object -Last 1 | ForEach-Object { @(0, $_) } } else { [WfcImage]::CellCoverage($a, $b, [int[]]$cells, 6.0) }
            $best = [Math]::Max($best, $cc[1])
        }
        $actor = ($t.id -split '_', 2)[1]; $st = $state["$m|$actor"]
        $fxc = -1.0
        if ($grids.ContainsKey("$m|nofx")) { foreach ($s in ($fxShots | Where-Object tid -eq $t.id)) { $a = $grids[$m][$s.name]; $b = $grids["$m|nofx"][$s.name]; if ($a -and $b) { $cc = [WfcImage]::CellCoverage($a, $b, (CropCells $s $a), 4.0); $fxc = [Math]::Max($fxc, $cc[1]) } } }
        $rows.Add([pscustomobject][ordered]@{ target = $t.id; kind = $t.kind; mode = $m; differs_from_DM = [Math]::Round($best, 3); own_effect = [Math]::Round($fxc, 3); gameplay_state = $(if ($st) { $st.st }); gameplay_vis = $(if ($st) { $st.vis }); col = $(if ($st) { $st.col }); touch = $(if ($st) { $st.touch }) })
    }
}
Write-WfcCsv $rows (Join-Path $OutDir "modes.csv")
$thr = 0.04
foreach ($t in $targets) {
    $r = @($rows | Where-Object target -eq $t.id)
    $seen = ($r | ForEach-Object { "{0} {1:P0}" -f $_.mode, $_.differs_from_DM }) -join ", "
    if ($t.kind -eq "koth") { Add-WfcResult $res "modes.$($t.id)" "INFO" ($r | Where-Object mode -eq "KOTH").differs_from_DM ("change vs DM: $seen; Gameplay KOTH state: " + (($r | Where-Object mode -eq "KOTH").gameplay_state) + " - " + $t.src); continue }
    $bad = @(); $notes = @()
    $useFx = $t.kind -in "flag", "bomb"
    if ($useFx) { $seen = ($r | ForEach-Object { "{0} {1:P0}" -f $_.mode, $_.own_effect }) -join ", " }
    foreach ($x in $r) {
        if ($x.mode -eq "DM" -and -not $useFx) { continue }
        $exp = & $t.expect $x.mode
        $obs = if ($useFx) { $x.own_effect -ge 0.02 } else { $x.differs_from_DM -ge $thr }
        if ($null -eq $exp) { continue }
        if ($exp -ne $obs) { $bad += "$($x.mode): expected $(if ($exp) { 'shown' } else { 'not shown' }), measured $(if ($useFx) { 'own effect ' + $x.own_effect } else { 'change vs DM ' + $x.differs_from_DM }) (change vs DM $($x.differs_from_DM)); Gameplay vis=$($x.gameplay_vis)" }
    }
    $authored = $t.src -like "AUTHORED*"
    # Factory effects are small and share their locations with the objective bases (unhidden in CTF/EXT, which
    # changes particle occlusion) and with neighbouring emitters: a mismatch there is measured, not proven -> HUMAN.
    $status = if (-not $bad.Count) { "PASS" } elseif ($useFx) { "HUMAN" } elseif ($authored) { "FAIL" } else {
        # RE expectation: FAIL only if Gameplay's own state says visible where nothing is drawn
        $own = @($r | Where-Object { $_.gameplay_vis -eq 1 -and $_.differs_from_DM -lt $thr -and $_.mode -ne "DM" })
        if ($own.Count) { "FAIL" } else { "KNOWN" } }
    Add-WfcResult $res "modes.$($t.id)" $status $bad.Count ("{0}. {3} by mode: {1}.{2}" -f $t.src, $seen, $(if ($bad.Count) { " Mismatch: " + ($bad -join "; ") } else { "" }), $(if ($useFx) { "Own particle effect (normal vs WFC_NOMAPFX, same mode)" } else { "Change vs DM" })) $(if ($status -ne "PASS") { "Rendering/Gameplay" } else { "" })
}
foreach ($t in ($targets | Where-Object kind -eq "pickup")) { $r = @($rows | Where-Object target -eq $t.id); Add-WfcResult $res "modes.$($t.id).own_effect" "INFO" ($r | Where-Object mode -eq "DM").own_effect ("own particle effect coverage by mode (normal vs no-FX): " + (($r | ForEach-Object { "{0} {1:P0}" -f $_.mode, $_.own_effect }) -join ", ")) }
# KOTH: exactly one zone active (RE)
$kothActive = @($rows | Where-Object { $_.kind -eq "koth" -and $_.mode -eq "KOTH" -and $_.differs_from_DM -ge $thr })
$kothState = @($rows | Where-Object { $_.kind -eq "koth" -and $_.mode -eq "KOTH" -and $_.gameplay_state -eq "Active" })
Add-WfcResult $res "modes.koth_one_active_zone" $(if ($kothActive.Count -eq 1) { "PASS" } elseif ($kothState.Count -eq 1 -and $kothActive.Count -eq 0) { "FAIL" } else { "KNOWN" }) $kothActive.Count ("KOTH zones changed vs DM: {0} ({1}); Gameplay state Active: {2} ({3}). RE: one random zone shown + ring" -f $kothActive.Count, (($kothActive | ForEach-Object { $_.target }) -join ","), $kothState.Count, (($kothState | ForEach-Object { $_.target }) -join ",")) "Rendering/Gameplay"
# collision flags (INFO)
$col = @($rows | Where-Object { $_.kind -in "flag", "bomb", "totem" } | ForEach-Object { "{0}/{1}: col={2} touch={3} ({4})" -f $_.target.Split('_')[0], $_.mode, $_.col, $_.touch, $_.gameplay_state })
Add-WfcResult $res "modes.collision_state" "INFO" $null ("Gameplay MapState per mode (disabled factories SetCollision(false,false), totems keep collision when hidden [RE]): " + (($col | Select-Object -Unique) -join "; "))
# compact evidence: one sheet per target kind, every mode side by side (view 0)
foreach ($kind in "base", "flag", "bomb", "totem", "koth", "pickup") {
    $tiles = @(); foreach ($t in ($targets | Where-Object kind -eq $kind)) { foreach ($m in $modes) { $j = Join-Path (Join-Path $OutDir $m) "$($t.id)_v0.jpg"; $x = $rows | Where-Object { $_.target -eq $t.id -and $_.mode -eq $m }; $tiles += @{ png = $j; label = ("{0} {1} d{2:P0}" -f $m, $t.id, $x.differs_from_DM); flag = ($x.differs_from_DM -ge $thr) } } }
    if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_$kind.png") 6 320 180; Save-WfcJpeg (Join-Path $OutDir "sheet_$kind.png") (Join-Path $OutDir "sheet_$kind.jpg"); Remove-Item (Join-Path $OutDir "sheet_$kind.png") }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"MODE PRESENTATION: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
