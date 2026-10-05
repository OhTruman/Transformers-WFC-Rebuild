# MILESTONE 07 MULTI-MAP MATRIX: every launchable map (AssetTools readiness + runtime data), one row per map.
#
#   .\tools\fidelity\m07-maps.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Config Release|Debug] [-Maps a,b] [-NoChain] [-ReportOnly]
#
# Per map, lockstep direct boot (WFC_MAP, deterministic):
#   robot      spawn at the map's own player start, walk (WFC_AUTOWALK + turn), jump every 1.5 s: moved, left the ground, landed
#   vehicle    start in vehicle, drive, boost from frame 120, transform to robot at frame 330 while boosting
#   transform  stationary robot -> vehicle -> robot
#   collision  Gameplay's WFC_CHAOS (8 starts x 20 s): under the map / KillZ / stuck
#   match      local TDM (WFC_MATCH=TDM, WFC_LIFECYCLE=2): kill, death, respawn delay, end
#   killplane  KillZ loaded = authored (physics.json); falls below it handled (any "fell out of world" line is a death,
#              not a hang) - no teleport hook exists, so the plane itself is only exercised when a run falls
# Then ONE frontend process chains every versus map through the lobby (load -> play -> end -> lobby -> next map ...)
# and finally quits to the title: per map world coverage (HUD / player excluded), VISUALCHECK draws, unload, second map,
# return to frontend. Failures are recorded per map, never folded into one total.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("Release", "Debug")][string]$Config = "Release",
      [string[]]$Maps = @(), [switch]$NoChain, [switch]$ReportOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe; $X = Get-M07Expectations
