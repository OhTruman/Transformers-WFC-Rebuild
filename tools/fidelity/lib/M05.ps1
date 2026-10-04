# Milestone 05 gate helpers (dot-source after lib\Run.ps1 and lib\Flow.ps1).

# WFC_* hooks compiled into an executable (ASCII scan of the binary). Lets the gate SKIP a check whose hook the build
# under test does not have instead of misreading its absence as product behaviour.
function Get-ExeHooks([string]$Exe) {
    $bytes = [IO.File]::ReadAllBytes($Exe)
    $txt = [Text.Encoding]::ASCII.GetString($bytes)
    $set = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($m in [regex]::Matches($txt, 'WFC_[A-Z0-9_]{2,40}')) { [void]$set.Add($m.Value) }
    return , $set
}

# Real-input menu paths (keys reach the shipped movies' own ActionScript through the frontend key hook).
# Flash key codes (docs/FRONTEND.md): 13 Enter = A, 27 Esc = B, 114 F3 = Start, 37/38/39/40 arrows.
# Verified on agents/frontend 08ef880: Start -> main menu (Campaign focused) -> Down -> Multiplayer -> party lobby
# (Find Match focused) -> Down -> Private Match -> mode list (TDM focused, EditGameMode on focus) -> host options
# (focus on Create Game; rows Team Balancing / Map Selection / Time Limit / Points to Win; Down from Create Game
# wraps to the first row) -> Create Game -> game lobby (Start Game focused, Select Map below).
function Keys([int[]]$codes, [double]$gap = 0.5) { return (($codes | ForEach-Object { "key:$_;wait:t=$gap" }) -join ";") }
function Path-ToHostOptions([int]$modeIndex = 0) {
    $s = "wait:frontend;wait:ui=FrontEnd;wait:t=1.5;key:114;wait:t=1.2;" + (Keys @(40)) + ";key:13;wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;" + (Keys @(40)) + ";key:13;wait:t=1.2"
    for ($i = 0; $i -lt $modeIndex; $i++) { $s += ";" + (Keys @(40) 0.6) }
    return $s + ";key:13;wait:t=1.5"
}
# From the host options (focus on Create Game): optionally Time Limit one step left (15 -> 10 minutes), then Create Game.
function Path-CreateGame([switch]$TenMinutes) {
    $s = ""
    if ($TenMinutes) { $s = (Keys @(40, 40, 40) 0.4) + ";" + (Keys @(37) 0.6) + ";" + (Keys @(40, 40) 0.4) + ";" }
    return $s + "key:13;wait:level=GameLobby;wait:ui=InLobby;wait:t=2.5"
}
# Game lobby: step the map selector n times right (wraps), back up to Start Game, start.
function Path-SelectMap([int]$steps) { if ($steps -le 0) { return "" }; return (Keys @(40) 0.6) + ";" + (Keys (@(39) * $steps) 0.9) + ";" + (Keys @(38) 0.6) }
function Path-StartGame { return "key:13;wait:t=1" }

# Shot step (product-side capture of the composed frame: 3D + GFx + Bink; BMP).
function Shot([string]$dir, [string]$name) { return "shot:" + (Join-Path $dir "$name.bmp") }

# Image judgement for a product shot: @{ mean; black; flat; ok }
function Shot-Stats([string]$bmp) {
    if (-not (Test-Path $bmp)) { return $null }
    $l = [WfcImage]::Luma((Resolve-Path $bmp).Path, 8); $s = [WfcImage]::Stats($l, 6.0)
    return [pscustomobject]@{ file = (Split-Path $bmp -Leaf); mean = [Math]::Round($s[0], 1); black = [Math]::Round($s[1], 3); flat = [Math]::Round($s[2], 3) }
}
function Shot-Diff([string]$a, [string]$b) {
    if (-not ((Test-Path $a) -and (Test-Path $b))) { return -1 }
    $la = [WfcImage]::Luma((Resolve-Path $a).Path, 8); $lb = [WfcImage]::Luma((Resolve-Path $b).Path, 8)
    $d = 0.0; $n = [Math]::Min($la.Length, $lb.Length); for ($i = 2; $i -lt $n; $i++) { $d += [Math]::Abs($la[$i] - $lb[$i]) }
    return [Math]::Round($d / [Math]::Max(1, $n - 2), 2)
}

