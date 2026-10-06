# MILESTONE 07 GATE - one command for the next integration.
#
#   .\tools\fidelity\m07-gate.ps1 -Ref origin/integration/<milestone> [-Name m7int] [-Build] [-Only a,b] [-Quick] [-ReportOnly]
#
# 1. build      isolated export of the exact sha (Debug + Release, m05\build-target.ps1) + render data for EVERY
#               launchable map and the five UI levels (tools\render\build_render_data.ps1 of that commit)
# 2. layout     a mirror of the user's layout (<tree>\build\release\bin + <tree>\work\render junction) so the product
#               resolves its data exactly as when the user double-clicks the Release exe
# 3. suites (one graphical WFC process at a time, system-wide; each waits for other sessions' renderers):
#      presentation  presentation-gate.ps1 (Debug): frontend route, world coverage, route vs direct, reference,
#                    pause overlay, soft-lock watchdog, text entry, Layout card
#      maploss       map-loss-gate.ps1 on the MIRRORED Release exe: Streets -> lobby -> Berth -> lobby -> Streets x
#                    fresh / 720p / 1080p / low-texture profiles, draws vs same-start direct boot
#      renderstate   m07-renderstate.ps1: GL state after every overlay + negative controls
#      frontend      m07-frontend.ps1: movie aspect x3, screen tour, nav.check, return routes, account name
#      characters    m07-characters.ps1: 33 exports + class presets end to end (OPTIMUS FALLBACK explicit)
#      maps          m07-maps.ps1: every launchable map, per-map mechanics + one frontend chain over all versus maps
#      modes         m07-modes.ps1: every mode by its own rules + frontend results / lobby / second match
#      lifecycle     m05-e2e-gate.ps1 Release -Full (quit routing auto-adapted) + Debug R1,R2,R4
#      acceptance    playtest-acceptance.ps1
#      soak          m07-soak.ps1: all versus maps x passes, plateau vs growth
# 4. M07-GATE.md: VISUAL HEALTH first (presentation + map-loss + render state + per-map frontend world), then every
#    suite, product failures by owner, test faults, UNKNOWN / HUMAN, links to the matrices and the human sheet.
param([Parameter(Mandatory)][string]$Ref, [string]$Name = "", [switch]$Build, [string[]]$Only = @(), [switch]$Quick, [switch]$ReportOnly, [string]$OutDir = "")
$ErrorActionPreference = "Continue"   # native tools (cmake, ninja, python) write warnings to stderr: never abort a step on them; failures are judged from the results
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ }); function Want($k) { return -not $Only.Count -or $Only -contains $k }
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$root = Get-WfcRoot
$sha = (& git -C $root rev-parse --verify "$Ref^{commit}").Trim(); if (-not $Name) { $Name = "m7_" + $sha.Substring(0, 7) }
$tgt = Join-Path $root "work\ab\$Name"
if (-not $OutDir) { $OutDir = Join-Path $root ("work\m07gate\" + $sha.Substring(0, 7)) }
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$log = Join-Path $OutDir "gate.log"
function Note($s) { Add-Content -Encoding UTF8 $log $s; Write-Host $s }
function Step($name, [scriptblock]$sb) { $t0 = Get-Date; Note ("[{0}] {1} ..." -f (Get-Date -Format HH:mm:ss), $name); try { & $sb 2>&1 | Select-Object -Last 2 | ForEach-Object { Note "  $_" } } catch { Note "  ERROR: $_" }; Note ("  ({0:N0} min)" -f ((Get-Date) - $t0).TotalMinutes) }
$X = Get-M07Expectations -Regenerate

# ---------- 1. build + render data
if (-not $ReportOnly -and ($Build -or -not (Test-Path (Join-Path $tgt "build-release\bin\wfc_rebuild.exe")))) {
    Step "build $sha" { & (Join-Path $PSScriptRoot "m05\build-target.ps1") -Ref $sha -Name $Name -Jobs 4 }
}
$built = if (Test-Path (Join-Path $tgt "M05_TARGET.txt")) { ((Get-Content (Join-Path $tgt "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "" }
if ($built -ne $sha) { throw "work\ab\$Name holds '$built', not ${sha}: rerun with -Build (never mix product revisions)" }
if (-not $ReportOnly) {
    $need = @($X.maps | Where-Object { $_.launchable } | ForEach-Object { $_.runtime }) + @("UI_FrontEnd", "UI_PartyLobby", "UI_Lobby", "UI_CharacterCustomization", "UI_CampaignLobby")
    $missing = @($need | Where-Object { -not (Test-Path (Join-Path $tgt "work\render\$_\materials_glsl.json")) })
    if ($missing.Count) { Step "render data ($($missing.Count))" { Push-Location $tgt; try { foreach ($m in $missing) { & powershell -NoProfile -ExecutionPolicy Bypass -File "tools\render\build_render_data.ps1" -Map $m *> (Join-Path $OutDir "rd_$m.log") } } finally { Pop-Location }; "render data: $($missing -join ', ')" } }
}
# ---------- 2. the user's layout: <mirror>\Rebuild\build\release\bin + <mirror>\Rebuild\work\render (junction)
$mirror = Join-Path $OutDir "layout\Rebuild"; $mbin = Join-Path $mirror "build\release\bin"
if (-not $ReportOnly -and -not (Test-Path (Join-Path $mbin "wfc_rebuild.exe"))) {
    New-Item -ItemType Directory -Force $mbin, (Join-Path $mirror "work") | Out-Null
    Copy-Item (Join-Path $tgt "build-release\bin\*") $mbin
    if (-not (Test-Path (Join-Path $mirror "work\render"))) { cmd /c mklink /J "$(Join-Path $mirror 'work\render')" "$(Join-Path $tgt 'work\render')" | Out-Null }
}
Note "M07 GATE $Ref = $sha; export $tgt; user-layout mirror $mbin"

# ---------- 3. suites
$suites = [ordered]@{
    presentation = { & (Join-Path $PSScriptRoot "presentation-gate.ps1") -Root $tgt -OutDir (Join-Path $OutDir "presentation") -Config Debug -Label $Ref -ReportOnly:$ReportOnly }
    maploss      = { & (Join-Path $PSScriptRoot "map-loss-gate.ps1") -Exe (Join-Path $mbin "wfc_rebuild.exe") -OutDir (Join-Path $OutDir "maploss") -Configs $(if ($Quick) { "normal,w1920" } else { "normal,w1280,w1920,lowtex" }) -ReportOnly:$ReportOnly }
    renderstate  = { & (Join-Path $PSScriptRoot "m07-renderstate.ps1") -Root $tgt -OutDir (Join-Path $OutDir "renderstate") -ReportOnly:$ReportOnly }
    frontend     = { & (Join-Path $PSScriptRoot "m07-frontend.ps1") -Root $tgt -OutDir (Join-Path $OutDir "frontend") -ReportOnly:$ReportOnly }
    characters   = { & (Join-Path $PSScriptRoot "m07-characters.ps1") -Root $tgt -OutDir (Join-Path $OutDir "characters") -ReportOnly:$ReportOnly }
    maps         = { & (Join-Path $PSScriptRoot "m07-maps.ps1") -Root $tgt -OutDir (Join-Path $OutDir "maps") -ReportOnly:$ReportOnly }
    modes        = { & (Join-Path $PSScriptRoot "m07-modes.ps1") -Root $tgt -OutDir (Join-Path $OutDir "modes") -Maps $(if ($Quick) { "MP_IAC_Streets" } else { "MP_IAC_Streets,MP_IAC_Seed" }) -ReportOnly:$ReportOnly }
    lifecycle    = { if (-not $ReportOnly) { if (Wait-WfcGpu) { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Release -Full -Cycles 4 -OutDir (Join-Path $OutDir "lifecycle_release") | Select-Object -Last 1 }; if (Wait-WfcGpu) { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Debug -Runs "R1,R2,R4" -Cycles 3 -OutDir (Join-Path $OutDir "lifecycle_debug") | Select-Object -Last 1 } } }
    acceptance   = { if (-not $ReportOnly -and (Wait-WfcGpu)) { & (Join-Path $PSScriptRoot "playtest-acceptance.ps1") -Root $tgt -OutDir (Join-Path $OutDir "acceptance") } }
    soak         = { & (Join-Path $PSScriptRoot "m07-soak.ps1") -Root $tgt -OutDir (Join-Path $OutDir "soak") -Passes $(if ($Quick) { 2 } else { 3 }) -ReportOnly:$ReportOnly }
}
foreach ($k in $suites.Keys) { if (Want $k) { Step $k $suites[$k] } }

# ---------- 4. M07-GATE.md
function Rep($rel) { $p = Join-Path $OutDir $rel; if (Test-Path $p) { return @((Get-Content -Raw $p | ConvertFrom-Json).results) } else { return @() } }
$reports = [ordered]@{ presentation = "presentation\report.json"; maploss = "maploss\report.json"; renderstate = "renderstate\report.json"; frontend = "frontend\report.json"; characters = "characters\report.json"
    maps = "maps\report.json"; modes = "modes\report.json"; lifecycle_release = "lifecycle_release\report.json"; lifecycle_debug = "lifecycle_debug\report.json"; acceptance = "acceptance\report.json"; soak = "soak\report_m07.json" }
$all = @(); $per = [ordered]@{}; foreach ($k in $reports.Keys) { $r = @(Rep $reports[$k]); $per[$k] = $r; $all += $r }
function Kind($r) { if ($r.status -ne "FAIL") { return $r.status }; if ("$($r.owner)" -match '^Experimental$' -or "$($r.note)" -match 'TEST FAULT') { return "TEST" }; return "PRODUCT" }
function Cnt($rows) { $c = [ordered]@{}; foreach ($r in $rows) { $k = Kind $r; if (-not $c.Contains($k)) { $c[$k] = 0 }; $c[$k]++ }; return (($c.Keys | ForEach-Object { "$_ $($c[$_])" }) -join " / ") }
$visualFails = @($all | Where-Object { $_.status -eq "FAIL" -and $_.id -match '^(present\.(gameplay\.world|gameplay\.route_vs_direct|reference|frontend\.scene|transition)|maploss\..*(environment|draws_vs_direct)|m07state\.transition|m07map\..*frontend_world|m07fe\.screen)' })
$controlBlind = @($all | Where-Object { $_.id -eq "m07state.control.both_defences_off" -and $_.status -eq "FAIL" })
$visual = if ($controlBlind.Count) { "UNKNOWN - the render-state detector is blind (negative control passed)" } elseif ($visualFails.Count) { "VISUALLY BROKEN ($($visualFails.Count) presentation failures)" } elseif (-not @($per.presentation).Count -or -not @($per.maploss).Count) { "NOT JUDGED - presentation / map-loss suites missing" } else { "NO CATASTROPHIC PRESENTATION FAILURE (human sheet still required)" }
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# M07 gate - $Ref"); $md.Add(""); $md.Add("| | |"); $md.Add("|---|---|"); $md.Add("| integration commit | ``$sha`` |"); $md.Add("| export | ``$tgt`` (Debug + Release) |"); $md.Add("| user-layout mirror | ``$mbin`` |"); $md.Add("| run | $(Get-Date -Format 'yyyy-MM-dd HH:mm')$(if ($Quick) { ' QUICK' }) |"); $md.Add("")
$md.Add("## VISUAL HEALTH: **$visual**"); $md.Add(""); $md.Add("This line overrides every count below. PASS counts, asset counts and draw counts never stand in for presentation."); $md.Add("")
$md.Add("**" + (Cnt $all) + "**"); $md.Add(""); $md.Add("| suite | result | matrix |"); $md.Add("|---|---|---|")
$links = @{ maps = "maps/MAPS.md"; modes = "modes/MODES.md"; characters = "characters/CHARACTERS.md"; renderstate = "renderstate/RENDERSTATE.md"; frontend = "frontend/FRONTEND.md"; presentation = "presentation/PRESENTATION.md"; maploss = "maploss/MAP-LOSS.md"; soak = "soak/SOAK.md" }
foreach ($k in $per.Keys) { $md.Add("| $k | $(if ($per[$k].Count) { Cnt $per[$k] } else { 'not run' }) | $(if ($links[$k]) { $links[$k] }) |") }; $md.Add("")
$md.Add("## PRODUCT FAIL by owner"); $md.Add("")
foreach ($g in @($all | Where-Object { (Kind $_) -eq "PRODUCT" } | Group-Object { ("$($_.owner)" -split '/')[0] } | Sort-Object Count -Descending)) { $md.Add("### $($g.Name) ($($g.Count))"); foreach ($r in $g.Group) { $md.Add("- **$($r.id)** [$($r.owner)]: " + (("$($r.note)" -replace "`n", ' ') -replace '\|', '/')) }; $md.Add("") }
foreach ($sec in @(@{ t = "TEST / HARNESS FAIL (Experimental)"; f = { (Kind $args[0]) -eq "TEST" } }, @{ t = "UNKNOWN / PARTIAL"; f = { $args[0].status -in "UNKNOWN", "PARTIAL", "WAITING" } }, @{ t = "HUMAN CHECK"; f = { $args[0].status -eq "HUMAN" } })) {
    $rows = @($all | Where-Object { & $sec.f $_ }); $md.Add("## $($sec.t): $($rows.Count)"); $md.Add(""); foreach ($r in $rows) { $md.Add("- **$($r.id)** [$($r.owner)]: " + (("$($r.note)" -replace "`n", ' ') -replace '\|', '/')) }; $md.Add("") }
$md.Add("Human playtest sheet: ``tools/fidelity/HUMAN-CHECK-M07.md``. Contact sheets: ``*/sheet_*.png``.")
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "M07-GATE.md")
Note ("M07 GATE ${sha}: VISUAL HEALTH $visual; " + (Cnt $all) + " -> $OutDir\M07-GATE.md")
