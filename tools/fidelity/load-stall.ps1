# FRONTEND -> MATCH LOAD STALL (package 7 bug item, Integration 2026-10-17; HANG_DUMPS.md class A / D): one frontend boot per mode
# (Off / Faithful / Realistic / Realistic + HD 2) through WFC_FRONTEND_AUTOPLAY=TDM,508, untimed. Per boot: the map load
# (loading.start kind=Map -> loading.close / the first match frame), the watchdog's stall lines (seconds, phase), the largest frame
# gap in the PACING lines, and the dumps written.
#   powershell -File tools/fidelity/load-stall.ps1 -Exe <wfc_rebuild.exe> -OutDir <dir> [-Modes off,faithful,real,realhd] [-Secs 150]
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [string[]]$Modes = @("off", "faithful", "real", "realhd"),
      [int]$Secs = 150, [string]$RenderData = "F:\Transformers Rebuild\Rebuild\work\render",
      [string]$HdRoot = "F:\Transformers Rebuild\Rebuild-Systems\work\hd_dev_full")
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
$Modes = @($Modes | % { $_ -split ',' } | ? { $_ })
$rows = @()
foreach ($m in $Modes) {
    $d = Join-Path $OutDir $m; New-Item -ItemType Directory -Force $d | Out-Null
    $rt = @{ off = 0; faithful = 1; real = 2; realhd = 2 }[$m]
    (Get-BotProfile 31 32 -Extended -Width 2560 -Height 1440 -HdTextures ([int]($m -eq "realhd")) -RayTracing $rt) | Set-Content -Encoding ascii (Join-Path $d "wfc_profile.ini")
    $envs = @{ WFC_FRONTEND_AUTOPLAY = "TDM,508"; WFC_RENDER_DATA = $RenderData; WFC_MUTE = "1"; WFC_NOINTERACT = "1" }
    if ($m -eq "realhd") { $envs.WFC_HDTEXTURES = "2"; $envs.WFC_HD_ROOT = $HdRoot }
    $t0 = Get-Date
    $r = Invoke-WfcExe -Exe $Exe -Dir $d -Env $envs -TimeoutSec $Secs
    $log = Join-Path $d "wfc.log"; $L = if (Test-Path $log) { Get-Content $log } else { @() }
    function T($rx) { $x = $L | Select-String -Pattern $rx | Select-Object -First 1; if ($x -and $x.Line -match '^\s*([\d.]+)\s') { [double]$Matches[1] } else { $null } }
    $ls = T 'FLOW loading.start kind=Map'; $lc = T 'FLOW loading.close kind=Map'; $lb = T 'FLOW level.begin level=MP'
    $wd = @($L | Select-String -Pattern "watchdog: no frame completed for ([\d.]+) s - render thread last at '([^']+)'" | % { "$($_.Matches[0].Groups[1].Value) s at '$($_.Matches[0].Groups[2].Value)'" })
    $gap = ($L | Select-String -Pattern 'PACING 600 frames: .* max ([\d.]+) ms' | % { [double]$_.Matches[0].Groups[1].Value } | Measure-Object -Maximum).Maximum
    $dumps = @(Get-ChildItem $d -Filter 'wfc_hang_*.dmp' -ErrorAction SilentlyContinue).Count
    $modeLine = ($L | Select-String -Pattern 'FLOW display.rayTracing mode=\d|HD texture pack available: \d+|Modern materials on for MP_\w+' | % { $_.Matches[0].Value }) -join '; '
    $row = [pscustomobject]@{ mode = $m; loadStart = $ls; loadClose = $lc; levelBegin = $lb
        load_s = if ($ls -ne $null -and $lc -ne $null) { [math]::Round($lc - $ls, 1) } else { $null }
        watchdog = ($wd -join ' | '); maxFrameGap_ms = $gap; dumps = $dumps; contaminated = (Test-Path (Join-Path $d "CONTAMINATED.txt")); settings = $modeLine }
    $rows += $row; "$(Get-Date -Format HH:mm:ss) $m : load $($row.load_s) s (start $ls, close $lc); max PACING gap $gap ms; watchdog: $($row.watchdog); dumps $dumps; $modeLine"
}
$rows | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutDir "load_stall.json")
$md = @("# Frontend -> match load stall ($(Get-Date -Format 'yyyy-MM-dd HH:mm'), $Exe)", "", "| mode | map load s (loading.start -> close) | max frame gap ms (PACING) | watchdog stall | dumps | settings line |", "|---|---|---|---|---|---|")
$md += $rows | % { "| $($_.mode) | $($_.load_s) | $($_.maxFrameGap_ms) | $(if ($_.watchdog) { $_.watchdog } else { 'none' }) | $($_.dumps) | $($_.settings) |" }
$md | Set-Content -Encoding utf8 (Join-Path $OutDir "LOAD_STALL.md"); $md
