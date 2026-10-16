# Render the authored eye-pass views (tools/fidelity/views/<map id>.txt, from eyepass_views.py) with one exe per map and mode:
# direct match boot, WFC_SHOTLIST (each view held 24 frames: two warm-up entries + the kept one), RT via WFC_RT_MODE, HD via
# WFC_HDTEXTURES + WFC_HD_ROOT. Keeps <OutDir>\<map>-<mode>\<view>.bmp for the kept views only (warm-up captures deleted).
#   .\tools\fidelity\eyepass-views.ps1 -Exe <wfc_rebuild.exe> -OutDir <dir> [-Maps 508,510,507,504,509] [-Modes off,real,realhd] [-Size 2560x1440]
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [string[]]$Maps = @("508", "510", "507", "504", "509"),
      [string[]]$Modes = @("off", "real", "realhd"), [string]$Size = "2560x1440",
      [string]$HdRoot = "F:\Transformers Rebuild\Rebuild-Systems\work\hd_dev_full", [string]$RenderData = "", [string]$RtEnv = "")
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$Maps = @($Maps | ForEach-Object { "$_" -split '[,;\s]+' } | Where-Object { $_ }); $Modes = @($Modes | ForEach-Object { "$_" -split '[,;\s]+' } | Where-Object { $_ })   # (-File passes '508,510' as one string)
$names = @{ "508" = "MP_IAC_Streets"; "510" = "MP_UND_Gorge"; "507" = "MP_ORB_Debris"; "504" = "MP_IAC_Rust"; "509" = "MP_KON_Molten" }
$Exe = (Resolve-Path $Exe).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
foreach ($m in $Maps) {
    $list = Join-Path $PSScriptRoot "views\$m.txt"; if (-not (Test-Path $list)) { Write-Host "${m}: no views file"; continue }
    $lines = @(Get-Content $list | Where-Object { $_.Trim() }); $kept = @($lines | ForEach-Object { ($_ -split ' ')[0] } | Where-Object { $_ -notmatch '_w\d$' })
    foreach ($mode in $Modes) {
        $d = Join-Path $OutDir "$m-$mode"; if (Test-Path $d) { Remove-Item -Recurse -Force $d }; New-Item -ItemType Directory -Force $d | Out-Null
        $rt = @{ off = "0"; real = "2"; realhd = "2" }[$mode]
        $e = @{ WFC_MAP = $names[$m]; WFC_SHOTLIST = $list; WFC_SHOTDIR = $d; WFC_SMOKE_FRAMES = "$(8 * $lines.Count + 24)"; WFC_LOCKSTEP = "1";
                WFC_RT_MODE = $rt; WFC_RENDERSIZE = $Size; WFC_MUTE = "1"; WFC_NOINTERACT = "1"; WFC_LOGEVERY = "0" }
        if ($mode -eq "realhd") { $e.WFC_HDTEXTURES = "2"; $e.WFC_HD_ROOT = $HdRoot }
        if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
        if ($rt -ne "0" -and $RtEnv) { foreach ($kv in ($RtEnv -split '\s+' | Where-Object { $_ -match '=' })) { $i = $kv.IndexOf('='); $e[$kv.Substring(0, $i)] = $kv.Substring($i + 1) } }
        $t0 = Get-Date
        $rc = Invoke-WfcExe $Exe $d $e "run.log" 900
        Get-ChildItem $d -Filter "*_w?.bmp" | Remove-Item
        $got = @($kept | Where-Object { Test-Path (Join-Path $d "$_.bmp") })
        $lg = Join-Path $d "wfc.log"
        $hd = if (Test-Path $lg) { @(Select-String -Path $lg -Pattern 'HD texture pack available: \d+ textures' | Select-Object -First 1 | ForEach-Object { $_.Matches[0].Value }) } else { @() }
        $rtl = if (Test-Path $lg) { @(Select-String -Path $lg -Pattern 'renderer: Ray Tracing [A-Za-z]+' | Select-Object -Last 1 | ForEach-Object { $_.Matches[0].Value }) } else { @() }
        # client size: the last NOINTERACT line must be the requested size (Get-WfcClientRect); else the pixels are not comparable
        if ($mode -eq "realhd") {   # modern material maps (hd_pbr_index.json) + modern materials on, else UNKNOWN
            $mm = @(Select-String -Path $lg -Pattern 'HD modern material maps: \d+' | Select-Object -First 1 | ForEach-Object { $_.Matches[0].Value })
            $mo = @(Select-String -Path $lg -Pattern 'Modern materials on for MP_\S+' | Select-Object -First 1 | ForEach-Object { $_.Matches[0].Value })
            if ($mm.Count -and $mo.Count) { $hd += "; $($mm[0]); $($mo[0])" } else { $hd += "; UNKNOWN: modern materials not confirmed"; Set-Content (Join-Path $d "MODERN_UNKNOWN") "maps: $($mm -join '') on: $($mo -join '')" }
        }
        $cr = Get-WfcClientRect $lg; $req = $Size -split 'x'
        $crNote = if (-not $cr.known) { "client rect unknown" } elseif ($cr.differs -or $cr.w -ne [int]$req[0] -or $cr.h -ne [int]$req[1]) { "INVALID client $($cr.w)x$($cr.h) vs $Size"; Set-Content (Join-Path $d "INVALID") $cr.line } else { "client $($cr.w)x$($cr.h)" }
        Write-Host ("{0} {1}: {8}; rc {2}, {3}/{4} views ({5:N1} min) {6} {7}" -f $m, $mode, $rc, $got.Count, $kept.Count, ((Get-Date) - $t0).TotalMinutes, ($rtl -join ''), ($hd -join ''), $crNote)
    }
}
