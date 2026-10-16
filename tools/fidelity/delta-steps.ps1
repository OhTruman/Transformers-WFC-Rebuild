# Delta FAST step selection (package 6): the FAST GPU rows whose inputs changed between two heads, from git's changed paths.
#   .\tools\fidelity\delta-steps.ps1 -Base <penultimate sha> -Head <final sha> [-SwitchFlips]   -> prints the step list (comma)
# -SwitchFlips (a runtime switch default changed in the range): every row reruns. Mapping (Experimental -> Integration 2026-10-16):
#   src/render/**, shaders                 -> route, mapswitch, renderstate
#   src/game/**                            -> weapontest, chassistest, mechanics, route
#   src/frontend/**, src/ui/**             -> route
#   src/audio/**                           -> route, mapswitch (the audio rows read their runs)
#   src/core/**, src/platform/**, src/assets/** -> every row (shared runtime)
#   CMakeLists.txt, cmake/**, third_party/** -> every row
#   tools/**, docs/**                      -> none (unit ctest only)
param([Parameter(Mandatory)][string]$Base, [Parameter(Mandatory)][string]$Head, [switch]$SwitchFlips)
$all = @("weapontest", "chassistest", "route", "mapswitch", "renderstate", "mechanics")
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$paths = @(git -C $root diff --name-only $Base $Head)
$sel = New-Object System.Collections.Generic.HashSet[string]
if ($SwitchFlips) { $all | ForEach-Object { [void]$sel.Add($_) } }
foreach ($p in $paths) {
    $p = $p -replace '\\', '/'
    if ($p -match '^(src/core|src/platform|src/assets)/' -or $p -match '^(CMakeLists\.txt|cmake/|third_party/)') { $all | ForEach-Object { [void]$sel.Add($_) } }
    elseif ($p -match '^src/render/' -or $p -match '\.(hlsl|hlsli|glsl)$') { "route", "mapswitch", "renderstate" | ForEach-Object { [void]$sel.Add($_) } }
    elseif ($p -match '^src/game/') { "weapontest", "chassistest", "mechanics", "route" | ForEach-Object { [void]$sel.Add($_) } }
    elseif ($p -match '^src/(frontend|ui)/') { [void]$sel.Add("route") }
    elseif ($p -match '^src/audio/') { "route", "mapswitch" | ForEach-Object { [void]$sel.Add($_) } }
    elseif ($p -match '^src/') { $all | ForEach-Object { [void]$sel.Add($_) } }   # (an unmapped src area: be safe)
}
$out = @($all | Where-Object { $sel.Contains($_) })
Write-Host ("delta {0}..{1}: {2} changed paths -> rows: {3}" -f $Base, $Head, $paths.Count, $(if ($out.Count) { $out -join "," } else { "(none; unit ctest only)" }))
$out -join ","
