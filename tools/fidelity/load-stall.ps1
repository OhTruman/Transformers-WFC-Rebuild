# FRONTEND -> MATCH LOAD STALL (package 7 bug item, Integration 2026-10-17; HANG_DUMPS.md class A / D): one frontend boot per mode
# (Off / Faithful / Realistic / Realistic + HD 2) through WFC_FRONTEND_AUTOPLAY=TDM,508, untimed. Per boot: the map load
# (loading.start kind=Map -> loading.close / the first match frame), the watchdog's stall lines (seconds, phase), the largest frame
# gap in the PACING lines, and the dumps written.
#   powershell -File tools/fidelity/load-stall.ps1 -Exe <wfc_rebuild.exe> -OutDir <dir> [-Modes off,faithful,real,realhd] [-Secs 150]
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [string[]]$Modes = @("off", "faithful", "real", "realhd"),
      [int]$Secs = 180, [string]$RenderData = "F:\Transformers Rebuild\Rebuild\work\render",
      [string]$HdRoot = "F:\Transformers Rebuild\Rebuild-Systems\work\hd_dev_full", [switch]$ReportOnly)   # (-ReportOnly: re-parse the logs already in -OutDir)
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
$Modes = @($Modes | % { $_ -split ',' } | ? { $_ })
$rows = @()
foreach ($m in $Modes) {
    $d = Join-Path $OutDir $m; New-Item -ItemType Directory -Force $d | Out-Null
    $rt = @{ off = 0; faithful = 1; real = 2; realhd = 2 }[$m]
    if (-not $ReportOnly) { (Get-BotProfile 31 32 -Extended -Width 2560 -Height 1440 -HdTextures ([int]($m -eq "realhd")) -RayTracing $rt) | Set-Content -Encoding ascii (Join-Path $d "wfc_profile.ini") }
    # the autoplay script with 'wait:t=2' before SetSelectedMapID: af4d0731's autoplay sets the map before the GameLobby GFx map
    # selector initialises, and the selector's own init call (SetSelectedMapID 501) then replaces Streets with Seed Of Corruption
    # (2026-10-17 boot; Rendering's 10-16 boots won the race). Application_Frontend's own scripts wait 1 s there.
    $script = "wait:frontend;wait:ui=FrontEnd;snapshot:mainmenu;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;snapshot:partylobby;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;call:Online.SetSelectedMapID,508;snapshot:gamelobby;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;snapshot:ingame"
    $envs = @{ WFC_FRONTEND_SCRIPT = $script; WFC_SKIPINTRO = "1"; WFC_RENDER_DATA = $RenderData; WFC_MUTE = "1"; WFC_NOINTERACT = "1" }   # (SKIPINTRO: a fresh profile plays the logo movies, and the script waits forever)
    if ($m -eq "realhd") { $envs.WFC_HDTEXTURES = "2"; $envs.WFC_HD_ROOT = $HdRoot }
    $t0 = Get-Date
    if (-not $ReportOnly) { $r = Invoke-WfcExe -Exe $Exe -Dir $d -Env $envs -TimeoutSec $Secs }
    $log = Join-Path $d "wfc.log"; $L = if (Test-Path $log) { Get-Content $log } else { @() }
    function T($rx) { $x = $L | Select-String -Pattern $rx | Select-Object -First 1; if ($x -and $x.Line -match '^\s*([\d.]+)\s') { [double]$Matches[1] } else { $null } }
    $ls = T 'FLOW loading.start kind=Map'; $lc = T 'FLOW loading.close kind=Map'; $lb = T 'FLOW level.begin level=MP'
    $wd = @($L | Select-String -Pattern "watchdog: no frame completed for ([\d.]+) s - render thread last at '([^']+)'" | % { "$($_.Matches[0].Groups[1].Value) s at '$($_.Matches[0].Groups[2].Value)'" })
    $gap = ($L | Select-String -Pattern 'PACING 600 frames: .* max ([\d.]+) ms' | % { [double]$_.Matches[0].Groups[1].Value } | Measure-Object -Maximum).Maximum
    # frontend boots log no timestamps: the renderer's own "load: N ms without a presented frame, ending at '<step>'" lines carry
    # the gaps - the largest one, and the sum of those between loading.start kind=Map and loading.close kind=Map (the match load)
    $iS = ($L | Select-String -Pattern 'FLOW loading.start kind=Map' | Select-Object -First 1).LineNumber
    $iC = ($L | Select-String -Pattern 'FLOW loading.close kind=Map' | Select-Object -First 1).LineNumber
    $gl = @($L | Select-String -Pattern "load: (\d+) ms without a presented frame, ending at '([^']+)'" | % { [pscustomobject]@{ n = $_.LineNumber; ms = [int]$_.Matches[0].Groups[1].Value; at = $_.Matches[0].Groups[2].Value } })
    $big = $gl | Sort-Object ms -Descending | Select-Object -First 1
    $inMap = @($gl | ? { $iS -and $iC -and $_.n -gt $iS -and $_.n -lt $iC })
    $mapGaps = ($inMap | Measure-Object ms -Sum).Sum; $mapBig = $inMap | Sort-Object ms -Descending | Select-Object -First 1
    $map = ($L | Select-String -Pattern 'FLOW loading.start kind=Map .*message=in ([^=]+?) bink' | Select-Object -First 1 | % { $_.Matches[0].Groups[1].Value })
    $dumps = @(Get-ChildItem $d -Filter 'wfc_hang_*.dmp' -ErrorAction SilentlyContinue).Count
    $modeLine = ($L | Select-String -Pattern 'FLOW display.rayTracing mode=\d|HD texture pack available: \d+|Modern materials on for MP_\w+' | % { $_.Matches[0].Value }) -join '; '
    $row = [pscustomobject]@{ mode = $m; map = $map; reachedMatch = [bool]$iC
        mapLoadNoFrame_ms = $mapGaps; mapLoadLargest = if ($mapBig) { "$($mapBig.ms) ms at '$($mapBig.at)'" } else { "" }
        largestGap = if ($big) { "$($big.ms) ms at '$($big.at)'" } else { "" }
        watchdog = ($wd -join ' | '); maxPacing_ms = $gap; dumps = $dumps; contaminated = (Test-Path (Join-Path $d "CONTAMINATED.txt")); settings = $modeLine }
    $rows += $row; "$(Get-Date -Format HH:mm:ss) $m : map $map, reached match $($row.reachedMatch); match load without frames $mapGaps ms (largest $($row.mapLoadLargest)); largest gap of the boot $($row.largestGap); watchdog: $($row.watchdog); dumps $dumps; $modeLine"
}
$rows | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutDir "load_stall.json")
$md = @("# Frontend -> match load stall ($(Get-Date -Format 'yyyy-MM-dd HH:mm'), $Exe)", "", "| mode | map | reached match | match load without frames, ms (sum) | largest in the match load | largest gap of the boot | watchdog stall | dumps | contaminated | settings line |", "|---|---|---|---|---|---|---|---|---|---|")
$md += $rows | % { "| $($_.mode) | $($_.map) | $($_.reachedMatch) | $($_.mapLoadNoFrame_ms) | $($_.mapLoadLargest) | $($_.largestGap) | $(if ($_.watchdog) { $_.watchdog } else { 'none' }) | $($_.dumps) | $($_.contaminated) | $($_.settings) |" }
$md | Set-Content -Encoding utf8 (Join-Path $OutDir "LOAD_STALL.md"); $md
