param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$Test,[switch]$Run)
# Explicit fallback for hosts where Ninja subprocess execution stalls.
# Normal builds should use build.ps1, which configures and builds with CMake/Ninja.
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$cc=Join-Path $root '.toolchain\llvm-mingw-20260922-ucrt-x86_64\bin\clang++.exe'
$build=Join-Path $root "build\$Configuration"
New-Item -ItemType Directory -Path $build -Force | Out-Null
$flags=@('-std=c++17','-Wall','-Wextra','-Wpedantic','-static','-I',(Join-Path $root 'src'))
if($Configuration -eq 'Debug') { $flags+=@('-g','-O0') } else { $flags+='-O2' }
& $cc @flags -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $root 'src\Main.cpp') (Join-Path $root 'src\game\Gameplay.cpp') (Join-Path $root 'src\platform\WindowsPlatform.cpp') (Join-Path $root 'src\render\OpenGLRenderer.cpp') -mwindows -lopengl32 -lgdi32 -luser32 -o (Join-Path $build 'WFCRebuild.exe')
if($LASTEXITCODE) { throw 'Application compile/link failed' }
& $cc @flags (Join-Path $root 'tests\GameplayTests.cpp') (Join-Path $root 'src\game\Gameplay.cpp') -o (Join-Path $build 'GameplayTests.exe')
if($LASTEXITCODE) { throw 'Gameplay tests compile/link failed' }
if($Test) {
    & (Join-Path $build 'GameplayTests.exe')
    if($LASTEXITCODE) { throw 'Gameplay tests failed' }
    $proc=Start-Process -FilePath (Join-Path $build 'WFCRebuild.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
    if(!$proc.WaitForExit(20000)) { $proc.Kill(); throw 'Rendering smoke timed out' }
    $proc.Refresh()
    if($proc.ExitCode -ne 0) { throw "Rendering smoke failed ($($proc.ExitCode)); see $build\rebuild.log" }
    Get-Content -LiteralPath (Join-Path $build 'rebuild.log')
}
if($Run) { Start-Process -FilePath (Join-Path $build 'WFCRebuild.exe') -WorkingDirectory $build -WindowStyle Normal }

