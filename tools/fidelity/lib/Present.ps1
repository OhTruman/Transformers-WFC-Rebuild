# Presentation-health helpers (catastrophic-failure detection on product frames, GFx dumps and pawn motion).
# Dot-source after lib\Run.ps1. Thresholds are calibrated on known-good frames (M04 / M05 Streets, M06 Seed) and on the
# human-reported broken M06 frames; see results\m06-presentation\CALIBRATION.md.
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "Presentation.cs") -ErrorAction SilentlyContinue

# Regions (fractions of the frame). Gameplay "world" = left / right thirds and the upper centre below the HUD band:
# the HUD (top 20 %), the player character (lower centre) and the bottom HUD strip are excluded, so HUD or Optimus
# alone can never make a gameplay frame pass.
$script:PresentRegions = @{
    world_left = @(0.0, 0.20, 0.33, 0.88); world_right = @(0.67, 0.20, 1.0, 0.88); world_upper = @(0.33, 0.20, 0.67, 0.50)
    title_scene = @(0.0, 0.08, 0.55, 0.95)          # PC main menu: the 3D scene left of the menu panel
    lobby_scene = @(0.62, 0.15, 1.0, 0.85)          # party / game lobby: the scene right of the roster panel
    full = @(0.0, 0.0, 1.0, 1.0); menu_panel_left = @(0.03, 0.55, 0.32, 0.95); pause_panel = @(0.78, 0.50, 0.98, 0.95)
}
$script:PresentThr = @{ world_detail_pass = 0.15; world_detail_fail = 0.10; world_black_fail = 0.60; world_maxflat_fail = 0.60
                        scene_detail_pass = 0.12; scene_maxflat_fail = 0.45; scene_black_fail = 0.70; ui_detail_fail = 0.04; ui_maxflat_fail = 0.80
                        untex_max_fail = 0.12; untex_frac_fail = 0.40; noise_fail = 0.15 }
# Malformed-geometry signature (untextured / placeholder surfaces, noise textures): calibrated on the Frontend lane head
# 9e67bf8 title sphere (0.23 / 0.43 in the scene region) and lobby slab (0.30 / 0.55), return noise plates (0.43) vs good
# frames (largest <= 0.075, total <= 0.29 incl. close-up smooth metal in M05 Streets, noise <= 0.034).
function Present-Malformed($m) { $t = $script:PresentThr; return ($m.untexMax -ge $t.untex_max_fail -or $m.untexFrac -ge $t.untex_frac_fail -or $m.noise -ge $t.noise_fail) }

function Present-Measure([string]$Img, [double[]]$R) {
    if (-not (Test-Path $Img)) { return $null }
    $m = [WfcPresent]::Measure((Resolve-Path $Img).Path, $R[0], $R[1], $R[2], $R[3]); $q = [WfcPresent]::Malformed((Resolve-Path $Img).Path, $R[0], $R[1], $R[2], $R[3])
    return [pscustomobject]@{ untexMax = [Math]::Round($q[0], 3); untexFrac = [Math]::Round($q[1], 3); noise = [Math]::Round($q[2], 3); black = [Math]::Round($m[0], 3); detail = [Math]::Round($m[1], 3); flat = [Math]::Round($m[2], 3); maxFlat = [Math]::Round($m[3], 3); streak = [Math]::Round($m[4], 3); edges = [Math]::Round($m[5], 4); mean = [Math]::Round($m[6], 1); sat = [Math]::Round($m[7], 3) }
}
# World coverage of a gameplay frame: the three world regions, worst-of for black / maxFlat, mean detail.
function Present-World([string]$Img) {
    $rs = @("world_left", "world_right", "world_upper" | ForEach-Object { Present-Measure $Img $script:PresentRegions[$_] }) | Where-Object { $_ }
    if (-not $rs.Count) { return $null }
    $u = Present-Measure $Img @(0.0, 0.20, 1.0, 0.88)   # one blank area is judged over the whole world region (a near wall filling one third is normal)
    return [pscustomobject]@{ detail = [Math]::Round((($rs | Measure-Object detail -Average).Average), 3); black = [Math]::Round((($rs | Measure-Object black -Average).Average), 3)
        maxFlat = $u.maxFlat; untexMax = $u.untexMax; untexFrac = $u.untexFrac; noise = $u.noise; minRegionDetail = ($rs | Measure-Object detail -Minimum).Minimum; edges = [Math]::Round((($rs | Measure-Object edges -Average).Average), 4) }
}
# Verdict for a gameplay frame: FAIL = the world is predominantly missing / black / one blank area.
function Present-WorldVerdict($w) {
    if (-not $w) { return "SKIP" }
    $t = $script:PresentThr
    if ($w.black -ge $t.world_black_fail -or $w.maxFlat -ge $t.world_maxflat_fail -or $w.detail -lt $t.world_detail_fail -or (Present-Malformed $w)) { return "FAIL" }
    if ($w.detail -lt $t.world_detail_pass) { return "PARTIAL" }
    return "PASS"
}
# A set of gameplay frames (one state / one map): FAIL when MOST frames show an empty world, PARTIAL when some do.
function Present-WorldSetVerdict($ws) {
    $ws = @($ws | Where-Object { $_ }); if (-not $ws.Count) { return "SKIP" }
    $empty = @($ws | Where-Object { (Present-WorldVerdict $_) -eq "FAIL" }).Count; $weak = @($ws | Where-Object { (Present-WorldVerdict $_) -eq "PARTIAL" }).Count
    if ($empty * 2 -gt $ws.Count) { return "FAIL" }; if ($empty -or $weak * 2 -gt $ws.Count) { return "PARTIAL" }; return "PASS"
}
function Present-SceneVerdict($m) {
    if (-not $m) { return "SKIP" }; $t = $script:PresentThr
    if ($m.black -ge $t.scene_black_fail -or $m.maxFlat -ge $t.scene_maxflat_fail -or $m.detail -lt $t.world_detail_fail -or (Present-Malformed $m)) { return "FAIL" }
    if ($m.detail -lt $t.scene_detail_pass) { return "PARTIAL" }; return "PASS"
}
function Present-UiVerdict($m) {
    if (-not $m) { return "SKIP" }; $t = $script:PresentThr
    if ($m.detail -lt $t.ui_detail_fail -or $m.maxFlat -ge $t.ui_maxflat_fail -or $m.black -ge 0.9 -or (Present-Malformed $m)) { return "FAIL" }; return "PASS"
}
# Structural similarity in a region: { lumaCorr, gradCorr, detailIoU }.
function Present-Similar([string]$A, [string]$B, [double[]]$R = @(0, 0, 1, 1)) {
    if (-not (Test-Path $A) -or -not (Test-Path $B)) { return $null }
    $s = [WfcPresent]::Similarity((Resolve-Path $A).Path, (Resolve-Path $B).Path, $R[0], $R[1], $R[2], $R[3])
    return [pscustomobject]@{ luma = [Math]::Round($s[0], 3); grad = [Math]::Round($s[1], 3); iou = [Math]::Round($s[2], 3) }
}

