# Audio source attachment test for long-lived player-owned sounds (no audio implementation change).
#
#   .\tools\fidelity\audio-attach.ps1            # all scenarios -> work\fidelity\audio\
#
# Runs wfc_rebuild_audiospy (unmodified product sources; Win32Audio replaced by a recorder, see
# measure/SpyAudio.cpp) with scripted movement and evaluates every positional sound:
#   - 3D one-shot (playAt) at a FIXED trigger position: over min(duration, 1.5 s) compare how the
#     source-to-listener distance grows vs how far the listener (camera, which follows the pawn) moved.
#     growth/displacement ~1 => the sound stays behind at its trigger point while the owner moves on.
#   - voices (playVoice + updateVoice): attached if they receive position updates that follow the
#     listener; left behind if positional, long (loop or > 0.5 s) and never updated while moving.
# Writes report.json (wfc_fidelity schema) + findings.txt.
param([string[]]$Scenarios = @("robot_walk", "transform_moving", "landing", "vehicle_loop", "boost", "nitro"),
      [string]$Exe = "", [string]$OutDir = "", [switch]$AnalyzeOnly)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_audiospy.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\audio" }
if (-not (Test-Path $Exe)) { throw "measurement build missing: $Exe (configure with -DWFC_BUILD_MEASURE=ON)" }
$defs = [ordered]@{
    robot_walk       = @{ frames = 700; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.0" } }
    transform_moving = @{ frames = 900; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.0"; WFC_PRESSTRANSFORM = "150" } }
    landing          = @{ frames = 700; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.0"; WFC_AUTOJUMP = "1" } }
    vehicle_loop     = @{ frames = 700; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.8" } }
    boost            = @{ frames = 700; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.5" } }
    nitro            = @{ frames = 900; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTODASH = "200"; WFC_AUTOTURN = "0.5" } }
}
$results = New-Object System.Collections.Generic.List[object]
$find = New-Object System.Collections.Generic.List[string]
function Add($id, $status, $m, $note, $owner = "") {
    $o = [ordered]@{ id = "audio_attach.$id"; status = $status }
    if ($null -ne $m) { $o.measured = [double]$m }
    if ($owner) { $o.owner = $owner }
    $o.note = $note
    $results.Add([pscustomobject]$o)
}
foreach ($name in $Scenarios) {
    $d = $defs[$name]; $dir = Join-Path $OutDir $name
    New-Item -ItemType Directory -Force $dir | Out-Null
    $spy = Join-Path $dir "audiospy.txt"
    $envs = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_AUDIOSPY = $spy } + $d.env
    $saved = @{}; foreach ($k in $envs.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process"); [Environment]::SetEnvironmentVariable($k, [string]$envs[$k], "Process") }
    $p = [pscustomobject]@{ ExitCode = 0 }
    try { if (-not $AnalyzeOnly) { $p = Start-Process -FilePath $Exe -WorkingDirectory $dir -NoNewWindow -Wait -PassThru -RedirectStandardOutput (Join-Path $dir "run.log") -RedirectStandardError (Join-Path $dir "run.err") } }
    finally { foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], "Process") } }
    # ---- parse ----
    $snd = @{}; $L = New-Object System.Collections.Generic.List[object]; $events = New-Object System.Collections.Generic.List[object]
    $voices = @{}
    foreach ($ln in [IO.File]::ReadLines($spy)) {
        $q = $ln.Split(" ")
        switch ($q[0]) {
            "L" { $snd[[int]$q[2]] = @{ dur = [double]$q[3]; path = ($q[4..($q.Length - 1)] -join " ") } }
            "H" { $L.Add(@{ t = [double]$q[1]; p = @([double]$q[2], [double]$q[3], [double]$q[4]) }) }
            "A" { $events.Add(@{ kind = "oneshot"; t = [double]$q[1]; s = [int]$q[2]; p = @([double]$q[3], [double]$q[4], [double]$q[5]) }) }
            "V" { $voices[[int]$q[2]] = @{ kind = "voice"; t = [double]$q[1]; s = [int]$q[3]; pos = [int]$q[4]; loop = [int]$q[5]; p = @([double]$q[6], [double]$q[7], [double]$q[8]); upd = New-Object System.Collections.Generic.List[object]; stop = $null } }
            "U" { $v = $voices[[int]$q[2]]; if ($v) { $v.upd.Add(@{ t = [double]$q[1]; p = @([double]$q[3], [double]$q[4], [double]$q[5]) }) } }
            "S" { $v = $voices[[int]$q[2]]; if ($v) { $v.stop = [double]$q[1] } }
        }
    }
    $lis = $L.ToArray()
    function ListenerAt([double]$t) { $best = $lis[0]; foreach ($h in $lis) { if ($h.t -le $t) { $best = $h } else { break } }; return $best.p }
    function Dist($a, $b) { [Math]::Sqrt(($a[0] - $b[0]) * ($a[0] - $b[0]) + ($a[1] - $b[1]) * ($a[1] - $b[1]) + ($a[2] - $b[2]) * ($a[2] - $b[2])) }
    $tEnd = if ($lis.Count) { $lis[-1].t } else { 0 }
    # Owner proxy = listener (camera follows the pawn at a near-constant offset). A sound is LEFT
    # BEHIND when it is positional, never moves (playAt one-shot, or a voice without updateVoice),
    # lasts >= 0.5 s and its owner travels >= 2 m from the trigger point while it plays. Magnitude =
    # the owner's maximum displacement from the trigger point during playback (path-independent).
    function MaxDisp([double]$t0, [double]$t1) {
        $p0 = ListenerAt $t0; $m = 0
        foreach ($h in $lis) { if ($h.t -lt $t0) { continue }; if ($h.t -gt $t1) { break }; $d = Dist $h.p $p0; if ($d -gt $m) { $m = $d } }
        return $m
    }
    $agg = [ordered]@{}
    function Note($key, $behind, $moved, $dur, $kind, $updates) {
        if (-not $agg.Contains($key)) { $agg[$key] = @{ n = 0; behind = 0; maxMoved = 0; dur = $dur; kind = $kind; upd = 0 } }
        $a = $agg[$key]; $a.n++; $a.upd += $updates
        if ($behind) { $a.behind++ }
        if ($moved -gt $a.maxMoved) { $a.maxMoved = $moved }
    }
    foreach ($e in $events) {
        $info = $snd[$e.s]; $dur = if ($info) { $info.dur } else { -1 }; $nm = if ($info) { Split-Path $info.path -Leaf } else { "sound$($e.s)" }
        if ($dur -lt 0.5) { continue }                            # short impulses may stay where triggered
        $moved = MaxDisp $e.t ([Math]::Min($tEnd, $e.t + $dur))
        Note "$name.oneshot.$nm" ($moved -ge 2.0) $moved $dur "playAt one-shot (fixed by API)" 0
    }
    foreach ($v in $voices.Values) {
        if (-not $v.pos) { continue }
        $info = $snd[$v.s]; $nm = if ($info) { Split-Path $info.path -Leaf } else { "sound$($v.s)" }
        $dur = if ($info) { $info.dur } else { -1 }
        $end = if ($v.stop) { $v.stop } elseif ($v.loop) { $tEnd } else { [Math]::Min($tEnd, $v.t + [Math]::Max($dur, 0)) }
        if (($end - $v.t) -lt 0.5) { continue }
        $moved = MaxDisp $v.t $end
        $kind = if ($v.loop) { "looping voice" } else { "voice" }
        Note "$name.voice.$nm" (($v.upd.Count -eq 0) -and ($moved -ge 2.0)) $moved $(if ($v.loop) { -1 } else { $dur }) $kind $v.upd.Count
    }
    foreach ($k in $agg.Keys) {
        $a = $agg[$k]
        $len = if ($a.dur -lt 0) { "loop" } else { "{0:F2} s" -f $a.dur }
        if ($a.behind -gt 0) {
            Add $k "KNOWN" $a.maxMoved ("{0} ({1}) x{2}: stays at its trigger point; owner moved up to {3:F1} m during playback ({4} of {2} instances, 0 position updates)" -f $a.kind, $len, $a.n, $a.maxMoved, $a.behind) "Systems"
            $find.Add(("{0} : {1} ({2}) left at its trigger point, owner up to {3:F1} m away" -f $k, $a.kind, $len, $a.maxMoved))
        } elseif ($a.upd -gt 0) {
            Add $k "PASS" $a.upd ("{0} ({1}) follows its owner: {2} position updates over {3} instance(s)" -f $a.kind, $len, $a.upd, $a.n)
        } else {
            Add $k "INFO" $a.maxMoved ("{0} ({1}) never updated, but the owner moved < 2 m while it played - inconclusive" -f $a.kind, $len)
        }
    }
    Add "$name.ran" ($(if ($p.ExitCode -eq 0) { "PASS" } else { "FAIL" })) $p.ExitCode ("{0} 3D one-shots, {1} voices, {2} listener samples" -f $events.Count, $voices.Count, $lis.Count)
}
$sum = [ordered]@{ pass = @($results | Where-Object status -eq PASS).Count; fail = @($results | Where-Object status -eq FAIL).Count
                   known = @($results | Where-Object status -eq KNOWN).Count; info = @($results | Where-Object status -eq INFO).Count; skip = 0 }
[ordered]@{ summary = $sum; results = $results } | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutDir "report.json") -Encoding UTF8
$find | Set-Content (Join-Path $OutDir "findings.txt")
$results | Format-Table status, id, measured, note -AutoSize | Out-String -Width 280
"AUDIO SUMMARY: $($sum.pass) pass, $($sum.fail) FAIL, $($sum.known) known, $($sum.info) info"
