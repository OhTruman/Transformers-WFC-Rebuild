# Clean-room reconstruction — portable build script using the bundled toolchain.
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1 [-Run] [-Clean]
param(
    [switch]$Run,
    [switch]$Clean,
    [string]$Config = "Debug",
    [ValidateRange(1,64)][int]$Jobs = 2
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$tc   = Join-Path $root ".toolchain"

$clangDir = Join-Path $tc "llvm-mingw-20260922-ucrt-x86_64\bin"
$cmakeExe = Join-Path $tc "cmake-4.4.3-windows-x86_64\bin\cmake.exe"
$ninjaExe = Join-Path $tc "ninja\ninja.exe"
$build    = Join-Path $root "build"

foreach ($p in @($clangDir, $cmakeExe, $ninjaExe)) {
    if (-not (Test-Path $p)) { throw "toolchain component missing: $p" }
}

$env:PATH = "$clangDir;" + $env:PATH

if ($Clean -and (Test-Path $build)) {
    $expected = [IO.Path]::GetFullPath((Join-Path $root "build"))
    $resolved = (Resolve-Path -LiteralPath $build).Path
    if ($resolved -ne $expected -or (Get-Item -LiteralPath $build).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Unsafe build cleanup target: $resolved" }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Force $build | Out-Null

& $cmakeExe -S $root -B $build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninjaExe" `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_C_COMPILER=$clangDir\clang.exe" `
    "-DCMAKE_CXX_COMPILER=$clangDir\clang++.exe"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& $cmakeExe --build $build --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$exe = Join-Path $build "bin\wfc_rebuild.exe"
Write-Host "BUILD OK -> $exe"

if ($Run) { & $exe }
