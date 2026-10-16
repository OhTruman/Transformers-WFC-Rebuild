# After m05\build-target.ps1: point <Target>\work\render at integration's full render data (build-target's own pass builds only
# Streets; its output is kept as render_built) and add work\ab\<Alias> as a junction to the target (eyepass / r4leak use m9c_<sha>).
# Checks the Release exe has the RT shaders and WFC_NOINTERACT. Junctions are removed with rmdir (never a recursive delete).
param([Parameter(Mandatory)][string]$Target, [string]$Alias = "", [string]$RenderData = "F:\Transformers Rebuild\Rebuild\work\render")
$Target = (Resolve-Path $Target).Path; $r = Join-Path $Target "work\render"
if (Test-Path $r) { $it = Get-Item $r -Force; if ($it.LinkType) { cmd /c rmdir "$r" } elseif (-not (Test-Path (Join-Path $Target "work\render_built"))) { Rename-Item $r "render_built" } else { throw "$r exists and render_built too" } }
New-Item -ItemType Directory -Force (Join-Path $Target "work") | Out-Null
cmd /c mklink /J "$r" "$RenderData" | Out-Null
if ($Alias) { $a = Join-Path (Split-Path $Target) $Alias; if (-not (Test-Path $a)) { cmd /c mklink /J "$a" "$Target" | Out-Null } }
$exe = Join-Path $Target "build-release\bin\wfc_rebuild.exe"
$ni = Select-String -Path $exe -Pattern "WFC_NOINTERACT" -SimpleMatch -Quiet
$rt = Select-String -Path (Join-Path $Target "build-release\build.ninja") -Pattern "WFC_RT_SHADERS=1" -SimpleMatch -Quiet
"{0}: render -> {1} ({2} maps); alias {3}; NOINTERACT {4}; RT shaders {5}" -f $Target, $RenderData, (Get-ChildItem $r -Directory).Count, $(if ($Alias) { $Alias } else { "-" }), $ni, $rt
if (-not ($ni -and $rt)) { exit 1 }
