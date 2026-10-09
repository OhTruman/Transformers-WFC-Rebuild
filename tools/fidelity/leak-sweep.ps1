# LEAK SWEEP (tier TARGETED, untimed; user 2026-10-08: "every memory leak found and fixed"). Five scenarios, one process each,
# every series checked for a per-cycle slope (FAIL on growth that keeps going in the LATER half - a one-time warm-up step is
# not a leak). With Systems' WFC_ALLOCPROF live sampling and Rendering's WFC_TEXTRACE when the build has them.
#   maps      all 10 MP maps (501..510) x -Passes, 32 v 32 (WFC_LOBBY_OPTIONS), matches end on -MatchSeconds
#   modes     Streets, team modes TDM / CTF / DOM / KOTH round-robin (-ModeMatches), 10 v 10
#   long      ONE 32 v 32 match of -LongMinutes (PLAYERBOT), memory sampled through the match (within-match growth)
#   frontend  title -> party lobby -> Create a Character (enter, browse, back) -> title, -FrontendCycles times
#   scoreboard ONE 10 v 10 match, the scoreboard (ui:Select) toggled -ToggleCycles times
# Series per cycle: private MB (flow match.unloaded / nav.check privateMB), C++ heap live (ALLOCPROF "MEM: operator new live"
# nearest after the cycle marker), GL live names (match.glCensus textures / buffers / framebuffers / vertexArrays / programs,
# nav.check glTextures), CaC preview meshes / bodies (nav.check preview). Verdict per series on the later half of the cycles:
# FAIL if slope > -Tol units per cycle AND the later half rises by > 3 x Tol (MB series: -TolMb; count series: 0.5 per cycle).
#
#   .\tools\fidelity\leak-sweep.ps1 -Root work\ab\<target> -OutDir <dir> [-Scenarios maps,modes,long,frontend,scoreboard] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir,
      [string[]]$Scenarios = @("maps", "modes", "long", "frontend", "scoreboard"),
      [int[]]$MapIds = @(501, 502, 503, 504, 505, 506, 507, 508, 509, 510), [int]$Passes = 2, [int]$MatchSeconds = 60,
      [int]$ModeMatches = 12, [int]$LongMinutes = 20, [int]$FrontendCycles = 30, [int]$ToggleCycles = 60, [double]$TolMb = 5,
      [string]$ExtraEnv = "", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$Scenarios = @($Scenarios | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$MapIds = @($MapIds | ForEach-Object { "$_" -split ',' } | Where-Object { "$_".Trim() } | ForEach-Object { [int]"$_".Trim() })
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; $H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "leak.$id" $status $null $note $owner }
# diagnostics env: Systems' live heap sampling + Rendering's texture-site trace, when the build has them
$diag = @{}
if ($H.Contains("WFC_ALLOCPROF_LIVE")) { $diag.WFC_ALLOCPROF = "64"; $diag.WFC_ALLOCPROF_LIVE = "64"; if ($H.Contains("WFC_ALLOCPROF_EVERY_S")) { $diag.WFC_ALLOCPROF_EVERY_S = "3" } }
if ($H.Contains("WFC_TEXTRACE")) { $diag.WFC_TEXTRACE = "1" }
if ($H.Contains("WFC_GLTRACE")) { $diag.WFC_GLTRACE = "1" }   # Rendering: per-unload live counts of every GL object type + creation sites that grew
foreach ($kv in @($ExtraEnv -split ';' | Where-Object { $_ -match '=' })) { $i = $kv.IndexOf('='); $diag[$kv.Substring(0, $i).Trim()] = $kv.Substring($i + 1) }
$diagStr = (($diag.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ';')
Res "diagnostics" "INFO" ("build {0}; diagnostics env: {1}" -f $sha.Substring(0, [Math]::Min(7, $sha.Length)), $(if ($diagStr) { $diagStr } else { "none (build lacks ALLOCPROF_LIVE / TEXTRACE)" })) "Experimental"

function Slope([double[]]$y) { if ($y.Count -lt 2) { return 0 }; $mx = ($y.Count - 1) / 2.0; $my = ($y | Measure-Object -Average).Average; $num = 0.0; $den = 0.0
    for ($i = 0; $i -lt $y.Count; $i++) { $num += ($i - $mx) * ($y[$i] - $my); $den += ($i - $mx) * ($i - $mx) }; return [Math]::Round($num / $den, 3) }
# one series -> verdict on its later half (>= 6 points) or all points
function Judge([string]$id, [string]$what, [double[]]$y, [double]$tol, [string]$unit, [string]$owner) {
    if ($y.Count -lt 4) { Res $id "UNKNOWN" "$what - only $($y.Count) samples" $owner; return }
    $yv = if ($y.Count -ge 6) { @($y | Select-Object -Skip ([int][Math]::Floor($y.Count / 2))) } else { $y }
    $sl = Slope $yv; $rise = [Math]::Round($yv[-1] - $yv[0], 2)
    $st = if ($sl -gt $tol -and $rise -gt 3 * $tol) { "FAIL" } elseif ($sl -gt $tol / 2 -and $rise -gt 1.5 * $tol) { "PARTIAL" } else { "PASS" }
    $shown = if ($y.Count -le 24) { ($y | ForEach-Object { [Math]::Round($_, 1) }) -join " " } else { (($y | Select-Object -First 6 | ForEach-Object { [Math]::Round($_, 1) }) -join " ") + " ... " + (($y | Select-Object -Last 6 | ForEach-Object { [Math]::Round($_, 1) }) -join " ") }
    Res $id $st ("{0}: {1} cycles [{2}]; later half slope {3} {4}/cycle, rise {5} {4} (FAIL if slope > {6} and rise > {7})" -f $what, $y.Count, $shown, $sl, $unit, $rise, $tol, (3 * $tol)) $owner
}
# ALLOCPROF: "== context:" then "MEM: operator new live ... X MB; Windows heaps committed Y MB ...; committed private Z MB"
function MemSamples([string]$dir) {
    $f = Join-Path $dir "wfc_allocprof.txt"; if (-not (Test-Path $f)) { return ,@() }
    $out = New-Object System.Collections.Generic.List[object]; $ctx = ""
    foreach ($l in [IO.File]::ReadLines($f)) {
        if ($l.StartsWith("== context:")) { $ctx = $l.Substring(11).Trim(); continue }
        $m = [regex]::Match($l, 'MEM: operator new live \(all sizes\) ([\d.]+) MB; Windows heaps committed ([\d.]+) MB.*committed private ([\d.]+) MB')
        if ($m.Success) { $out.Add([pscustomobject]@{ newMb = [double]$m.Groups[1].Value; heapsMb = [double]$m.Groups[2].Value; privMb = [double]$m.Groups[3].Value; ctx = $ctx }) }
    }
    return ,$out.ToArray()
}
# GLTRACE / TEXTRACE "sites that grew" lines = a GL leak with its creation site (Rendering 2026-10-08)
function GrewCheck([string]$dir, [string]$tag) {
    $lg = Join-Path $dir "wfc.log"; if (-not (Test-Path $lg)) { return }
    $g = @(Select-String $lg -Pattern '(GLTRACE|TEXTRACE).*(grew|sites that grew)' -ErrorAction SilentlyContinue)
    $any = @(Select-String $lg -Pattern 'GLTRACE' -ErrorAction SilentlyContinue).Count
    if (-not $any -and -not $g.Count) { if ($diag.ContainsKey("WFC_GLTRACE")) { Res "$tag.gltrace" "UNKNOWN" "WFC_GLTRACE set but no GLTRACE lines logged" "Experimental" }; return }
    $ex = @($g | Select-Object -First 4 | ForEach-Object { $t = $_.Line -replace '^\[[^\]]*\]\s*', ''; $t.Substring(0, [Math]::Min(240, $t.Length)) })
    Res "$tag.gl_sites_grew" $(if ($g.Count) { "FAIL" } else { "PASS" }) $(if ($g.Count) { "{0} 'sites that grew' lines (GL objects not released at unload; symbolise with tools/render/gltrace_sym.py + the .map): {1}" -f $g.Count, ($ex -join " || ") } else { "no 'sites that grew' lines over $any GLTRACE / TEXTRACE reports" }) "Rendering"
}
function Run([string]$name, [hashtable]$e, [int]$timeoutS) {
    $d = Join-Path $OutDir $name; New-Item -ItemType Directory -Force $d | Out-Null
    if ($ReportOnly -or (Test-Path (Join-Path $d "wfc.log"))) { return $d }
    if (-not (Wait-WfcGpu)) { Res "$name.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; return $d }
    foreach ($k in $diag.Keys) { $e[$k] = $diag[$k] }
    $null = Invoke-WfcExe $exe $d $e "run.log" $timeoutS
    return $d
}
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
$lobbyIn = @((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1")
function FrontendEnv([string]$script, [string]$lobbyOpts, [string]$dir) {
    $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $script; WFC_FLOWLOG = (Join-Path $dir "flow.jsonl"); WFC_FLOW_TIMEOUT = "7200"
            WFC_SMOKE_FRAMES = "1000000000"; WFC_LOGEVERY = "0"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2" }
    if ($lobbyOpts) { $e.WFC_LOBBY_OPTIONS = $lobbyOpts }
    if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
    if ($H.Contains("WFC_FLOWSEED")) { $e.WFC_FLOWSEED = "1" }
    return $e
}
# per-match series from a match-cycling run: flow match.unloaded privateMB + the match.glCensus logged at each return
function MatchSeries([string]$dir, [string]$tag, [string]$owner) {
    $lg = Join-Path $dir "wfc.log"; $fl = Join-Path $dir "flow.jsonl"
    if (-not (Test-Path $fl)) { Res "$tag.ran" "UNKNOWN" "no flow trace (not run?)" "Experimental"; return }
    $F = Read-FlowLog $fl; $ul = @(Flow-Ev $F "match.unloaded"); $ld = @(Flow-Ev $F "match.loaded")
    Res "$tag.matches" $(if ($ul.Count -ge 4) { "INFO" } else { "FAIL" }) ("{0} matches loaded / {1} unloaded; maps: {2}" -f $ld.Count, $ul.Count, ((@($ld | ForEach-Object { $_.map -replace '^MP_', '' -replace '_Base_m$|_BASE_m$', '' })) -join ", ")) "Frontend"
    Judge "$tag.private_after_unload" "private MB back in the lobby after each match" @($ul | Select-Object -Skip 1 | ForEach-Object { [double]$_.privateMB }) $TolMb "MB" $owner
    $cen = @(foreach ($l in [IO.File]::ReadLines($lg)) { $m = [regex]::Match($l, 'match\.glCensus live=textures=(\d+) buffers=(\d+) framebuffers=(\d+) renderbuffers=(\d+) vertexArrays=(\d+) programs=(\d+)'); if ($m.Success) { ,@(1..6 | ForEach-Object { [double]$m.Groups[$_].Value }) } })
    $names = @("textures", "buffers", "framebuffers", "renderbuffers", "vertexArrays", "programs")
    for ($k = 0; $k -lt 6; $k++) { Judge "$tag.gl_$($names[$k])" "GL live $($names[$k]) after each unload" @($cen | ForEach-Object { $_[$k] }) 0.5 "names" "Rendering" }
    # C++ heap live in the lobby: the last ALLOCPROF sample before each next match load (context = lobby / flow lines)
    $ms = MemSamples $dir
    if ($ms.Count) {
        $lobby = @($ms | Where-Object { $_.ctx -match 'GameLobby|PartyLobby|match\.unloaded|unloadMapRenderData|scene\.view' })
        Judge "$tag.heap_new_lobby" "C++ heap (operator new live) at lobby samples" @($lobby | ForEach-Object { $_.newMb }) $TolMb "MB" "Systems"
    }
    GrewCheck $dir $tag
    $tt = @(Select-String $lg -Pattern 'TEXTRACE' -ErrorAction SilentlyContinue | Select-Object -Last 3 | ForEach-Object { ($_.Line -replace '^\[[^\]]*\]\s*', '').Substring(0, [Math]::Min(220, ($_.Line -replace '^\[[^\]]*\]\s*', '').Length)) })
    if ($tt.Count) { Res "$tag.textrace" "INFO" ("last TEXTRACE lines: " + ($tt -join " || ")) "Rendering" }
}
# nav.check series (frontend / scoreboard cycles): privateMB, glTextures, preview meshes / bodies per labelled check
function NavSeries([string]$dir, [string]$tag, [string]$prefix) {
    $lg = Join-Path $dir "wfc.log"; if (-not (Test-Path $lg)) { Res "$tag.ran" "UNKNOWN" "not run" "Experimental"; return }
    $rows = @(foreach ($l in [IO.File]::ReadLines($lg)) { if ($l.Contains("FLOW nav.check label=$prefix")) {
        $g = { param($k) $m = [regex]::Match($l, " $k=(\S+)"); if ($m.Success) { $m.Groups[1].Value } else { $null } }
        $pv = & $g "preview"; $pp = if ($pv -and $pv -ne '-') { $pv -split '/' } else { @() }
        [pscustomobject]@{ label = (& $g "label"); ui = (& $g "uiState"); priv = [double](& $g "privateMB"); glTex = $(if (& $g "glTextures") { [double](& $g "glTextures") }); meshes = $(if ($pp.Count -ge 4) { [double]$pp[3] }); bodies = $(if ($pp.Count -ge 5) { [double]$pp[4] }) } } })
    Res "$tag.cycles" $(if ($rows.Count -ge 4) { "INFO" } else { "FAIL" }) ("{0} cycle checks; UI states at the check: {1}" -f $rows.Count, ((@($rows | ForEach-Object { $_.ui }) | Select-Object -Unique) -join ", ")) "Frontend"
    Judge "$tag.private" "private MB at each cycle check" @($rows | Select-Object -Skip 1 | ForEach-Object { $_.priv }) $TolMb "MB" "Systems/Frontend"
    Judge "$tag.gl_textures" "GL live textures at each cycle check" @($rows | Select-Object -Skip 1 | Where-Object { $_.glTex -ne $null } | ForEach-Object { $_.glTex }) 0.5 "names" "Rendering/Frontend"
    $pm = @($rows | Select-Object -Skip 1 | Where-Object { $_.meshes -ne $null } | ForEach-Object { $_.meshes }); if ($pm.Count -ge 4) { Judge "$tag.preview_meshes" "CaC preview cached meshes" $pm 0.5 "meshes" "Frontend/Rendering" }
    $ms = MemSamples $dir
    if ($ms.Count) { $near = @($ms | Where-Object { $_.ctx -match "nav\.check label=$prefix" }); Judge "$tag.heap_new" "C++ heap (operator new live) at samples right after each check" @($near | Select-Object -Skip 1 | ForEach-Object { $_.newMb }) $TolMb "MB" "Systems" }
    GrewCheck $dir $tag
    Write-WfcCsv $rows (Join-Path $dir "cycles.csv")
}

# 1. all MP maps, 32 v 32, -Passes times round
if ($Scenarios -contains "maps") {
    $one = { param($id) "call:Online.SetSelectedMapID,$id;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:ui=GameEnded;wait:t=3;wait:level=GameLobby;wait:ui=InLobby;wait:t=3" }
    $steps = @($lobbyIn + @("call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5"))
    for ($p = 0; $p -lt $Passes; $p++) { foreach ($id in $MapIds) { $steps += (& $one $id) } }
    $d = Join-Path $OutDir "maps"; $n = $MapIds.Count * $Passes
    $d = Run "maps" (FrontendEnv (($steps + "quit") -join ";") "ExtendedPlayers=1;BotsAutobot=32;BotsDecepticon=32;PointsToWin=9999;TimeLimit=$MatchSeconds" $d) (600 + 240 * $n)
    MatchSeries $d "maps" "Gameplay/Rendering/Systems"
    # same map, first vs last visit (map sizes differ, so the sequence slope mixes maps; the revisit delta does not)
    $F = if (Test-Path (Join-Path $d "flow.jsonl")) { Read-FlowLog (Join-Path $d "flow.jsonl") } else { $null }
    if ($F) { $ld = @(Flow-Ev $F "match.loaded"); $ul = @(Flow-Ev $F "match.unloaded"); $dl = @()
        for ($i = 0; $i -lt [Math]::Min($ld.Count, $ul.Count); $i++) { $dl += [pscustomobject]@{ map = $ld[$i].map; mb = [double]$ul[$i].privateMB } }
        $rv = @($dl | Group-Object map | Where-Object { $_.Count -ge 2 } | ForEach-Object { [pscustomobject]@{ map = $_.Name; delta = [Math]::Round($_.Group[-1].mb - $_.Group[0].mb, 1) } })
        if ($rv.Count) { $mean = [Math]::Round(($rv | Measure-Object delta -Average).Average, 1); $tail = @($rv | Select-Object -Last 3); $tm = ($tail | Measure-Object delta -Maximum).Maximum
            Res "maps.revisit" $(if ($tm -gt 6 * $TolMb) { "FAIL" } elseif ($tm -gt 3 * $TolMb) { "PARTIAL" } else { "PASS" }) ("private MB after unload, same map first vs last visit: {0}; mean {1} MB; verdict on the last three maps (warm caches): max {2} MB" -f (($rv | ForEach-Object { "$($_.map -replace '^MP_','' -replace '_Base_m$|_BASE_m$','') $($_.delta)" }) -join "; "), $mean, $tm) "Gameplay/Rendering/Systems" } }
}
# 2. team modes round-robin on Streets, 10 v 10
if ($Scenarios -contains "modes") {
    $modes = @("TDM", "CTF", "DOM", "KOTH"); $steps = @($lobbyIn + @("call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5"))
    for ($i = 0; $i -lt $ModeMatches; $i++) { $md = $modes[$i % $modes.Count]
        $steps += "call:Online.EditGameMode,$md;wait:t=1;call:Online.SetSelectedMapID,508;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:ui=GameEnded;wait:t=3;wait:level=GameLobby;wait:ui=InLobby;wait:t=3" }
    $d = Join-Path $OutDir "modes"
    $d = Run "modes" (FrontendEnv (($steps + "quit") -join ";") "BotsAutobot=10;BotsDecepticon=10;PointsToWin=9999;TimeLimit=$MatchSeconds" $d) (600 + 240 * $ModeMatches)
    MatchSeries $d "modes" "Gameplay/Rendering/Systems"
    $lg = Join-Path $d "wfc.log"; if (Test-Path $lg) { $mp = @(Select-String $lg -Pattern '\] MATCH init mode=(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }); Res "modes.played" "INFO" ("modes played in order: " + ($mp -join ", ")) "Frontend" }
}
# 3. one long 32 v 32 match (PLAYERBOT), within-match growth from the ALLOCPROF samples (first 3 minutes = warm-up, excluded)
if ($Scenarios -contains "long") {
    $steps = @($lobbyIn + @("call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5",
             "call:Online.SetSelectedMapID,508", "wait:t=1", "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:ui=GameEnded", "wait:t=3", "quit"))
    $d = Join-Path $OutDir "long"; $e = FrontendEnv ($steps -join ";") "ExtendedPlayers=1;BotsAutobot=32;BotsDecepticon=32;PointsToWin=9999;TimeLimit=$($LongMinutes * 60)" $d
    if ($H.Contains("WFC_PLAYERBOT")) { $e.Remove("WFC_AUTOWALK"); $e.Remove("WFC_AUTOTURN"); $e.WFC_PLAYERBOT = "1" }
    if ($H.Contains("WFC_ALLOCPROF_EVERY_S")) { $diag.WFC_ALLOCPROF_EVERY_S = "15" }
    $d = Run "long" $e (900 + 60 * $LongMinutes)
    if ($H.Contains("WFC_ALLOCPROF_EVERY_S")) { $diag.WFC_ALLOCPROF_EVERY_S = "3" }
    $ms = MemSamples $d; $inm = @($ms | Where-Object { $_.ctx -notmatch 'GameLobby|PartyLobby|FrontEnd|scene\.view|loadMap|uploaded|render data' })
    if ($inm.Count -ge 8) { $per = 60.0 / 15; $skip = [int](3 * $per); $w = @($inm | Select-Object -Skip $skip)
        # per-minute series (mean of the samples in each minute) so the slope is MB / minute
        $mins = @(for ($i = 0; $i -lt $w.Count; $i += [int]$per) { $c = @($w[$i..([Math]::Min($w.Count - 1, $i + [int]$per - 1))]); [pscustomobject]@{ newMb = ($c | Measure-Object newMb -Average).Average; privMb = ($c | Measure-Object privMb -Average).Average } })
        Judge "long.heap_new_per_min" "C++ heap (operator new live) per in-match minute after 3 min" @($mins | ForEach-Object { $_.newMb }) $TolMb "MB" "Systems/Gameplay"
        Judge "long.private_per_min" "committed private per in-match minute after 3 min" @($mins | ForEach-Object { $_.privMb }) $TolMb "MB" "Systems/Gameplay" }
    else { Res "long.samples" "UNKNOWN" ("only {0} in-match ALLOCPROF samples (needs WFC_ALLOCPROF_LIVE + EVERY_S)" -f $inm.Count) "Experimental" }
}
# 4. frontend-only cycling: title -> party lobby -> Create a Character (enter, browse two items, back) -> title
if ($Scenarios -contains "frontend") {
    $steps = @((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2")
    for ($i = 1; $i -le $FrontendCycles; $i++) {
        $steps += @("call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:t=3", "clickclip:lobby_mc.menuAnchor_mc.menu_mc.customCharacters_mc", "wait:t=3", "ui:Accept", "wait:t=5",
                    "ui:Down", "wait:t=0.5", "ui:Down", "wait:t=0.5", "ui:Accept", "wait:t=3", "ui:Down", "wait:t=1.5", "ui:Back", "wait:t=2", "ui:Back", "wait:t=2", "ui:Back", "wait:t=2", "ui:Back", "wait:t=3",
                    "navcheck:fe$i", "wait:t=4") }
    $d = Join-Path $OutDir "frontend"; $d = Run "frontend" (FrontendEnv (($steps + "quit") -join ";") "" $d) (600 + 45 * $FrontendCycles)
    NavSeries $d "frontend" "fe"
}
# 5. scoreboard toggling inside one 10 v 10 match
if ($Scenarios -contains "scoreboard") {
    $tog = @(); for ($i = 1; $i -le $ToggleCycles; $i++) { $tog += @("ui:Select", "wait:t=1", "ui:Select", "wait:t=1"); if ($i % 5 -eq 0) { $tog += @("navcheck:sb$i", "wait:t=4") } }
    $steps = @($lobbyIn + @("call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5",
             "call:Online.SetSelectedMapID,508", "wait:t=1", "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:t=5") + $tog + @("quit"))
    $d = Join-Path $OutDir "scoreboard"; $d = Run "scoreboard" (FrontendEnv ($steps -join ";") "BotsAutobot=10;BotsDecepticon=10;PointsToWin=9999;TimeLimit=3600" $d) (900 + 4 * $ToggleCycles)
    NavSeries $d "scoreboard" "sb"
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"LEAK SWEEP ($($sha.Substring(0, [Math]::Min(7, $sha.Length)))): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | Where-Object { $_.status -ne "PASS" } | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
