# Untimed Molten lightmap-specular A/B on integration 42dd17b: same build, WFC_NOLMSPEC=1 (off) vs default (on), lockstep seed 123,
# 5 fixed cams, one shot per cam at boot frame 2400 (static geometry identical; bots aligned by lockstep).
$ErrorActionPreference = "Continue"
$T = "F:\Transformers Rebuild\Rebuild-Experimental\tools\fidelity"
. (Join-Path $T "lib\Run.ps1"); . (Join-Path $T "lib\M05.ps1"); . (Join-Path $T "lib\M07.ps1")
$root = "F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m9c_42dd17b"; $exe = Join-Path $root "build-release\bin\wfc_rebuild.exe"
$out = "F:\Transformers Rebuild\Rebuild-Experimental\work\lmspec\molten"
$url = "MP_KON_Molten?GameModeTag=TDM?BotsAutobot=8?BotsDecepticon=8?BotDifficulty=1?ExtendedPlayers=1"
$cams = [ordered]@{ pick = "247.1,148.8,-63.4,91.4,-12"; c11 = "295.9,101.3,-67.5,90.0,-28.1"; c9 = "108.5,101.3,49.6,0.0,-40.5"
                    c15 = "295.9,101.3,49.6,58.0,-24.4"; c14 = "295.9,101.3,-184.5,122.0,-24.4" }
$F = 2400
foreach ($k in $cams.Keys) { foreach ($arm in @("off", "on")) {
    $d = Join-Path $out "$k-$arm"; New-Item -ItemType Directory -Force $d | Out-Null
    if (Test-Path (Join-Path $d ("f{0:D4}.bmp" -f $F))) { continue }
    if (-not (Wait-WfcGpu)) { "GPU busy, skip $k $arm"; continue }
    $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_FIXEDCAM = $cams[$k]; WFC_SMOKE_FRAMES = "$($F + 5)"; WFC_LOGEVERY = "0"; WFC_NOMOUSE = "1"
            WFC_LOCKSTEP = "1"; WFC_SEED = "123"; WFC_FLOWSEED = "123"; WFC_SHOTEVERY = "$d,$F,$F"; WFC_AUTODETECT = "0" }
    if ($arm -eq "off") { $e.WFC_NOLMSPEC = "1" }
    $null = Invoke-WfcExe $exe $d $e "run.log" 400
    "$k $arm $(Get-Date -Format HH:mm:ss) $(Test-Path (Join-Path $d ('f{0:D4}.bmp' -f $F)))"
} }
"molten ab done $(Get-Date -Format HH:mm:ss)"
