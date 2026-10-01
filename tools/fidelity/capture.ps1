# Screenshot capture harness for the real wfc_rebuild.exe (A/B and regression stills).
#
#   .\tools\fidelity\capture.ps1 -Name idle_chase
#   .\tools\fidelity\capture.ps1 -Name strafe -Frames 120 -Env @{ WFC_AUTOSTRAFE = '1'; WFC_DEBUGDRAW = '1' }
#   .\tools\fidelity\capture.ps1 -Name nolm -Env @{ WFC_NOLIGHTMAP = '1' } -Exe other\wfc_rebuild.exe
#
# Runs the exe headless-ish (WFC_SMOKE_FRAMES), grabs WFC_SHOT on the final frame, converts it to
# PNG and keeps the run log next to it in work/fidelity/shots/. Env vars are scoped to the run.
# NOTE: the in-game smoke path integrates wall-clock frame time, so stills of moving scenes vary
# run to run; for exact numbers use wfc_fidelity traces instead.
param(
    [Parameter(Mandatory)][string]$Name,
    [int]$Frames = 90,
    [hashtable]$Env = @{},
    [string]$Exe = "",
    [string]$OutDir = ""
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\shots" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

$bmp = Join-Path $OutDir "$Name.bmp"
$png = Join-Path $OutDir "$Name.png"
$log = Join-Path $OutDir "$Name.log"
Remove-Item -LiteralPath $bmp, $png -ErrorAction SilentlyContinue

$saved = @{}
$all = @{ WFC_SMOKE_FRAMES = "$Frames"; WFC_SHOT = $bmp } + $Env
foreach ($k in $all.Keys) {
    $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process")
    [Environment]::SetEnvironmentVariable($k, [string]$all[$k], "Process")
}
try {
    $p = Start-Process -FilePath $Exe -WorkingDirectory $OutDir -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err"
} finally {
    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], "Process") }
}
if (-not (Test-Path $bmp)) { throw "no screenshot produced (exit $($p.ExitCode)); see $log" }

Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile($bmp)
try { $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $img.Dispose() }
Remove-Item -LiteralPath $bmp
Write-Host "captured $png (exit $($p.ExitCode))"