# GFx dumps ("dump:<movie>" script step): every dump in a log -> visible clip paths, visible texts and the focused button.
# A HIDDEN clip hides its subtree. Focus = a visible button whose tab_mc stands at its last frame (11/11) or whose glow
# alpha > 0.3 (SharedComponents button convention).
function Read-GfxDumps([string]$Log) {
    $dumps = New-Object System.Collections.Generic.List[object]; $cur = $null; $stack = @(); $hiddenBelow = -1; $idx = 0
    foreach ($ln in [IO.File]::ReadLines($Log)) {
        $idx++
        if ($ln -match '\] GFX DUMP (\S+)') { $cur = [pscustomobject]@{ movie = $Matches[1]; line = $idx; clips = New-Object System.Collections.Generic.List[string]; texts = New-Object System.Collections.Generic.List[string]; focused = New-Object System.Collections.Generic.List[string] }; $dumps.Add($cur); $stack = @(); $hiddenBelow = -1; continue }
        if (-not $cur) { continue }
        if ($ln -notmatch '^(\s*)(clip|text|shape|button) ''([^'']*)''') { if ($ln -match '^\[') { $cur = $null }; continue }
        $depth = $Matches[1].Length / 2; $kind = $Matches[2]; $name = $Matches[3]
        if ($stack.Count -gt $depth) { $stack = @($stack | Select-Object -First $depth) }
        if ($hiddenBelow -ge 0 -and $depth -gt $hiddenBelow) { $stack += , $name; continue }
        $hiddenBelow = -1
        if ($ln -match ' HIDDEN' -or $ln -match ' a=0\.00 ') { $hiddenBelow = $depth; $stack += , $name; continue }
        $path = (@($stack) + $name) -join "/"; $stack += , $name
        if ($kind -eq "clip") { $cur.clips.Add($path) }
        if ($kind -eq "text" -and $ln -match '"(.*)"\s*$') { if ($Matches[1]) { $cur.texts.Add($Matches[1]) } }
        if ($kind -eq "clip" -and $name -eq "tab_mc" -and $ln -match 'frame (\d+)/(\d+)' -and [int]$Matches[1] -eq [int]$Matches[2] -and [int]$Matches[2] -gt 1) { $cur.focused.Add(($stack | Select-Object -SkipLast 1 | Select-Object -Last 1)) }
    }
    return $dumps.ToArray()
}

# Pawn movement per UI state from a frontend-launched run (needs WFC_SMOKE_FRAMES + WFC_LOGEVERY): the log interleaves
# "FLOW ui.state ... to=X" and "frame N pos x y z" lines. Returns per-state segments with the horizontal distance moved.
function Read-MotionByUiState([string]$Log) {
    $segs = New-Object System.Collections.Generic.List[object]; $state = "?"; $last = $null; $seg = $null; $n = 0
    foreach ($ln in [IO.File]::ReadLines($Log)) {
        $n++
        if ($ln -match 'FLOW ui\.state from=\S+ to=(\S+)') { $state = $Matches[1]; $seg = [pscustomobject]@{ state = $state; startLine = $n; frames = 0; dist = 0.0 }; $segs.Add($seg); $last = $null; continue }
        if ($ln -match '\] frame (\d+) pos (\S+) (\S+) (\S+)' -and $seg) {
            $p = @([double]$Matches[2], [double]$Matches[3], [double]$Matches[4])
            if ($last) { $seg.dist += [Math]::Sqrt([Math]::Pow($p[0] - $last[0], 2) + [Math]::Pow($p[2] - $last[2], 2)) }
            $seg.frames++; $last = $p
        }
    }
    return $segs.ToArray()
}
