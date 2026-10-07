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
# When the exe supports the "ui:<Action>" script step (Frontend 76b8287: logical UI commands through platform::UiBindings,
# the same path as the physical keyboard / pad), the menu paths use it; otherwise "key:<flash code>" into the movies.
$script:WfcUseUiActions = $false
function Set-WfcInputMode($Hooks, [string]$Exe) { $script:WfcUseUiActions = $false; try { $txt = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Exe)); $script:WfcUseUiActions = $txt.Contains("script.ui") } catch { }; return $script:WfcUseUiActions }
$script:FlashToUi = @{ 13 = "Accept"; 27 = "Back"; 114 = "Start"; 37 = "Left"; 38 = "Up"; 39 = "Right"; 40 = "Down"; 112 = "X"; 113 = "Y"; 115 = "Select" }
function Keys([int[]]$codes, [double]$gap = 0.5) { return (($codes | ForEach-Object { if ($script:WfcUseUiActions -and $script:FlashToUi.ContainsKey($_)) { "ui:$($script:FlashToUi[$_]);wait:t=$gap" } else { "key:$_;wait:t=$gap" } }) -join ";") }
function K([int]$code) { if ($script:WfcUseUiActions -and $script:FlashToUi.ContainsKey($code)) { return "ui:$($script:FlashToUi[$code])" } else { return "key:$code" } }
function Path-ToHostOptions([int]$modeIndex = 0) {
    $s = "wait:frontend;wait:ui=FrontEnd;wait:t=1.5;" + (K 114) + ";wait:t=1.2;" + (Keys @(40)) + ";" + (K 13) + ";wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;" + (Keys @(40)) + ";" + (K 13) + ";wait:t=1.2"
    for ($i = 0; $i -lt $modeIndex; $i++) { $s += ";" + (Keys @(40) 0.6) }
    return $s + ";" + (K 13) + ";wait:t=1.5"
}
# From the host options (focus on Create Game): optionally Time Limit one step left (15 -> 10 minutes), then Create Game.
function Path-CreateGame([switch]$TenMinutes) {
    $s = ""
    if ($TenMinutes) { $s = (Keys @(40, 40, 40) 0.4) + ";" + (Keys @(37) 0.6) + ";" + (Keys @(40, 40) 0.4) + ";" }
    return $s + (K 13) + ";wait:level=GameLobby;wait:ui=InLobby;wait:t=2.5"
}
# Game lobby: step the map selector n times right (wraps), back up to Start Game, start.
function Path-SelectMap([int]$steps) { if ($steps -le 0) { return "" }; return (Keys @(40) 0.6) + ";" + (Keys (@(39) * $steps) 0.9) + ";" + (Keys @(38) 0.6) }
function Path-StartGame { return (K 13) + ";wait:t=1" }

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

# Fraction of the frame covered by flat, untextured mid-grey: 8 px cells with low chroma and mid luminance whose 4
# neighbours have (nearly) the same colour. Large values = placeholder / missing-material geometry on screen.
function FlatGreyFraction([string]$bmp) {
    if (-not (Test-Path $bmp)) { return $null }
    $c = [WfcImage]::Rgb((Resolve-Path $bmp).Path, 8); $w = [int]$c[0]; $h = [int]$c[1]
    function px($x, $y) { $i = 2 + 3 * ($y * $w + $x); return @($c[$i], $c[$i + 1], $c[$i + 2]) }
    $n = 0; $flat = 0
    for ($y = 1; $y -lt $h - 1; $y++) { for ($x = 1; $x -lt $w - 1; $x++) {
        $n++; $p = px $x $y; $l = ($p[0] + $p[1] + $p[2]) / 3
        if ($l -lt 70 -or $l -gt 215 -or ([Math]::Max([Math]::Max($p[0], $p[1]), $p[2]) - [Math]::Min([Math]::Min($p[0], $p[1]), $p[2])) -gt 14) { continue }
        $same = $true; foreach ($q in @((px ($x - 1) $y), (px ($x + 1) $y), (px $x ($y - 1)), (px $x ($y + 1)))) { if ([Math]::Abs($q[0] - $p[0]) + [Math]::Abs($q[1] - $p[1]) + [Math]::Abs($q[2] - $p[2]) -gt 9) { $same = $false; break } }
        if ($same) { $flat++ }
    } }
    return [Math]::Round($flat / [Math]::Max(1, $n), 3)
}

# Frontend pass-4 routing (CONFIRMED original per Frontend: TnQuitMessageBox): Quit asks "Quit Game?" (Yes / No) and a
# match / game lobby quits to the PARTY LOBBY; Back from the party lobby (+ Yes) returns to the title. Detected from the
# build's source; scripts written for the old direct routing are rewritten so tomorrow's gate does not stall on the box.
# Park the real desktop cursor in the window corner before scripted menu input: a cursor over the window changes menu
# focus by hover (Integration M07: two runs picked the wrong item). Uses the frontend "mouse:x,y" step when the build has it.
$script:MouseParkCache = @{}
function Get-MousePark([string]$Root) {
    if (-not $Root) { return "wait:t=0" }
    if (-not $script:MouseParkCache.ContainsKey($Root)) { $script:MouseParkCache[$Root] = [bool](Get-ChildItem (Join-Path $Root "src\frontend") -Recurse -Include *.cpp -ErrorAction SilentlyContinue | Select-String -Pattern 'rfind("mouse:"' -SimpleMatch -List | Select-Object -First 1) }
    if ($script:MouseParkCache[$Root]) { return "mouse:2,2" } else { return "wait:t=0" }
}
function Test-QuitBox([string]$Root) { return [bool](Get-ChildItem (Join-Path $Root "src") -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue | Select-String -Pattern "TnQuitMessageBox" -SimpleMatch -List | Select-Object -First 1) }
function Convert-QuitRouting([string]$Script, [bool]$QuitBox) {
    if (-not $QuitBox -or -not $Script) { return $Script }
    $s = $Script.Replace("call:Game.QuitToMainMenu;wait:level=FrontEnd", "call:Game.QuitToMainMenu;wait:t=1.5;ui:Accept;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1.5;ui:Back;wait:t=1.5;ui:Accept;wait:level=FrontEnd")
    $s = $s.Replace("ui:Back;wait:t=1;wait:level=FrontEnd", "ui:Back;wait:t=1.5;ui:Accept;wait:level=FrontEnd")
    return $s
}

# Private Match Bot Settings for a run's profile. Writes BOTH key sets: BotsFriendly / BotsEnemy (8c2b6e3 and older; FFA uses
# BotsEnemy) and the per-faction BotsAutobot / BotsDecepticon that team modes read from 09c (Frontend; the old keys are ignored
# there - 2026-10-07 harness defect: zero bots). Convention: Autobot = $Friendly count, Decepticon = $Enemy count.
function Get-BotProfile([int]$Friendly, [int]$Enemy, [int]$Difficulty = 1, [switch]$Extended, [int]$Width = 1280, [int]$Height = 720) {
    return "[PCSettings]`nWidth=$Width`nHeight=$Height`nFullscreen=0`nVSync=0`nFrameLimit=0`nBotsFriendly=$Friendly`nBotsEnemy=$Enemy`nBotsAutobot=$Friendly`nBotsDecepticon=$Enemy`nBotDifficulty=$Difficulty`nBotsExtended=$(if ($Extended) { 1 } else { 0 })`n"
}