# AMB lines (Systems): one object per sample with the fields the lifecycle checks use.
function Read-Amb([string]$WfcLog) {
    $out = New-Object System.Collections.Generic.List[object]
    if (-not (Test-Path $WfcLog)) { return }
    $i = 0
    foreach ($ln in [IO.File]::ReadLines((Resolve-Path $WfcLog).Path)) {
        $i++
        if ($ln -notmatch '\] AMB zone=') { continue }
        $o = [ordered]@{ i = $i; live = $null; backendVoices = $null; script = $null; pools = $null; emitters = $null; cues = $null; pending = $null; voices = $null; pcm = $null; map = $null; musicState = $null; musicInstance = $null; zone = $null }
        foreach ($k in 'live', 'backendVoices', 'script', 'pools', 'emitters', 'cues', 'pending') { $m = [regex]::Match($ln, "\b$k=(-?\d+)"); if ($m.Success) { $o[$k] = [int]$m.Groups[1].Value } }
        $m = [regex]::Match($ln, '\bvoices=(-?\d+)'); if ($m.Success) { $o.voices = [int]$m.Groups[1].Value }
        $m = [regex]::Match($ln, '\bpcm=([\d.]+)MB'); if ($m.Success) { $o.pcm = [double]$m.Groups[1].Value }
        $m = [regex]::Match($ln, '\bmap=(\S+)'); if ($m.Success) { $o.map = $m.Groups[1].Value }
        $m = [regex]::Match($ln, '\bmusic=(-?\d+)/(-?\d+)'); if ($m.Success) { $o.musicState = [int]$m.Groups[1].Value; $o.musicInstance = [int]$m.Groups[2].Value }
        $m = [regex]::Match($ln, '\bzone=(\S+)'); if ($m.Success) { $o.zone = $m.Groups[1].Value }
        $out.Add([pscustomobject]$o)
    }
    return $out.ToArray()   # unrolled: callers wrap in @()
}
# Lines matching a regex, with their line index (to order them against FLOW lines in the same wfc.log).
function Grep-Log([string]$WfcLog, [string]$Pattern) {
    if (-not (Test-Path $WfcLog)) { return }
    $out = New-Object System.Collections.Generic.List[object]; $i = 0
    foreach ($ln in [IO.File]::ReadLines((Resolve-Path $WfcLog).Path)) { $i++; if ($ln -match $Pattern) { $out.Add([pscustomobject]@{ i = $i; text = $ln }) } }
    return $out.ToArray()   # unrolled: callers wrap in @()
}
# Index of the wfc.log line mirroring a flow event (FLOW <ev> ...), n-th occurrence (0-based); -1 if absent.
function Flow-LogIndex($runLog, [string]$ev, [int]$nth = 0) { $hits = @($runLog | Where-Object { $_.kind -eq "flow" -and $_.ev -eq $ev }); if ($hits.Count -gt $nth) { return $hits[$nth].i } else { return -1 } }

# Doubled sounds: CUE lines (WFC_CUELOG) for a fresh instance (t=0.000) with the same cue, wave, owner and position as
# the previous fresh-instance line = the same sound started twice in the same place at the same moment.
function Find-DoubledCues([string]$WfcLog) {
    $prev = $null; $dups = New-Object System.Collections.Generic.List[object]
    foreach ($c in (Grep-Log $WfcLog '\] CUE \S+ ev\d+ t=0\.000')) {
        $m = [regex]::Match($c.text, '\] CUE (\S+) ev(\d+) t=0\.000 wave=(\d+).*owner=(-?\d+).*pos=(\S+)')
        if (-not $m.Success) { continue }
        $key = "$($m.Groups[1].Value)|$($m.Groups[2].Value)|$($m.Groups[3].Value)|$($m.Groups[4].Value)|$($m.Groups[5].Value)"   # cue|event|wave|owner|pos: two events of one instance are layering, not a double
        if ($prev -and $prev.key -eq $key -and $c.i -eq $prev.i + 1) { $dups.Add([pscustomobject]@{ i = $c.i; cue = $m.Groups[1].Value; owner = $m.Groups[4].Value; pos = $m.Groups[5].Value }) }
        $prev = @{ key = $key; i = $c.i }
    }
    return $dups.ToArray()
}
# Maximum of a numeric property over objects, ignoring nulls; $null when none.
function MaxOf($rows, [string]$prop) { $v = @($rows | ForEach-Object { $_.$prop } | Where-Object { $_ -ne $null }); if (-not $v.Count) { return $null }; return ($v | Measure-Object -Maximum).Maximum }
