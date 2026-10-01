# Compare two wfc_fidelity JSON reports (A = baseline, B = candidate).
#
#   .\tools\fidelity\diff-reports.ps1 work\ab\main\report.json work\ab\agents_gameplay\report.json
#
# Lists status transitions (e.g. KNOWN -> PASS = fixed, PASS -> FAIL = regression) and numeric
# metrics that moved by more than -MinDelta (relative, default 0.5 %). Exit 1 if B has new FAILs.
param(
    [Parameter(Mandatory)][string]$A,
    [Parameter(Mandatory)][string]$B,
    [double]$MinDelta = 0.005
)
$ErrorActionPreference = "Stop"
function Load($p) {
    $j = Get-Content -Raw $p | ConvertFrom-Json
    $m = [ordered]@{}
    foreach ($r in $j.results) { $m[$r.id] = $r }
    return $m
}
$ra = Load $A; $rb = Load $B
$rank = @{ FAIL = 0; KNOWN = 1; INFO = 2; SKIP = 3; PASS = 4 }
$newFails = 0
$rows = @()
foreach ($id in ($ra.Keys + $rb.Keys | Select-Object -Unique)) {
    $x = $ra[$id]; $y = $rb[$id]
    $sa = if ($x) { $x.status } else { "-" }
    $sb = if ($y) { $y.status } else { "-" }
    $ma = if ($x -and $null -ne $x.measured) { [double]$x.measured } else { $null }
    $mb = if ($y -and $null -ne $y.measured) { [double]$y.measured } else { $null }
    $moved = $false
    if ($null -ne $ma -and $null -ne $mb) {
        $scale = [Math]::Max([Math]::Abs($ma), 1e-6)
        $moved = [Math]::Abs($mb - $ma) / $scale -gt $MinDelta
    }
    if ($sa -eq $sb -and -not $moved) { continue }
    $tag = if ($sa -ne $sb) {
        if ($sb -eq "FAIL") { $newFails++; "REGRESSED" }
        elseif ($sa -eq "KNOWN" -and $sb -eq "PASS") { "FIXED" }
        elseif ($sa -eq "-") { "NEW" } elseif ($sb -eq "-") { "REMOVED" }
        elseif ($rank[$sb] -gt $rank[$sa]) { "improved" } else { "changed" }
    } else { "moved" }
    $rows += [pscustomobject]@{
        change = $tag; id = $id; A = $sa; B = $sb
        measuredA = if ($null -ne $ma) { "{0:G6}" -f $ma } else { "" }
        measuredB = if ($null -ne $mb) { "{0:G6}" -f $mb } else { "" }
        unit = if ($y) { $y.unit } elseif ($x) { $x.unit } else { "" }
    }
}
$order = @{ REGRESSED = 0; FIXED = 1; changed = 2; improved = 3; NEW = 4; REMOVED = 5; moved = 6 }
$rows | Sort-Object { $order[$_.change] }, id | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
Write-Host ("A: {0}" -f (Get-Content -Raw $A | ConvertFrom-Json).summary)
Write-Host ("B: {0}" -f (Get-Content -Raw $B | ConvertFrom-Json).summary)
if ($newFails -gt 0) { Write-Host "$newFails regression(s) in B"; exit 1 }
