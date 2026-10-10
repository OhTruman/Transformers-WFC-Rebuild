# LOCKSTEP RENDERER A/B (tier TARGETED; Integration 2026-10-10: free-running real-play A/Bs are dominated by match-to-match
# variance - one quiet match moved Gorge's p90 by 0.5 ms and its draw counts by 25 %). Every arm plays the SAME match: direct boot,
# WFC_LOCKSTEP=1 (exactly one fixed sim step per rendered frame), the same WFC_SEED / WFC_FLOWSEED, the same bots and (optionally)
# the bot-driven local player (WFC_PLAYERBOT). Frame times are compared over the same SIM STEP range, aligned through WFC_SIMHASH.
#
# Validity (per arm vs the first arm): the per-step SIMHASH sequence over the measured range must be identical (= the same match,
# frame for frame); and the standing parity check (Integration 2026-10-10): mean draws (total / dyn / fx) and chars must agree
# within 3 %, else the row is UNKNOWN (counts: draws / dyn / fx / program binds; chars CPU is reported, not gated) - a renderer A/B must not change the workload.
# Lockstep caveat: the sim cost per frame is a fixed step, not free-running play - use it for A/Bs, not for the absolute verdict.
#
#   .\tools\fidelity\lockstep-ab.ps1 -OutDir <dir> -Map 510 -Arms "new|work\ab\m9c_47726bc|","old|work\ab\m9c_47726bc|WFC_NOLMARRAYS=1"
#        [-Seed 1234] [-Steps 9000] [-From 1800] [-RenderSize 3840x2160] [-PlayerBot 1] [-Reps 1]
param([Parameter(Mandatory)][string]$OutDir, [Parameter(Mandatory)][string]$Map, [Parameter(Mandatory)][string[]]$Arms,
      [int]$Seed = 1234, [int]$Steps = 9000, [int]$From = 1800, [string]$RenderSize = "", [int]$PlayerBot = 1, [int]$Reps = 1,
      [int]$Bots = 32, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$Arms = @($Arms | ForEach-Object { "$_" -split ';;' } | Where-Object { $_.Trim() })
$levels = @{ "501" = "MP_IAC_Seed_Base_m"; "502" = "MP_IAC_Berth_Base_m"; "503" = "MP_UND_Complex_BASE_m"; "504" = "MP_IAC_Rust_BASE_m"
             "507" = "MP_ORB_Debris_BASE_m"; "508" = "MP_IAC_Streets_Base_m"; "509" = "MP_KON_Molten_Base_m"; "510" = "MP_UND_Gorge_BASE_m" }
$lvl = $levels["$Map"]; if (-not $lvl) { throw "unknown map $Map" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "lockstep.$id" $status $null $note $owner }
$url = "{0}?GameModeTag=TDM?BotsAutobot={1}?BotsDecepticon={2}?BotDifficulty=1?ExtendedPlayers=1?PointsToWin=9999?TimeLimit=3600" -f $lvl, ($Bots - 1), $Bots
$sfRe = 'interval ([\d.]+) ms: render ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)\), outside ([-\d.]+); gpu ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)(?:; [^)]*)?\); draws (\d+) \(dyn (\d+), fx (\d+)\), program binds (\d+)'
function Pct($a, $p) { if (-not $a.Count) { return [double]::NaN }; $s = @($a | Sort-Object); return $s[[Math]::Min($s.Count - 1, [int][Math]::Floor($p * $s.Count))] }
function Mean($a) { if (-not $a.Count) { return [double]::NaN }; return ($a | Measure-Object -Average).Average }
$rows = New-Object System.Collections.Generic.List[object]
for ($rep = 1; $rep -le $Reps; $rep++) {
    foreach ($arm in $Arms) {
        $p = $arm -split '\|', 3; $label = $p[0]; $root = (Resolve-Path $p[1]).Path; $extra = if ($p.Count -gt 2) { $p[2] } else { "" }
        $exe = Join-Path $root "build-release\bin\wfc_rebuild.exe"; $H = Get-ExeHooks $exe
        $d = Join-Path $OutDir ("{0}-r{1}" -f $label, $rep); $lg = Join-Path $d "wfc.log"
        if (-not $ReportOnly -and -not (Test-Path $lg)) {
            New-Item -ItemType Directory -Force $d | Out-Null
            (Get-BotProfile 0 0 1 -Width 1920 -Height 1080) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
            if (-not (Wait-WfcGpu)) { Res "$label.r$rep.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
            $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_LOCKSTEP = "1"; WFC_SEED = "$Seed"; WFC_SMOKE_FRAMES = "$($Steps + 600)"; WFC_LOGEVERY = "0"
                    WFC_NOMOUSE = "1"; WFC_SLOWFRAME = "0.01"; WFC_SIMHASH = "0" }   # every frame: 'interval' = wall frame time (PERF frame= is the lockstep dt)
            if ($H.Contains("WFC_FLOWSEED")) { $e.WFC_FLOWSEED = "$Seed" }
            if ($PlayerBot -ge 0 -and $H.Contains("WFC_PLAYERBOT")) { $e.WFC_PLAYERBOT = "$PlayerBot" }
            if ($RenderSize) { $e.WFC_RENDERSIZE = $RenderSize }
            foreach ($kv in @($extra -split ';' | Where-Object { $_ -match '=' })) { $i = $kv.IndexOf('='); $e[$kv.Substring(0, $i).Trim()] = $kv.Substring($i + 1) }
            $null = Invoke-WfcExe $exe $d $e "run.log" (1800)
        }
        if (-not (Test-Path $lg)) { Res "$label.r$rep" "UNKNOWN" "no log" "Experimental"; continue }
        # walk the log: SIMHASH <step> <hash> marks the current sim step; PERF / SLOWFRAME lines belong to it
        $step = -1; $hash = @{}; $ft = New-Object System.Collections.Generic.List[double]; $sf = New-Object System.Collections.Generic.List[object]
        foreach ($l in [IO.File]::ReadLines($lg)) {
            $m = [regex]::Match($l, 'SIMHASH (\d+) ([0-9a-f]+)'); if ($m.Success) { $step = [int]$m.Groups[1].Value; $hash[$step] = $m.Groups[2].Value; continue }
            if ($step -lt $From -or $step -ge $Steps) { continue }
            if ($l.Contains('SLOWFRAME f')) { $m = [regex]::Match($l, $sfRe); if ($m.Success) { $v = @($m.Groups | Select-Object -Skip 1 | ForEach-Object { [double]$_.Value }); $ft.Add($v[0]); $sf.Add($v) } }
        }
        $rng = @($hash.Keys | Where-Object { $_ -ge $From -and $_ -lt $Steps } | Sort-Object)
        $all = $sf; $slowF = @($sf | Where-Object { $_[0] -gt 3.333 })
        $col = { param($i) @($slowF | ForEach-Object { $_[$i] } | Where-Object { $_ -ge 0 }) }
        $colA = { param($i) @($all | ForEach-Object { $_[$i] } | Where-Object { $_ -ge 0 }) }
        $rows.Add([pscustomobject][ordered]@{ arm = $label; rep = $rep; steps = $rng.Count; frames = $ft.Count
            p50 = [Math]::Round((Pct $ft 0.5), 2); p90 = [Math]::Round((Pct $ft 0.9), 2); p99 = [Math]::Round((Pct $ft 0.99), 2)
            slow = $slowF.Count; gpu = [Math]::Round((Mean (& $col 8)), 2); gpu_world = [Math]::Round((Mean (& $col 9)), 2); gpu_fx = [Math]::Round((Mean (& $col 11)), 2)
            gpu_transl = [Math]::Round((Mean (& $col 12)), 2); gpu_post = [Math]::Round((Mean (& $col 13)), 2); cpu_render = [Math]::Round((Mean (& $col 1)), 2)
            cpu_chars = [Math]::Round((Mean (& $colA 3)), 2); outside = [Math]::Round((Mean (& $col 7)), 2)
            draws = [Math]::Round((Mean (& $colA 14)), 0); draws_dyn = [Math]::Round((Mean (& $colA 15)), 0); draws_fx = [Math]::Round((Mean (& $colA 16)), 0); binds = [Math]::Round((Mean (& $colA 17)), 0)
            hashes = ($rng | ForEach-Object { $hash[$_] }) -join ',' })
    }
}
# validity vs the first arm of the same rep: identical SIMHASH over the range + draws / chars parity (within 3 %)
foreach ($r in $rows) {
    $ref = @($rows | Where-Object { $_.rep -eq $r.rep })[0]
    $sameMatch = $r.steps -gt 0 -and $r.hashes -eq $ref.hashes
    $par = { param($a, $b) if (-not $b -or [double]::IsNaN($a) -or [double]::IsNaN($b)) { $true } else { [Math]::Abs($a - $b) / [Math]::Max(1e-6, [Math]::Abs($b)) -le 0.03 } }
    # workload COUNTS only (2026-10-10 validation: chars CPU is a time, it varies with machine noise; counts are the workload)
    $parity = (& $par $r.draws $ref.draws) -and (& $par $r.draws_dyn $ref.draws_dyn) -and (& $par $r.draws_fx $ref.draws_fx) -and (& $par $r.binds $ref.binds)
    $cm = Join-Path $OutDir ("{0}-r{1}\CONTAMINATED.txt" -f $r.arm, $r.rep); $contam = Test-Path $cm
    $st = if (-not $r.steps -or $contam) { "UNKNOWN" } elseif (-not $sameMatch) { "FAIL" } elseif (-not $parity) { "UNKNOWN" } else { "PASS" }
    if ($contam) { Res "$($r.arm).r$($r.rep).contaminated" "UNKNOWN" ("foreign process during the row - rerun: " + ((Get-Content $cm) -join " | ")) "Experimental" }
    Res "$($r.arm).r$($r.rep)" $st ("{0}: steps {1} ({2}-{3}), same match as '{4}' {5}, workload parity {6}; frame p50 / p90 / p99 {7} / {8} / {9} ms over {10} frames; slow frames {11}: GPU {12} (world {13}, fx {14}, transl {15}, post {16}), CPU render {17}, outside {19}; all frames: chars CPU {18}, draws {20} (dyn {21}, fx {22}), binds {23}" -f `
        $r.arm, $r.steps, $From, $Steps, $ref.arm, $sameMatch, $parity, $r.p50, $r.p90, $r.p99, $r.frames, $r.slow, $r.gpu, $r.gpu_world, $r.gpu_fx, $r.gpu_transl, $r.gpu_post, $r.cpu_render, $r.cpu_chars, $r.outside, $r.draws, $r.draws_dyn, $r.draws_fx, $r.binds) "Experimental"
}
Write-WfcCsv @($rows | Select-Object * -ExcludeProperty hashes) (Join-Path $OutDir "lockstep.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"LOCKSTEP A/B: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