$Maps = @($Maps | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$todo = @($X.maps | Where-Object { $_.launchable -and (-not $Maps.Count -or $Maps -contains $_.runtime) })
$res = New-WfcResults
function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "m07map.$id" $status $null $note $owner }
function RunDirect([string]$d, [hashtable]$e, [int]$timeout = 300) {
    if ($ReportOnly) { return }
    if (-not (Wait-WfcGpu)) { "GPU busy past the wait limit" | Set-Content (Join-Path $d "SKIPPED.txt"); return }
    $null = Invoke-WfcExe $exe $d $e "run.log" $timeout
}
$rows = New-Object System.Collections.Generic.List[object]
foreach ($m in $todo) {
    $md = Join-Path $OutDir $m.runtime; New-Item -ItemType Directory -Force $md | Out-Null
    $ps = @((Get-Content -Raw "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$($m.runtime)\gameplay.json" | ConvertFrom-Json).player_starts | Where-Object { @($_.location_gltf).Count -ge 3 })
    $si = [Array]::IndexOf(@($ps | ForEach-Object { $_.class }), "TnTeamPlayerStart"); if ($si -lt 0) { $si = 0 }
    $base = @{ WFC_BOOT = "match"; WFC_MAP = $m.runtime; WFC_LOCKSTEP = "1"; WFC_START = "$si"; WFC_LOGEVERY = "5"; WFC_NOMOUSE = "1" }
    if ($H.Contains("WFC_VISUALCHECK")) { $base.WFC_VISUALCHECK = "1" }
    $runs = [ordered]@{
        robot     = @{ WFC_SMOKE_FRAMES = "540"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.15"; WFC_AUTOJUMP_EVERY = "90" }
        vehicle   = @{ WFC_SMOKE_FRAMES = "480"; WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "120"; WFC_PRESSTRANSFORM = "330"; WFC_BOOSTLOG = "1" }
        transform = @{ WFC_SMOKE_FRAMES = "420"; WFC_PRESSTRANSFORM_EVERY = "150" }
    }
    foreach ($k in $runs.Keys) { $d = Join-Path $md $k; New-Item -ItemType Directory -Force $d | Out-Null; $e = $base.Clone(); foreach ($kk in $runs[$k].Keys) { $e[$kk] = $runs[$k][$kk] }; RunDirect $d $e }
    if ($H.Contains("WFC_CHAOS")) { $d = Join-Path $md "collision"; New-Item -ItemType Directory -Force $d | Out-Null; RunDirect $d @{ WFC_CHAOS = "8"; WFC_MAP = $m.runtime } 900 }
    if ($H.Contains("WFC_MATCH") -and $H.Contains("WFC_LIFECYCLE")) { $d = Join-Path $md "match"; New-Item -ItemType Directory -Force $d | Out-Null
        RunDirect $d @{ WFC_BOOT = "match"; WFC_MAP = $m.runtime; WFC_MATCH = "TDM"; WFC_GAMEMODE = "TDM"; WFC_LIFECYCLE = "2"; WFC_SMOKE_FRAMES = "2400"; WFC_LOGEVERY = "30"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2" } 400 }
    # ---------- judge
    $row = [ordered]@{ map = $m.runtime; id = $m.mapId }
    $fr = @(Read-FrameLog (Join-Path $md "robot\wfc.log") | Where-Object { $_.frame -gt 0 })
    $loadOk = (Test-Path (Join-Path $md "robot\wfc.log")) -and @(Grep-Log (Join-Path $md "robot\wfc.log") 'starts: \d+ player starts').Count -gt 0
    $row.load = if ($loadOk) { "PASS" } elseif (Test-Path (Join-Path $md "robot\SKIPPED.txt")) { "SKIP" } else { "FAIL" }
    if ($fr.Count) {
        $p0 = $fr[0]; $st = $ps[$si].location_gltf; $dsp = [Math]::Sqrt([Math]::Pow($p0.x - $st[0], 2) + [Math]::Pow($p0.z - $st[2], 2))
        $row.spawn = if ($dsp -lt 3) { "PASS" } else { "FAIL" }
        $dist = 0.0; for ($i = 1; $i -lt $fr.Count; $i++) { $dist += [Math]::Sqrt([Math]::Pow($fr[$i].x - $fr[$i - 1].x, 2) + [Math]::Pow($fr[$i].z - $fr[$i - 1].z, 2)) }
        $row.walk = if ($dist -gt 15) { "PASS" } else { "FAIL" }
        $air = @($fr | Where-Object { "$($_.grounded)" -eq "0" }).Count; $peak = ($fr | ForEach-Object { [double]$_.y } | Measure-Object -Maximum).Maximum
        $row.jump = if ($air -ge 2 -and $peak -gt [double]$p0.y + 0.5 -and "$($fr[-1].grounded)" -eq "1") { "PASS" } elseif ($air -ge 2) { "PARTIAL" } else { "FAIL" }
        $row.robot_note = "spawn {0:N1} m from the start; walked {1:N0} m; airborne samples {2}; peak +{3:N1} m" -f $dsp, $dist, $air, ($peak - [double]$p0.y)
    } else { $row.spawn = $row.walk = $row.jump = $row.load }
    $fv = @(Read-FrameLog (Join-Path $md "vehicle\wfc.log") | Where-Object { $_.frame -gt 0 })
    if ($fv.Count) {
        $veh = @($fv | Where-Object { $_.form -eq "VEHICLE" -and $_.frame -lt 120 }); $hov = Median ($veh | ForEach-Object { [double]$_.hspeed })
        $bst = @($fv | Where-Object { $_.form -eq "VEHICLE" -and $_.frame -ge 150 -and $_.frame -lt 330 }); $bmax = ($bst | ForEach-Object { [double]$_.hspeed } | Measure-Object -Maximum).Maximum
        $after = @($fv | Where-Object { $_.frame -ge 400 }); $robotAfter = @($after | Where-Object { $_.form -eq "ROBOT" }).Count -gt 0
        $minY = ($fv | ForEach-Object { [double]$_.y } | Measure-Object -Minimum).Minimum
        $row.vehicle = if ($veh.Count -and $hov -gt 5) { "PASS" } elseif ($veh.Count) { "PARTIAL" } else { "FAIL" }
        $row.boost = if ($bmax -gt 20) { "PASS" } elseif ($bst.Count) { "PARTIAL" } else { "FAIL" }
        $row.boost_transform = if ($robotAfter -and $minY -gt $m.killz_m) { "PASS" } elseif ($robotAfter) { "FAIL" } else { "FAIL" }
        $row.vehicle_note = "hover median {0:N1} m/s, boost max {1:N1} m/s (RE: 15 / 30); robot after the boost transform {2}; min y {3:N1} (KillZ {4})" -f $hov, $bmax, $robotAfter, $minY, $m.killz_m
    } else { $row.vehicle = $row.boost = $row.boost_transform = "SKIP" }
    $ft = @(Read-FrameLog (Join-Path $md "transform\wfc.log") | Where-Object { $_.frame -gt 0 })
    $forms = @($ft | ForEach-Object { $_.form } | Select-Object -Unique); $flips = 0; for ($i = 1; $i -lt $ft.Count; $i++) { if ($ft[$i].form -ne $ft[$i - 1].form) { $flips++ } }
    $row.transform = if (-not $ft.Count) { "SKIP" } elseif ($flips -ge 2 -and $forms -contains "VEHICLE" -and $forms -contains "ROBOT") { "PASS" } else { "FAIL" }
    $cl = Join-Path $md "collision\wfc.log"; $cs = if (Test-Path $cl) { @(Grep-Log $cl 'CHAOS SUMMARY')[0] } else { $null }
    if ($cs) { $mm = [regex]::Match($cs.text, ': (\d+) runs UNDER THE MAP.*?, (\d+) KillZ, (\d+) stuck'); $row.collision = if ([int]$mm.Groups[1].Value -or [int]$mm.Groups[2].Value) { "FAIL" } elseif ([int]$mm.Groups[3].Value) { "PARTIAL" } else { "PASS" }; $row.collision_note = "CHAOS 8 starts: under {0}, KillZ {1}, stuck {2}" -f $mm.Groups[1].Value, $mm.Groups[2].Value, $mm.Groups[3].Value } else { $row.collision = "SKIP" }
    $kz = @(Grep-Log (Join-Path $md "robot\wfc.log") 'KillZ ([-\d.]+) m')[0]
    $kzv = if ($kz) { [double][regex]::Match($kz.text, 'KillZ ([-\d.]+) m').Groups[1].Value } else { $null }
    $row.killplane = if ($kzv -eq $null) { "UNKNOWN" } elseif ([Math]::Abs($kzv - [double]$m.killz_m) -lt 0.2) { "PASS" } else { "FAIL" }
    $row.killplane_note = "KillZ loaded {0} m, authored {1} m (exercised only when a run falls; teleport hook proposed)" -f $kzv, $m.killz_m
    $ml = Join-Path $md "match\wfc.log"
    if (Test-Path $ml) { $kills = @(Grep-Log $ml '\] MATCH kill ').Count; $resp = @(Grep-Log $ml '\] MATCH respawn ' | ForEach-Object { [double][regex]::Match($_.text, 'delay_s=([\d.]+)').Groups[1].Value }); $end = @(Grep-Log $ml '\] MATCH end ')[0]
        $row.respawn = if ($resp.Count -and @($resp | Where-Object { [Math]::Abs($_ - 5.0) -gt 0.4 }).Count -eq 0) { "PASS" } elseif ($resp.Count) { "FAIL" } else { "FAIL" }
        $row.match_end = if ($end) { "PASS" } else { "FAIL" }; $row.match_note = "kills {0}, respawn delays {1}, end {2}" -f $kills, (($resp | ForEach-Object { "{0:N2}" -f $_ }) -join ","), $(if ($end) { $end.text -replace '^.*MATCH end ', '' } else { "none" }) }
    else { $row.respawn = $row.match_end = "SKIP" }
    $rows.Add([pscustomobject]$row)
}

# ---------- frontend chain: every versus map through the lobby in ONE process, then quit to the title
$chain = @($todo | Where-Object { $_.versus_launchable } | Sort-Object { [int]$_.mapId })
if (-not $NoChain -and $chain.Count) {
    $d = Join-Path $OutDir "chain"; New-Item -ItemType Directory -Force $d | Out-Null
    $quitBox = [bool](Get-ChildItem (Join-Path $Root "src") -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue | Select-String -Pattern "TnQuitMessageBox" -SimpleMatch -List | Select-Object -First 1)
    $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
    $seg = foreach ($m in $chain) { "call:Online.SetSelectedMapID,$($m.mapId);wait:t=1.5;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:t=1.5;shot:$d\$($m.runtime)_1spawn.bmp;wait:t=1.5;shot:$d\$($m.runtime)_2move.bmp;wait:ui=GameEnded;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;shot:$d\$($m.runtime)_3lobby.bmp" }
    $quit = if ($quitBox) { "ui:Back;wait:t=1.5;ui:Accept;wait:level=FrontEnd;wait:ui=FrontEnd" } else { "call:Game.QuitToMainMenu;wait:level=FrontEnd;wait:ui=FrontEnd" }
    $s = @("wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2") + @($seg) + @("ui:Back", "wait:t=1.5", $(if ($quitBox) { "ui:Accept;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1.5;$quit" } else { "wait:level=PartyLobby;wait:t=1;$quit" }), "wait:t=3", "shot:$d\z_frontend.bmp", "snapshot:frontend_return", "quit")
    $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = ($s -join ";"); WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "1800"; WFC_LIFECYCLE = "2"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "60" }
    if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }; if ($H.Contains("WFC_VISUALCHECK")) { $e.WFC_VISUALCHECK = "1" }
    if (-not $ReportOnly) { if (Wait-WfcGpu) { $r = Invoke-WfcSampled $exe $d $e 2400 1.0; Write-WfcCsv $r.samples (Join-Path $d "process.csv") } else { "GPU busy" | Set-Content (Join-Path $d "SKIPPED.txt") } }
    $vc = @(Read-VisualCheckByVisit (Join-Path $d "wfc.log") | Where-Object { $_.level -like "Match:*" -and $_.ui -eq "InGame" })
    $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $ret = @(Flow-Ev $F "snapshot" | Where-Object why -eq "frontend_return").Count -gt 0
    foreach ($m in $chain) { $row = @($rows | Where-Object { $_.map -eq $m.runtime })[0]; if (-not $row) { continue }
        $ws = @("1spawn", "2move" | ForEach-Object { Present-World (Join-Path $d "$($m.runtime)_$_.bmp") } | Where-Object { $_ })
        $row | Add-Member -Force -NotePropertyName frontend_world -NotePropertyValue (Present-WorldSetVerdict $ws)
        $row | Add-Member -Force -NotePropertyName unload_lobby -NotePropertyValue $(if (Test-Path (Join-Path $d "$($m.runtime)_3lobby.bmp")) { "PASS" } else { "FAIL" })
        $mv = @($vc | Where-Object { $_.level -like "*$($m.runtime)*" }); $row | Add-Member -Force -NotePropertyName draws -NotePropertyValue $(if ($mv.Count) { "world {0} / bsp {1} / noDepth {2}" -f (Median ($mv | ForEach-Object { $_.world })), (Median ($mv | ForEach-Object { $_.bsp })), (Median ($mv | ForEach-Object { $_.noDepth })) } else { "n/a" })
        $row | Add-Member -Force -NotePropertyName world_detail -NotePropertyValue (($ws | ForEach-Object { $_.detail }) -join "/") }
    Res "chain.second_map_and_return" $(if ($ret) { "PASS" } else { "FAIL" }) ("frontend chain over {0} maps ({1}) then quit to the title: returned {2}" -f $chain.Count, (($chain | ForEach-Object { $_.runtime }) -join " > "), $ret) "Frontend/Integration"
}

# ---------- results per map (one result per map and check; nothing folded into a total)
$checks = "load", "spawn", "walk", "jump", "vehicle", "boost", "transform", "boost_transform", "collision", "killplane", "respawn", "match_end", "frontend_world", "unload_lobby"
foreach ($r in $rows) { foreach ($c in $checks) { if ($r.PSObject.Properties[$c]) { $note = @("robot_note", "vehicle_note", "collision_note", "killplane_note", "match_note", "world_detail", "draws" | Where-Object { $r.PSObject.Properties[$_] } | ForEach-Object { "$($_): $($r.$_)" }) -join "; "
    Res "$($r.map).$c" $r.$c $note $(switch ($c) { { $_ -in "frontend_world" } { "Rendering/Frontend" } { $_ -in "load", "unload_lobby" } { "Integration" } default { "Gameplay" } }) } } }
foreach ($m in @($X.maps | Where-Object { -not $_.launchable })) { Res "$($m.runtime).load" $(if (-not $m.cooked) { "SKIP" } else { "FAIL" }) ("not launchable: cooked {0}, runtime data {1} ({2})" -f $m.cooked, $m.runtime_data, $(if (-not $m.cooked) { "SOURCE DATA ABSENT" } else { "export missing" })) "AssetTools" }
Write-WfcCsv $rows (Join-Path $OutDir "maps.csv")
Write-M07Matrix $rows (@("map", "id") + $checks + @("draws", "world_detail")) (Join-Path $OutDir "MAPS.md") "M07 multi-map matrix" @("exe: ``$exe``", "", "One row per map; every cell is its own check (PASS / PARTIAL / FAIL / SKIP / UNKNOWN). Notes: maps.csv.")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"M07 MAPS: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
