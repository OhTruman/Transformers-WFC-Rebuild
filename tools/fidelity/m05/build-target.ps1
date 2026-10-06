# Build a product commit for the M05 gate in an isolated export (no refs touched, nothing outside work/ab):
#   work\ab\<Name>\build\bin          Debug   (ab.ps1 -Exe)
#   work\ab\<Name>\build-release\bin  Release (every target: wfc_rebuild, wfc_frontend_tests, ...)
#   work\ab\<Name>\work\render        render data (tools\render\build_render_data.ps1 of that commit)
#   .\tools\fidelity\m05\build-target.ps1 -Ref origin/integration/milestone-05 -Name m5int [-Jobs 2] [-NoDebug]
param([Parameter(Mandatory)][string]$Ref, [Parameter(Mandatory)][string]$Name, [int]$Jobs = 2, [switch]$NoDebug)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$sha = (& git -C $root rev-parse --verify "$Ref^{commit}").Trim()
Write-Host "M05 build target: $Ref = $sha -> work\ab\$Name"
& (Join-Path $root "tools\fidelity\ab.ps1") -Ref $sha -Name $Name -Exe -Jobs $Jobs
$abExit = $LASTEXITCODE   # ab.ps1 also runs the fidelity harness: its exit code counts harness failures, not build errors
if (-not (Test-Path (Join-Path $root "work\ab\$Name\build\bin\wfc_rebuild.exe"))) { throw "ab.ps1 did not produce build\bin\wfc_rebuild.exe (exit $abExit)" }
$dest = Join-Path $root "work\ab\$Name"
$tc = Join-Path $root ".toolchain"
$clangDir = Join-Path $tc "llvm-mingw-20260922-ucrt-x86_64\bin"; $cmakeExe = Join-Path $tc "cmake-4.4.3-windows-x86_64\bin\cmake.exe"; $ninjaExe = Join-Path $tc "ninja\ninja.exe"
$env:PATH = "$clangDir;" + $env:PATH
$rel = Join-Path $dest "build-release"
& $cmakeExe -S $dest -B $rel -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaExe" "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_C_COMPILER=$clangDir\clang.exe" "-DCMAKE_CXX_COMPILER=$clangDir\clang++.exe" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "release configure failed" }
& $cmakeExe --build $rel --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "release build failed" }
if (-not $NoDebug) {
    $dbg = Join-Path $dest "build"
    if (Test-Path (Join-Path $dbg "CMakeCache.txt")) { & $cmakeExe --build $dbg --parallel $Jobs; if ($LASTEXITCODE -ne 0) { throw "debug build (all targets) failed" } }
}
$rdScript = Join-Path $dest "tools\render\build_render_data.ps1"
if (Test-Path $rdScript) { Push-Location $dest; try { & powershell -NoProfile -ExecutionPolicy Bypass -File $rdScript | Select-Object -Last 3 } finally { Pop-Location } }
Set-Content -Encoding ASCII (Join-Path $dest "M05_TARGET.txt") "ref=$Ref`nsha=$sha`nbuilt=$(Get-Date -Format s)"
Get-ChildItem (Join-Path $dest "build*\bin") -Filter *.exe | ForEach-Object { $_.FullName }
