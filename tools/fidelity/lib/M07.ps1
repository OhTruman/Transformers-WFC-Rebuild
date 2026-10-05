# Milestone 07 gate helpers. Dot-source after lib\Run.ps1, lib\Flow.ps1, lib\M05.ps1, lib\Present.ps1.
# Expectations come from tools\fidelity\m07\expectations.m07.json (generated from AssetTools / ExtractedAssets by
# m07\gen_expectations.py), never from the build under test.

function Get-M07Expectations([switch]$Regenerate) {
    $f = Join-Path $PSScriptRoot "..\m07\expectations.m07.json"
    if ($Regenerate -or -not (Test-Path $f)) { & "F:\Transformers Rebuild\AssetTools\bin\py\python.exe" (Join-Path $PSScriptRoot "..\m07\gen_expectations.py") $f | Out-Host }
    return Get-Content -Raw $f | ConvertFrom-Json
}

# One graphical WFC instance at a time, system-wide (other sessions included). Waits; returns $false on timeout so a
# suite can record SKIP instead of starting a second renderer.
function Wait-WfcGpu([int]$Minutes = 0) {
    if (-not $Minutes) { $Minutes = if ($env:WFC_GATE_GPU_WAIT_MIN) { [int]$env:WFC_GATE_GPU_WAIT_MIN } else { 240 } }
    $deadline = (Get-Date).AddMinutes($Minutes)
    while ((Get-Date) -lt $deadline) {
        if (-not @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue).Count) { Start-Sleep 3; if (-not @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue).Count) { return $true } }
        Start-Sleep 10
    }
    return $false
}

# Rendering's per-shot diagnostics (WFC_VISUALCHECK + shot: writes <shot>.json): inherited GL state at beginFrame,
# opaque draws without depth testing, GL errors, draw counts. $null when the build has no such report.
function Read-ShotDiag([string]$Bmp) {
    $j = "$Bmp.json"; if (-not (Test-Path $j)) { return $null }
    try { $d = Get-Content -Raw $j | ConvertFrom-Json } catch { return $null }
    $st = @{}; foreach ($kv in ("$($d.gl_entry_state)" -split ' ')) { $p = $kv -split '=', 2; if ($p.Count -eq 2) { $st[$p[0]] = $p[1] } }
    return [pscustomobject]@{ verdict = $d.verdict; reasons = $d.reasons; world = $d.world_draws; bsp = $d.bsp_draws; draws = $d.draws; noDepth = $d.opaque_no_depth_test
        glErrors = $d.gl_errors; materials = $d.distinct_materials; viewport = (@($d.viewport) -join ","); entry = $st; entryRaw = "$($d.gl_entry_state)" }
}
# A sane world-frame GL state (what the 3D pass needs at entry, or re-asserts itself): depth test on, depth writes on,
# cull face per the renderer, no scissor, full colour mask, fill mode. Returns the list of deviations.
function Test-GlEntryState($entry) {
    $bad = @()
    if (-not $entry -or -not $entry.Count) { return @("no state captured") }
    if ($entry["depth"] -ne "1") { $bad += "depth test off" }
    if ($entry["depthMask"] -ne "1") { $bad += "depth writes off" }
    if ($entry["scissor"] -eq "1") { $bad += "scissor on" }
    if ($entry["colorMask"] -and $entry["colorMask"] -ne "1111") { $bad += "colour mask $($entry['colorMask'])" }
    if ($entry["polygonMode"] -and $entry["polygonMode"] -ne "6914") { $bad += "polygon mode $($entry['polygonMode'])" }   # GL_FILL
    if ($entry["fbo"] -and $entry["fbo"] -ne "0") { $bad += "framebuffer $($entry['fbo']) bound" }
    return $bad
}

# VISUALCHECK lines per visit (level.begin order) and UI state: in-play world / BSP / materials / noDepth / glErr medians.
function Read-VisualCheckByVisit([string]$Log) {
    $rows = New-Object System.Collections.Generic.List[object]; $visit = 0; $level = "boot"; $ui = ""
    if (-not (Test-Path $Log)) { return @() }
    foreach ($ln in [IO.File]::ReadLines($Log)) {
        if ($ln -match 'FLOW level\.begin level=(\S+) map=(\S+)') { $visit++; $level = "$($Matches[1]):$($Matches[2])" }
        if ($ln -match 'FLOW ui\.state from=\S+ to=(\S+)') { $ui = $Matches[1] }
        if ($ln -match 'VISUALCHECK frame (\d+) .*draws=(\d+) world=(\d+) bsp=(\d+) materials=(\d+)') {
            $nd = if ($ln -match 'noDepth=(\d+)') { [int]$Matches[1] } else { $null }; $ge = if ($ln -match 'glErr=(\d+)') { [int]$Matches[1] } else { $null }
            $m = [regex]::Match($ln, 'draws=(\d+) world=(\d+) bsp=(\d+) materials=(\d+)')
            $rows.Add([pscustomobject]@{ visit = $visit; level = $level; ui = $ui; world = [int]$m.Groups[2].Value; bsp = [int]$m.Groups[3].Value; materials = [int]$m.Groups[4].Value; noDepth = $nd; glErr = $ge; pass = ($ln -match '-> PASS') })
        }
    }
    return $rows.ToArray()
}
function Median($vals) { $v = @($vals | Where-Object { $_ -ne $null } | Sort-Object); if (-not $v.Count) { return $null }; return $v[[int]($v.Count / 2)] }

# frame log ("frame N pos x y z key=value ...") as objects with x / y / z / form / grounded / hspeed / vy / drv / nitro
function Read-FrameLog([string]$Log) { if (-not (Test-Path $Log)) { return @() }; return @(Read-WfcFrames $Log) }

# Markdown matrix writer: rows = objects, columns = property names
function Write-M07Matrix($Rows, [string[]]$Columns, [string]$Path, [string]$Title, [string[]]$Preamble = @()) {
    $md = @("# $Title", "") + $Preamble + @("", ("| " + ($Columns -join " | ") + " |"), ("|" + (($Columns | ForEach-Object { "---" }) -join "|") + "|"))
    foreach ($r in $Rows) { $md += "| " + (($Columns | ForEach-Object { ("$($r.$_)" -replace '\|', '/') }) -join " | ") + " |" }
    $md | Set-Content -Encoding UTF8 $Path
}
# status ordering for a cell that aggregates several checks
function Worst([string[]]$s) { foreach ($k in "FAIL", "PARTIAL", "UNKNOWN", "HUMAN", "SKIP", "INFO", "PASS") { if ($s -contains $k) { return $k } }; return "SKIP" }
