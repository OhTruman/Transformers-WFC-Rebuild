# Rendering's in-product self-tests on the real exe, as a fidelity report:
#   WFC_SHADOWSELFTEST=1  ShadowMask machinery + native constants ("shadow-test PASS|FAIL: <what>")
#   WFC_DLETEST=1         DirectLightEnv rule port               ("dle-test PASS|FAIL: <what>")
# Each logged line becomes one result. A tree without a self-test reports SKIP (not FAIL).
#
#   .\tools\fidelity\render-selftests.ps1 -Exe build\bin\wfc_rebuild.exe -RenderData work\render -OutDir <dir>
param([string]$Exe = "", [string]$RenderData = "", [string]$OutDir = "")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\render_selftests" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$res = New-WfcResults
foreach ($t in @(@{ name = "shadow"; env = "WFC_SHADOWSELFTEST"; tag = "shadow-test" }, @{ name = "dle"; env = "WFC_DLETEST"; tag = "dle-test" })) {
    $dir = Join-Path $OutDir $t.name
    $envs = @{ WFC_SMOKE_FRAMES = "30"; $t.env = "1" }
    if ($RenderData) { $envs.WFC_RENDER_DATA = $RenderData }
    $rc = Invoke-WfcExe $Exe $dir $envs "run.log" 600
    $lines = @(Get-Content (Join-Path $dir "wfc.log") -ErrorAction SilentlyContinue | Select-String ("{0} (PASS|FAIL): (.*)$" -f [regex]::Escape($t.tag)))
    if (-not $lines.Count) { Add-WfcResult $res "render_selftest.$($t.name).ran" "SKIP" $null "no '$($t.tag)' lines (exit $rc): self-test absent in this tree"; continue }
    $i = 0
    foreach ($m in $lines) {
        $i++
        $what = $m.Matches[0].Groups[2].Value.Trim()
        $slug = ($what -replace '[^A-Za-z0-9]+', '_').Trim('_'); if ($slug.Length -gt 60) { $slug = $slug.Substring(0, 60) }
        Add-WfcResult $res ("render_selftest.{0}.{1:D2}_{2}" -f $t.name, $i, $slug) $m.Matches[0].Groups[1].Value $null $what "Rendering"
    }
    $p = @($lines | Where-Object { $_.Matches[0].Groups[1].Value -eq "PASS" }).Count
    "{0}: {1}/{2} PASS (exit {3})" -f $t.name, $p, $lines.Count, $rc
}
$null = Write-WfcReport $res (Join-Path $OutDir "report.json")
