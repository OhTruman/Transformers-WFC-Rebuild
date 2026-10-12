# Identify the material of the near pillar at Molten c15 (left third of the frame): FIXEDCAM c15 turned so the pillar is
# centred, WFC_PICK logs the surface under the crosshair. Two yaw offsets (sign convention unverified), shot to confirm.
$ErrorActionPreference = "Continue"
$T = "F:\Transformers Rebuild\Rebuild-Experimental\tools\fidelity"
. (Join-Path $T "lib\Run.ps1"); . (Join-Path $T "lib\M05.ps1"); . (Join-Path $T "lib\M07.ps1")
$exe = "F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m9c_42dd17b\build-release\bin\wfc_rebuild.exe"
$out = "F:\Transformers Rebuild\Rebuild-Experimental\work\lmspec\pick"
$url = "MP_KON_Molten?GameModeTag=TDM?BotsAutobot=8?BotsDecepticon=8?BotDifficulty=1?ExtendedPlayers=1"
foreach ($yaw in @(58.0, 83.0, 33.0)) {
    $d = Join-Path $out "y$yaw"; New-Item -ItemType Directory -Force $d | Out-Null
    if (-not (Wait-WfcGpu)) { "GPU busy, skip $yaw"; continue }
    $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_FIXEDCAM = "295.9,101.3,49.6,$yaw,-24.4"; WFC_SMOKE_FRAMES = "2405"; WFC_LOGEVERY = "0"
            WFC_NOMOUSE = "1"; WFC_LOCKSTEP = "1"; WFC_SEED = "123"; WFC_FLOWSEED = "123"; WFC_SHOTEVERY = "$d,2400,2400"; WFC_AUTODETECT = "0"; WFC_PICK = "1" }
    $null = Invoke-WfcExe $exe $d $e "run.log" 400
    "yaw $yaw $(Get-Date -Format HH:mm:ss): " + ((Select-String -Path (Join-Path $d "wfc.log"), (Join-Path $d "run.log") -Pattern "PICK " -ErrorAction SilentlyContinue | Select-Object -Last 1).Line)
}
"pick done $(Get-Date -Format HH:mm:ss)"
