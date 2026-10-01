# Run wfc_fidelity against any git ref (optionally + a patch) without touching other worktrees.
#
#   .\tools\fidelity\ab.ps1 -Ref agents/gameplay
#   .\tools\fidelity\ab.ps1 -Ref HEAD -Patch tools\fidelity\proposals\mesh-yaw-offset.patch -Name yawfix
#   .\tools\fidelity\ab.ps1 -Ref main -Exe          # also build wfc_rebuild.exe (for capture.ps1 -Exe)
#   .\tools\fidelity\ab.ps1 -Ref agents/gameplay -Merge agents/systems -Name gp+sys   # merge preview
#
# Exports the ref's tracked sources (git archive, read-only on the repo) into work/ab/<Name>,
# overlays THIS worktree's tools/fidelity (so every ref is judged by the same checks), applies the
# patch, builds with this worktree's toolchain and writes work/ab/<Name>/report.json + run.txt.
# Compare two runs with diff-reports.ps1.
param(
    [Parameter(Mandatory)][string]$Ref,
    [string]$Patch = "",
    [string]$Merge = "",
    [string]$Name = "",
    [switch]$Exe,
    [ValidateRange(1, 64)][int]$Jobs = 2,
    [string[]]$HarnessArgs = @()
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Name) { $Name = ($Ref -replace '[^A-Za-z0-9._-]', '_') + $(if ($Merge) { "+" + ($Merge -replace '[^A-Za-z0-9._-]', '_') } else { "" }) + $(if ($Patch) { "+" + [IO.Path]::GetFileNameWithoutExtension($Patch) } else { "" }) }
$abRoot = Join-Path $root "work\ab"
$dest = Join-Path $abRoot $Name
New-Item -ItemType Directory -Force $abRoot | Out-Null

# Fresh export (only ever deletes inside work/ab).
if (Test-Path $dest) {
    $full = [IO.Path]::GetFullPath($dest)
    if (-not $full.StartsWith([IO.Path]::GetFullPath($abRoot) + "\")) { throw "unsafe target $full" }
    Remove-Item -LiteralPath $full -Recurse -Force
}
New-Item -ItemType Directory -Force $dest | Out-Null
$sha = (git -C $root rev-parse --short $Ref).Trim()
if ($LASTEXITCODE -ne 0) { throw "unknown ref $Ref" }
$tree = $Ref
if ($Merge) {
    # Merge preview: git merge-tree computes the merged tree in the object store only — no
    # worktree, index or ref is touched. Conflicts stop the run and are listed.
    $out = git -C $root merge-tree --write-tree --name-only $Ref $Merge
    $mt = $LASTEXITCODE
    $tree = ($out | Select-Object -First 1).Trim()
    $sha = "$sha+" + (git -C $root rev-parse --short $Merge).Trim()
    if ($mt -ne 0) {
        $out | Set-Content (Join-Path $dest "CONFLICTS.txt")
        Write-Host "[$Name] merge preview $Ref + $Merge CONFLICTS:"
        $out | Select-Object -Skip 1 | Where-Object { $_ -and $_ -notmatch '^Auto-merging' } | ForEach-Object { Write-Host "  $_" }
        exit 3
    }
}
$tarFile = Join-Path $dest "src.tar"
git -C $root archive --format=tar -o $tarFile $tree
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
tar -xf $tarFile -C $dest
Remove-Item $tarFile

# Overlay the current harness (working copy) and make sure the CMake hook exists.
$h = Join-Path $dest "tools\fidelity"
if (Test-Path $h) { Remove-Item -LiteralPath $h -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $dest "tools") | Out-Null
Copy-Item -Recurse (Join-Path $root "tools\fidelity") $h
Remove-Item -LiteralPath (Join-Path $h "proposals") -Recurse -Force -ErrorAction SilentlyContinue
$cml = Join-Path $dest "CMakeLists.txt"
if (-not (Select-String -Path $cml -Pattern "tools/fidelity" -Quiet)) {
    Add-Content -Path $cml -Value "`noption(WFC_BUILD_FIDELITY `"`" ON)`nif(WFC_BUILD_FIDELITY)`n    enable_testing()`n    add_subdirectory(tools/fidelity)`nendif()"
}

if ($Patch) {
    $pp = (Resolve-Path $Patch).Path
    Push-Location $dest
    try { git apply --whitespace=nowarn $pp; if ($LASTEXITCODE -ne 0) { throw "patch did not apply: $pp" } }
    finally { Pop-Location }
}

# Build with this worktree's toolchain into the export's own build dir.
$tc = Join-Path $root ".toolchain"
$clangDir = Join-Path $tc "llvm-mingw-20260922-ucrt-x86_64\bin"
$cmakeExe = Join-Path $tc "cmake-4.4.3-windows-x86_64\bin\cmake.exe"
$ninjaExe = Join-Path $tc "ninja\ninja.exe"
$env:PATH = "$clangDir;" + $env:PATH
$build = Join-Path $dest "build"
& $cmakeExe -S $dest -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaExe" "-DCMAKE_BUILD_TYPE=Debug" `
    "-DCMAKE_CXX_COMPILER=$clangDir\clang++.exe" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "configure failed for $Ref" }
$targets = @("wfc_fidelity") + $(if ($Exe) { @("wfc_rebuild") } else { @() })
& $cmakeExe --build $build --parallel $Jobs --target @targets
if ($LASTEXITCODE -ne 0) { throw "build failed for $Ref (harness may need updating for API changes on that ref)" }

$report = Join-Path $dest "report.json"
$run = Join-Path $dest "run.txt"
Push-Location $dest
try {
    & (Join-Path $build "bin\wfc_fidelity.exe") --json $report @HarnessArgs *> $run
    $code = $LASTEXITCODE
} finally { Pop-Location }
Set-Content -Path (Join-Path $dest "SOURCE.txt") -Value "ref=$Ref sha=$sha patch=$Patch"
$summary = (Select-String -Path $run -Pattern '^SUMMARY').Line
Write-Host "[$Name] $Ref$(if ($Merge) { " + $Merge" })@$sha  $summary  (exit $code)"
Write-Host "  report: $report"
exit $code
