# INPUT LATENCY (tier TARGETED; Gameplay's WFC_LATENCYPROBE). In a live direct-boot match (WFC_MATCH=TDM: WFC_MAP alone is a sandbox, the probe never arms) with the local pawn alive, the probe
# holds Fire / Forward every ~1.5 s from that frame's input sample and stops at the first frame whose draw shows the effect
# (shot serial changed / pawn moved > 1 cm), timing input sample -> that frame's present. Per kind (fire, move) and async mode
# (WFC_ASYNCSTEP 0 / 1). Expected: 1 frame in both modes (the local action runs in the synchronous local part before the draw).
# ms is that frame's time, so compare frames, and ms only at matched fps. Uncapped by default; -FpsLimit to match fps.
#
#   .\tools\fidelity\latency-probe.ps1 -Root work\ab\<target> -OutDir <dir> [-Seconds 60] [-Async 0,1] [-Kinds fire,move] [-FpsLimit 0]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [int]$Seconds = 60, [string[]]$Async = @("0", "1"),
      [string[]]$Kinds = @("fire", "move"), [int]$FpsLimit = 0, [string]$Map = "MP_IAC_Streets", [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "latency.$id" $status $null $note $owner }
if (-not $H.Contains("WFC_LATENCYPROBE")) { Res "hook" "UNKNOWN" "build has no WFC_LATENCYPROBE (Gameplay, after eeb2e0f)" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$Async = @($Async | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$Kinds = @($Kinds | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$rows = New-Object System.Collections.Generic.List[object]
foreach ($k in $Kinds) { foreach ($a in $Async) {
    $tag = "${k}_async$a"; $d = Join-Path $OutDir $tag; New-Item -ItemType Directory -Force $d | Out-Null; $lg = Join-Path $d "wfc.log"
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "$tag.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        $e = @{ WFC_BOOT = "match"; WFC_MAP = $Map; WFC_MATCH = "TDM"; WFC_LATENCYPROBE = $k; WFC_ASYNCSTEP = $a; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0"
                WFC_SMOKE_FRAMES = "100000000"; WFC_FPS_LIMIT = "$FpsLimit" }
        # stop after -Seconds: the probe loop runs until the smoke frame count; a frame budget at ~300 fps uncapped is generous
        $e.WFC_SMOKE_FRAMES = "$([Math]::Max(3000, $Seconds * $(if ($FpsLimit -gt 0) { $FpsLimit } else { 300 })))"
        $null = Invoke-WfcExe $exe $d $e "run.log" ($Seconds * 4 + 180)
    }
    if (-not (Test-Path $lg)) { continue }
    $lat = @(Select-String $lg -Pattern '\] LATENCY (\w+) input->present ([\d.]+) ms \((\d+) frames.*async=(\d).*avg ([\d.]+) ms, max ([\d.]+), avg frames ([\d.]+) over (\d+)')
    $to = @(Select-String $lg -Pattern '\] LATENCY \w+ timeout').Count
    if (-not $lat.Count) { Res $tag "UNKNOWN" ("no LATENCY lines ({0} timeouts)" -f $to) "Gameplay"; continue }
    $last = $lat[-1].Matches[0].Groups
    $frames = @($lat | ForEach-Object { [int]$_.Matches[0].Groups[3].Value })
    $row = [pscustomobject][ordered]@{ kind = $k; async = $a; probes = [int]$last[8].Value; avg_ms = [double]$last[5].Value; max_ms = [double]$last[6].Value; avg_frames = [double]$last[7].Value
        max_frames = ($frames | Measure-Object -Maximum).Maximum; timeouts = $to }
    $rows.Add($row)
    Res $tag $(if ($row.avg_frames -le 1.05 -and $to -eq 0) { "PASS" } elseif ($row.avg_frames -le 1.5) { "PARTIAL" } else { "FAIL" }) ("{0} async={1}: {2} probes, avg {3} frames (max {4}), input->present avg {5} ms (max {6}); timeouts {7}. Expected 1 frame." -f $k, $a, $row.probes, $row.avg_frames, $row.max_frames, $row.avg_ms, $row.max_ms, $to) "Gameplay"
} }
Write-WfcCsv $rows (Join-Path $OutDir "latency.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"LATENCY: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$rows | Format-Table -AutoSize | Out-String -Width 160
