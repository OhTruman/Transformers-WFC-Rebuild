# Branch-aware lane report: what each feature lane changed since the last integration, without merging anything.
#
#   .\tools\fidelity\lane-watch.ps1 [-Base origin/integration/milestone-05] [-OutDir <dir>] [-Fetch]
#
# Per lane (agents/frontend, gameplay, rendering, systems) and for the local AssetTools / RE-Workspace repos:
#   - commits ahead of the base, files touched by area
#   - WFC_* hooks added / removed in the diff; removed hooks that Experimental's tools use = tests to retire / adapt
#   - FlowTrace events and LOG prefixes added (new evidence the gate can consume)
#   - FIDELITY / STATUS / docs headings added (what the owner claims)
#   - git merge-tree conflicts against the base and pairwise between lanes (no refs written)
#   - "isolated" vs "needs integration": a lane change that touches only its own area can be validated on an ab.ps1
#     export of the lane; anything on the launch path / World / Application / audio-render-frontend seams needs the merge
# Writes LANES.md + lanes.json. State in work\lanewatch\last.json so a rerun reports which heads moved.
param([string]$Base = "origin/integration/milestone-05", [string]$OutDir = "", [switch]$Fetch)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
if ($Fetch) { & git -C $root fetch -q origin 2>$null }
if (-not $OutDir) { $OutDir = Join-Path $root ("work\lanewatch\" + (Get-Date -Format "yyyyMMdd-HHmm")) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$statePath = Join-Path $root "work\lanewatch\last.json"; $last = @{}
if (Test-Path $statePath) { (Get-Content $statePath -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $last[$_.Name] = $_.Value } }
$lanes = "frontend", "gameplay", "rendering", "systems"
function G { param([Parameter(ValueFromRemainingArguments)]$a) $o = & git -C $root @a 2>$null; return @($o) }
$baseSha = @(G rev-parse $Base)[0]
# hooks the Experimental tools rely on
$toolHooks = @{}; foreach ($f in Get-ChildItem (Join-Path $root "tools\fidelity") -Recurse -Include *.ps1, *.py) { foreach ($m in [regex]::Matches((Get-Content $f.FullName -Raw), 'WFC_[A-Z0-9_]{3,}')) { $toolHooks[$m.Value] = $true } }
function Area($path) {
    switch -regex ($path) {
        '^src/frontend/|^src/ui/|^data/frontend/' { "frontend" } '^src/render/|^shaders?/|^tools/render/' { "rendering" }
        '^src/audio/|^src/game/(Sound|Ambient|Music|LevelAudio|VehicleAudio|Foley)' { "systems" }
        '^src/core/Application|^src/game/World\.|^src/game/Match|^CMakeLists' { "SEAM" }
        '^src/game/' { "gameplay" } '^tools/' { "tools" } '\.md$' { "docs" } default { "other" }
    }
}
$report = [ordered]@{}; $md = New-Object System.Collections.Generic.List[string]
$md.Add("# Lane watch vs $Base ($($baseSha.Substring(0,7))) - $(Get-Date -Format 'yyyy-MM-dd HH:mm')"); $md.Add("")
$md.Add("Nothing here is merged or integrated: these are the lane heads as pushed. Final verdicts only on an integration commit."); $md.Add("")
foreach ($ln in $lanes) {
    $ref = "origin/agents/$ln"; $head = @(G rev-parse $ref)[0]; if (-not $head) { continue }
    $ahead = @(G log --oneline "$Base..$ref")
    $files = @(G diff --name-only "$Base...$ref")
    $diff = (G diff "$Base...$ref" -- src tools) -join "`n"
    $addH = @([regex]::Matches(($diff -split "`n" | Where-Object { $_ -like "+*" }) -join "`n", 'getenv\("(WFC_[A-Z0-9_]+)"\)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    $remH = @([regex]::Matches(($diff -split "`n" | Where-Object { $_ -like "-*" }) -join "`n", 'getenv\("(WFC_[A-Z0-9_]+)"\)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    $remH = @($remH | Where-Object { $addH -notcontains $_ }); $addH = @($addH | Where-Object { $remH -notcontains $_ })
    $flowAdd = @([regex]::Matches(($diff -split "`n" | Where-Object { $_ -like "+*" }) -join "`n", 'emit\("([a-zA-Z.]+)"') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    $logAdd = @([regex]::Matches(($diff -split "`n" | Where-Object { $_ -like "+*" }) -join "`n", 'LOG_INFO\("([A-Z][A-Z0-9_]{2,}) ') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    $docH = @((G diff "$Base...$ref" -- FIDELITY.md STATUS.md docs) | Where-Object { $_ -match '^\+#{1,3} ' } | ForEach-Object { $_.Substring(1).Trim() })
    $areas = @($files | ForEach-Object { Area $_ } | Group-Object | ForEach-Object { "$($_.Name) $($_.Count)" })
    $seam = @($files | Where-Object { (Area $_) -eq "SEAM" })
    $conf = @(G merge-tree --write-tree --name-only --no-messages $Base $ref | Select-Object -Skip 1 | Where-Object { $_ })
    $moved = if ($last.ContainsKey($ln)) { $last[$ln] -ne $head } else { $true }
    $report[$ln] = [ordered]@{ head = $head; moved = $moved; ahead = $ahead.Count; areas = $areas; seam_files = $seam; hooks_added = $addH; hooks_removed = $remH; tool_hooks_removed = @($remH | Where-Object { $toolHooks[$_] }); flow_events_added = $flowAdd; log_prefixes_added = $logAdd; doc_headings = $docH; conflicts_with_base = $conf }
    $md.Add("## agents/$ln  ``$($head.Substring(0,7))``  ($($ahead.Count) ahead$(if ($moved) { ', MOVED since last watch' }))"); $md.Add("")
    if (-not $ahead.Count) { $md.Add("No commits beyond the integration base."); $md.Add(""); continue }
    foreach ($c in ($ahead | Select-Object -First 12)) { $md.Add("- $c") }
    $md.Add(""); $md.Add("- areas: " + ($areas -join ", "))
    $md.Add("- **validate in isolation:** " + $(if (-not $seam.Count) { "yes - lane-local change; ab.ps1 export of $ref + the lane's own self-tests + the matching Experimental suite" } else { "partly - lane-local parts only" }))
    $md.Add("- **needs Integration:** " + $(if ($seam.Count) { ($seam -join ", ") + " (launch path / World / Application seams)" } else { "nothing on the shared seams" }))
    $md.Add("- conflicts with $Base`: " + $(if ($conf.Count) { $conf -join ", " } else { "none" }))
    $md.Add("- hooks added: " + $(if ($addH.Count) { $addH -join ", " } else { "-" }) + "; removed: " + $(if ($remH.Count) { $remH -join ", " } else { "-" }))
    if (@($report[$ln].tool_hooks_removed).Count) { $md.Add("- **STALE-TEST RISK:** Experimental tools use removed hooks: " + ($report[$ln].tool_hooks_removed -join ", ")) }
    $md.Add("- new trace evidence: flow " + $(if ($flowAdd.Count) { $flowAdd -join ", " } else { "-" }) + "; log " + $(if ($logAdd.Count) { $logAdd -join ", " } else { "-" }))
    if ($docH.Count) { $md.Add("- owner claims (new doc headings): " + (($docH | Select-Object -First 8) -join " / ")) }
    $md.Add("")
}
# pairwise conflicts among lanes that moved
$md.Add("## Pairwise merge preview (lane heads)"); $md.Add(""); $md.Add("| pair | conflicting paths |"); $md.Add("|---|---|")
for ($i = 0; $i -lt $lanes.Count; $i++) { for ($j = $i + 1; $j -lt $lanes.Count; $j++) {
    $a = "origin/agents/$($lanes[$i])"; $b = "origin/agents/$($lanes[$j])"
    $c = @(G merge-tree --write-tree --name-only --no-messages $a $b | Select-Object -Skip 1 | Where-Object { $_ })
    $md.Add("| $($lanes[$i]) + $($lanes[$j]) | $(if ($c.Count) { $c -join ', ' } else { 'clean' }) |") } }
$md.Add("")
foreach ($repo in @(@{ n = "AssetTools"; p = "F:\Transformers Rebuild\AssetTools" }, @{ n = "RE-Workspace"; p = "F:\Transformers Rebuild\RE-Workspace" })) {
    if (-not (Test-Path (Join-Path $repo.p ".git"))) { continue }
    $hd = (& git -C $repo.p rev-parse HEAD 2>$null); $lg = @(& git -C $repo.p log --oneline -6 2>$null)
    $moved = if ($last.ContainsKey($repo.n)) { $last[$repo.n] -ne $hd } else { $true }
    $report[$repo.n] = [ordered]@{ head = $hd; moved = $moved; recent = $lg }
    $md.Add("## $($repo.n) ``$($hd.Substring(0,7))``$(if ($moved) { ' (MOVED)' })"); $md.Add(""); foreach ($l in $lg) { $md.Add("- $l") }; $md.Add("")
}
$report | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $OutDir "lanes.json")
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "LANES.md")
$state = @{}; foreach ($k in $report.Keys) { $state[$k] = $report[$k].head }; $state | ConvertTo-Json | Set-Content -Encoding UTF8 $statePath
"LANE WATCH -> $OutDir\LANES.md; moved: " + (($report.Keys | Where-Object { $report[$_].moved }) -join ", ")
