# INPUT LATENCY (tier TARGETED; Gameplay's WFC_LATENCYPROBE). In a live direct-boot match (WFC_MATCH=TDM: WFC_MAP alone is a sandbox, the probe never arms) with the local pawn alive, the probe
# holds Fire / Forward every ~1.5 s from that frame's input sample and stops at the first frame whose draw shows the effect
# (shot serial changed / pawn moved > 1 cm), timing input sample -> that frame's present. Per kind (fire, move) and async mode
# (WFC_ASYNCSTEP 0 / 1). Expected: 1 frame in both modes (the local action runs in the synchronous local part before the draw).
# ms is that frame's time, so compare frames, and ms only at matched fps. Uncapped (the meaningful A/B here).
# NOTE: WFC_FPS_LIMIT applies only in the FRONTEND boot - in this direct boot -FpsLimit does not cap (a 60 run measured
# ~4.5 ms frames); use the frontend flow for a capped probe. -FpsLimit is kept for when the limiter covers direct boots.
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
    # Fire / move take effect on the next 60 Hz sim step, so the FRAME count scales with fps (1 frame only at <= ~60 fps, e.g.
    # -FpsLimit 60 or a Debug build): uncapped, the bound is one step (16.7 ms) + one frame. Per-run verdict: max within that bound
    # (capped 60: avg frames ~1); the async-vs-sync verdict is below.
    $frameMs = if ($row.avg_frames -gt 0) { $row.avg_ms / $row.avg_frames } else { 0 }
    $bound = 16.7 + 2 * $frameMs
    $st = if ($to -gt 0) { "FAIL" } elseif ($FpsLimit -gt 0 -and $FpsLimit -le 60) { $(if ($row.avg_frames -le 1.1) { "PASS" } elseif ($row.avg_frames -le 1.5) { "PARTIAL" } else { "FAIL" }) } elseif ($row.max_ms -le $bound) { "PASS" } else { "PARTIAL" }
    Res $tag $st ("{0} async={1}: {2} probes, avg {3} frames (max {4}), input->present avg {5} ms (max {6}; bound one step + 2 frames = {7:N1} ms); timeouts {8}" -f $k, $a, $row.probes, $row.avg_frames, $row.max_frames, $row.avg_ms, $row.max_ms, $bound, $to) "Gameplay"
} }
# async vs sync per kind: the local action runs before the draw in both modes, so async must not add latency
foreach ($k in $Kinds) {
    $s0 = @($rows | Where-Object { $_.kind -eq $k -and $_.async -eq "0" })[0]; $s1 = @($rows | Where-Object { $_.kind -eq $k -and $_.async -eq "1" })[0]
    if ($s0 -and $s1) { Res "$k.async_vs_sync" $(if ($s1.avg_ms -le $s0.avg_ms * 1.25 + 0.5) { "PASS" } else { "FAIL" }) ("{0}: async avg {1} ms / {2} frames vs sync {3} ms / {4} frames (PASS if async <= sync x 1.25 + 0.5 ms)" -f $k, $s1.avg_ms, $s1.avg_frames, $s0.avg_ms, $s0.avg_frames) "Gameplay" }
}
Write-WfcCsv $rows (Join-Path $OutDir "latency.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"LATENCY: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$rows | Format-Table -AutoSize | Out-String -Width 160
