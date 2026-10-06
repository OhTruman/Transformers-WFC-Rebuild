# MILESTONE 07 CHARACTER MATRIX: selected UI character = Gameplay character = rendered robot body = faction =
# vehicle body = class = default loadout. A pawn that spawned with a DIFFERENT body is a failure, never a pass.
#
#   .\tools\fidelity\m07-characters.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Config Debug|Release] [-ReportOnly]
#
# Part A (data, every exported MP character - 33 chassis): Characters/<id>/{robot.glb, vehicle.glb, character.json}
#   present and loadable (glb header + meshes + skin joints), character.json faction / class / vehicle form agree with
#   AssetTools mp_characters.json, vehicle form follows the class rule (Scout car, Scientist jet, Soldier tank, Leader truck).
# Part B (runtime, the real frontend path): one process, one match per class preset offered by Choose Character, each
#   chosen with keys; per match:
#   UI      flow match.characterSelected (name / specialty / chassis) - the screen's own selection
#   GAME    MATCH spawn chassis (+ drawn= / fallback= when Gameplay logs it)
#   BODY    the robot.glb and vehicle.glb the renderer loaded after the selection (Characters/<chassis>/...)
#   FACTION spawn team -> faction; the chassis' manifest faction must match
#   CLASS   specialty = preset class; the chassis' vehicle form = the class's form
#   LOADOUT weapon glTFs loaded vs the preset's weapons; a weapon with no export is ASSET MISSING, not PASS
#   PIXELS  spawn / vehicle frames per class in a contact sheet: whether the drawn body LOOKS like the selection is a
#           human check (no fake PASS)
#   OPTIMUS FALLBACK is reported explicitly whenever a non-Optimus chassis resolves to Characters/Optimus.
# Decepticon presets need a team-1 spawn; no hook selects the team offline, so they stay UNKNOWN until one exists.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("Release", "Debug")][string]$Config = "Debug", [switch]$ReportOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe; $X = Get-M07Expectations
$res = New-WfcResults
function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "m07char.$id" $status $null $note $owner }
$VS = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice"
$formByClass = @{ Scout = "car"; Scientist = "jet"; Soldier = "tank"; Leader = "truck" }

# ---------------- A. data: every exported character
function GlbInfo([string]$p) {
    if (-not (Test-Path $p)) { return $null }
    $fs = [IO.File]::OpenRead($p); try { $br = New-Object IO.BinaryReader $fs; $magic = $br.ReadUInt32(); $null = $br.ReadUInt32(); $null = $br.ReadUInt32(); $len = $br.ReadUInt32(); $null = $br.ReadUInt32()
        if ($magic -ne 0x46546C67) { return [pscustomobject]@{ ok = $false; why = "not a glb" } }
        $j = [Text.Encoding]::UTF8.GetString($br.ReadBytes($len)) | ConvertFrom-Json
        return [pscustomobject]@{ ok = $true; meshes = @($j.meshes).Count; joints = (@($j.skins | ForEach-Object { @($_.joints).Count }) | Measure-Object -Maximum).Maximum; anims = @($j.animations).Count; bytes = (Get-Item $p).Length } } finally { $fs.Dispose() }
}
$dataRows = New-Object System.Collections.Generic.List[object]
foreach ($c in $X.characters) {
    $d = Join-Path $VS "Characters\$($c.chassis)"; $r = GlbInfo (Join-Path $d "robot.glb"); $v = GlbInfo (Join-Path $d "vehicle.glb")
    $cj = if (Test-Path (Join-Path $d "character.json")) { Get-Content -Raw (Join-Path $d "character.json") | ConvertFrom-Json } else { $null }
    $cjF = if ($cj) { "$($cj.faction)" } else { "" }; $cjC = if ($cj) { "$($cj.default_class)$($cj.class)" } else { "" }
    $isPlayerChassis = $c.chassis -notlike "Minion*"   # minions (Frenzy / Rumble / Laserbeak) are AI archetypes, not player chassis: the class -> vehicle-form rule does not apply
    $formOk = (-not $isPlayerChassis) -or $formByClass[$c.default_class] -eq $c.vehicle_form
    $ok = $r -and $r.ok -and $r.meshes -gt 0 -and $r.joints -gt 0 -and $v -and $v.ok -and $v.meshes -gt 0 -and $cj
    $consistent = (-not $cjF -or $cjF -eq $c.faction) -and (-not $cjC -or $cjC -match $c.default_class)
    $dataRows.Add([pscustomobject][ordered]@{ chassis = $c.chassis; iconic = $c.iconic; faction = $c.faction; class = $c.default_class; vehicle_form = $c.vehicle_form; default_mp = $c.available_by_default
        robot = $(if ($r -and $r.ok) { "{0} meshes / {1} joints / {2} clips" -f $r.meshes, $r.joints, $r.anims } else { "MISSING" }); vehicle = $(if ($v -and $v.ok) { "{0} meshes / {1} joints" -f $v.meshes, $v.joints } else { "MISSING" })
        character_json = $(if ($cj) { "faction $cjF class $cjC" } else { "MISSING" }); verdict = $(if (-not $ok) { "FAIL" } elseif (-not $consistent -or -not $formOk) { "FAIL" } else { "PASS" }) })
    Res "data.$($c.chassis)" $dataRows[-1].verdict ("{0} ({1}, {2} {3}, vehicle {4}): robot {5}; vehicle {6}; character.json {7}; class / vehicle-form rule {8}" -f $c.chassis, $c.iconic, $c.faction, $c.default_class, $c.vehicle_form, $dataRows[-1].robot, $dataRows[-1].vehicle, $dataRows[-1].character_json, $formOk) "AssetTools"
}
Write-WfcCsv $dataRows (Join-Path $OutDir "characters_data.csv")
# every preset weapon (infantry + vehicle) exported and loadable
$script:NoMeshWeapons = @()
foreach ($cls in @($X.class_presets.PSObject.Properties | ForEach-Object { $_.Name })) { foreach ($w in @($X.class_presets.$cls.weapons) + @($X.class_presets.$cls.vehicle_weapons)) { if (-not $w) { continue }
    $g = @(Get-ChildItem (Join-Path $VS "Weapons\$w") -Filter *.glb -ErrorAction SilentlyContinue | Select-Object -First 1)[0]; $gi = if ($g) { GlbInfo $g.FullName } else { $null }
    $wj = Join-Path $VS "Weapons\$w\weapon.json"; $noMesh = (Test-Path $wj) -and ((Get-Content -Raw $wj | ConvertFrom-Json).mesh.mesh_note -match 'authors no SkeletalMesh')
    if ($noMesh) { $script:NoMeshWeapons += $w; Res "data.weapon.$cls.$w" "PASS" ("{0} preset weapon {1}: no held mesh by design (export: the weapon mesh component authors no SkeletalMesh - vehicle socket / thrown projectile)" -f $cls, $w) "AssetTools"; continue }
    Res "data.weapon.$cls.$w" $(if ($gi -and $gi.ok -and $gi.meshes -gt 0) { "PASS" } else { "FAIL" }) ("{0} preset weapon {1}: {2}" -f $cls, $w, $(if ($gi -and $gi.ok) { "{0} ({1} meshes, {2} joints)" -f $g.Name, $gi.meshes, $gi.joints } else { "no loadable glb under Weapons\$w" })) "AssetTools" } }

# Optimus identities: 06b's fallback body folder "Optimus" and Optimus Prime's roster chassis id "Truck" (Gameplay 22a+,
# also the boot-default body loaded before WFC_CHASSIS / the selection is applied). A non-Optimus selection whose ACTIVE
# body is one of these is an OPTIMUS FALLBACK.
$OptIds = @("Optimus", "Truck")
# ---------------- C. runtime: EVERY exported chassis by direct boot (WFC_CHASSIS, Gameplay 22a+) + Gameplay's chassis test
$relExe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; if (-not (Test-Path $relExe)) { $relExe = $exe }
if ($H.Contains("WFC_CHASSIS")) {
    $dc = Join-Path $OutDir "chassis"; New-Item -ItemType Directory -Force $dc | Out-Null; $cRows = New-Object System.Collections.Generic.List[object]
    foreach ($c in $X.characters) { $dd = Join-Path $dc $c.chassis; New-Item -ItemType Directory -Force $dd | Out-Null
        if (-not $ReportOnly -and -not (Test-Path (Join-Path $dd "wfc.log")) -and (Wait-WfcGpu)) { $null = Invoke-WfcExe $relExe $dd @{ WFC_BOOT = "match"; WFC_MAP = "MP_IAC_Streets"; WFC_CHASSIS = $c.chassis; WFC_LOCKSTEP = "1"; WFC_SMOKE_FRAMES = "160"; WFC_SHOTEVERY = "$dd,120,120"; WFC_LOGEVERY = "0" } "run.log" 240 }
        $lg = Join-Path $dd "wfc.log"; if (-not (Test-Path $lg)) { continue }
        $rb = @(Grep-Log $lg 'skinned glb: .*?/Characters/([^/\\]+)/robot\.glb' | ForEach-Object { [regex]::Match($_.text, '/Characters/([^/\\]+)/robot').Groups[1].Value } | Select-Object -Unique)
        $vb = @(Grep-Log $lg 'skinned glb: .*?/Characters/([^/\\]+)/vehicle\.glb' | ForEach-Object { [regex]::Match($_.text, '/Characters/([^/\\]+)/vehicle').Groups[1].Value } | Select-Object -Unique)
        # the ACTIVE chassis = the last one applied ("chassis <id> (<name>): robot N clips ..."); earlier loads are the boot default
        $applied = @([IO.File]::ReadLines($lg) | Where-Object { $_ -cmatch '\] chassis (\S+) \(' } | ForEach-Object { [regex]::Match($_, '\] chassis (\S+) \(').Groups[1].Value }); $active = if ($applied.Count) { $applied[-1] } else { "" }
        $unavail = @(Grep-Log $lg 'chassis unavailable|fallback='); $opt = ($OptIds -notcontains $c.chassis) -and ($OptIds -contains $active -or (-not $active -and @($rb | Where-Object { $OptIds -contains $_ }).Count -and $rb -notcontains $c.chassis))
        $st = if ($unavail.Count) { "FAIL" } elseif ($opt) { "FAIL" } elseif ($active -and $active -ne $c.chassis) { "FAIL" } elseif ($rb -contains $c.chassis -and $vb -contains $c.chassis) { "PASS" } elseif (-not $rb.Count) { "UNKNOWN" } else { "FAIL" }
        $cRows.Add([pscustomobject]@{ chassis = $c.chassis; iconic = $c.iconic; active = $active; robot = ($rb -join ","); vehicle = ($vb -join ","); status = $(if ($opt) { "FAIL (OPTIMUS FALLBACK)" } else { $st }) })
        Res "chassis.$($c.chassis)" $st ("WFC_CHASSIS={0} ({1}): active chassis {6}; robot bodies loaded {2}; vehicle bodies loaded {3}{4}{5}" -f $c.chassis, $c.iconic, ($rb -join ","), ($vb -join ","), $(if ($opt) { "; OPTIMUS FALLBACK" } else { "" }), $(if ($unavail.Count) { "; " + ($unavail[0].text -replace '^.*\] ', '') } else { "" }), $(if ($active) { $active } else { "not logged" })) "Gameplay/AssetTools" }
    Write-WfcCsv $cRows (Join-Path $OutDir "characters_chassis.csv")
    $tiles = @(Get-ChildItem $dc -Recurse -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object FullName | ForEach-Object { @{ png = $_.FullName; label = (Split-Path (Split-Path $_.FullName) -Leaf) } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_chassis.png") 6 320 180 }
} else { Res "chassis" "SKIP" "build has no WFC_CHASSIS (Gameplay 22a+): every-chassis runtime check not possible" "Experimental" }
if ($H.Contains("WFC_CHASSISTEST")) { $dt = Join-Path $OutDir "chassistest"; New-Item -ItemType Directory -Force $dt | Out-Null
    if (-not $ReportOnly -and (Wait-WfcGpu)) { $null = Invoke-WfcExe $relExe $dt @{ WFC_CHASSISTEST = "1" } "run.log" 600 }
    $cl = @(Grep-Log (Join-Path $dt "wfc.log") 'CHASSIS (PASS|FAIL) '); $sumL = @(Grep-Log (Join-Path $dt "wfc.log") 'CHASSIS SUMMARY')[0]
    Res "gameplay_chassistest" $(if (@($cl | Where-Object { $_.text -cmatch '\] CHASSIS FAIL ' }).Count) { "FAIL" } elseif ($cl.Count) { "PASS" } else { "SKIP" }) ("Gameplay WFC_CHASSISTEST: {0}; failing: {1}" -f $(if ($sumL) { $sumL.text -replace '^.*CHASSIS ', '' } else { "no summary" }), ((@($cl | Where-Object { $_.text -cmatch '\] CHASSIS FAIL ' }) | Select-Object -First 5 | ForEach-Object { $_.text -replace '^.*CHASSIS FAIL ', '' }) -join " | ")) "Gameplay" }

# ---------------- B. runtime: the class presets through Choose Character
$d = Join-Path $OutDir "runtime"; New-Item -ItemType Directory -Force $d | Out-Null
$presetOrder = @("Scout", "Scientist", "Leader", "Soldier")   # Choose Character list order (CustomTransformers_GFX); verified per match from the selection event
if (-not $H.Contains("WFC_CHARSELECT")) { Res "runtime" "SKIP" "build has no WFC_CHARSELECT: the match auto-selects; selection cannot be exercised" "Experimental" }
else {
    $seg = for ($i = 0; $i -lt $presetOrder.Count; $i++) {
        $down = (@(0..($i)) | Select-Object -Skip 1 | ForEach-Object { "ui:Down;wait:t=0.6" }) -join ";"
        "call:Online.SetSelectedMapID,508;wait:t=1.5;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:movie=CustomTransformers;wait:t=2;shot:$d\c${i}_0choose.bmp;dump:CustomTransformers;$down$(if ($down) { ';' })wait:t=0.6;shot:$d\c${i}_1focused.bmp;ui:Accept;wait:ui=InGame;wait:t=2;shot:$d\c${i}_2robot.bmp;wait:t=4;shot:$d\c${i}_3later.bmp;snapshot:class$i;wait:ui=GameEnded;wait:level=GameLobby;wait:ui=InLobby;wait:t=2"
    }
    $s = @("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2") + @($seg) + @("quit")
    $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_CHARSELECT = "1"; WFC_FRONTEND_SCRIPT = ($s -join ";"); WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "1200"; WFC_LIFECYCLE = "2"; WFC_PRESSTRANSFORM_EVERY = "200"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "60" }
    if (-not $ReportOnly) { if (Wait-WfcGpu) { $null = Invoke-WfcSampled $exe $d $e 1500 1.0 } else { "GPU busy" | Set-Content (Join-Path $d "SKIPPED.txt") } }
    # walk the log: each selection event opens a segment; loads after it belong to that selection
    $log = Join-Path $d "wfc.log"; $segs = New-Object System.Collections.Generic.List[object]; $cur = $null
    if (Test-Path $log) { foreach ($ln in [IO.File]::ReadLines($log)) {
        if ($ln -match 'FLOW match\.characterSelected name=(\S+) type=(\S+) specialty=(\S+) chassis=(\S+)') { $cur = [ordered]@{ ui_name = $Matches[1]; ui_class = $Matches[3]; ui_chassis = $Matches[4]; game_chassis = ""; team = ""; drawn = ""; fallback = ""; robot = @(); vehicle = @(); weapons = @() }; $segs.Add($cur); continue }
        if (-not $cur) { continue }
        if ($ln -match '\] MATCH spawn .*team=(\d+).*chassis=(\S+)') { if (-not $cur.game_chassis) { $cur.team = $Matches[1]; $cur.game_chassis = $Matches[2] } }
        if ($ln -match '\] MATCH spawn .*drawn=(\S+)') { $cur.drawn = $Matches[1] }; if ($ln -match 'fallback=(\S+)') { $cur.fallback = $Matches[1] }
        if ($ln -match 'skinned glb: .*?/Characters/([^/\\]+)/robot\.glb') { $cur.robot += $Matches[1] }
        if ($ln -match 'skinned glb: .*?/Characters/([^/\\]+)/vehicle\.glb') { $cur.vehicle += $Matches[1] }
        if ($ln -match 'skinned glb: .*?/Weapons/([^/\\]+)/') { $cur.weapons += $Matches[1] }
    } }
    $rtRows = New-Object System.Collections.Generic.List[object]; $i = 0; $lastRobot = @(); $lastVeh = @(); $lastW = @()   # a body already loaded is reused by the renderer: carry it forward
    foreach ($sg in $segs) {
        $cls = $sg.ui_class; $p = $X.class_presets.$cls; $team = $sg.team; $fac = if ($team -eq "0") { "Autobot" } elseif ($team -eq "1") { "Decepticon" } else { "?" }
        $want = if ($p -and $fac -ne "?") { $p.$fac } else { "" }; $man = @($X.characters | Where-Object { $_.chassis -eq $sg.game_chassis })[0]
        $body = @($sg.robot | Select-Object -Unique); $vbody = @($sg.vehicle | Select-Object -Unique)
        if (-not $body.Count -and $lastRobot.Count) { $body = $lastRobot }; if (-not $vbody.Count -and $lastVeh.Count) { $vbody = $lastVeh }; $lastRobot = $body; $lastVeh = $vbody
        $optimusFallback = ($sg.game_chassis -and $OptIds -notcontains $sg.game_chassis -and (($OptIds -contains $sg.drawn) -or (@($body | Where-Object { $OptIds -contains $_ }).Count -and $body -notcontains $sg.game_chassis)))
        $wantW = @($p.weapons); $gotW = @($sg.weapons | Select-Object -Unique); if (-not $gotW.Count -and $lastW.Count) { $gotW = $lastW }; $lastW = $gotW; $missingExport = @($wantW | Where-Object { $X.weapon_exports -notcontains $_ })
        $wrongW = @($gotW | Where-Object { $wantW -notcontains $_ }); $notLoaded = @($wantW | Where-Object { $gotW -notcontains $_ -and $X.weapon_exports -contains $_ -and $script:NoMeshWeapons -notcontains $_ })
        $loadout = if (-not $wantW.Count) { "UNKNOWN" } elseif ($wrongW.Count -or $notLoaded.Count) { "FAIL" } elseif ($missingExport.Count) { "ASSET MISSING" } else { "PASS" }
        $r = [ordered]@{ n = $i; ui = "$($sg.ui_name) ($cls)"; ui_selection = $(if ($presetOrder -contains $cls) { "PASS" } else { "FAIL" })
            gameplay = $(if ($sg.game_chassis -and $sg.game_chassis -eq $sg.ui_chassis) { "PASS" } else { "FAIL" })
            expected_chassis = $want; selected_chassis = $sg.game_chassis; body = ($body -join ","); vehicle_body = ($vbody -join ",")
            body_resolves = $(if ($optimusFallback) { "FAIL (OPTIMUS FALLBACK)" } elseif ($body -contains $sg.game_chassis) { "PASS" } elseif (-not $body.Count) { "UNKNOWN" } else { "FAIL" })
            faction = $(if ($fac -eq "?") { "UNKNOWN" } elseif ($man -and $man.faction -eq $fac -and (-not $want -or $want -eq $sg.game_chassis)) { "PASS" } else { "FAIL" })
            vehicle = $(if ($vbody -contains $sg.game_chassis) { "PASS" } elseif (@($vbody | Where-Object { $OptIds -contains $_ }).Count -and $OptIds -notcontains $sg.game_chassis) { "FAIL (OPTIMUS FALLBACK)" } elseif (-not $vbody.Count) { "UNKNOWN" } else { "FAIL" })
            class = $(if ($man -and $man.default_class -eq $cls -and $formByClass[$cls] -eq $man.vehicle_form) { "PASS" } elseif ($man) { "FAIL" } else { "UNKNOWN" })
            loadout = $loadout; loadout_note = "expected {0}; loaded {1}; wrong weapon(s) {2}; exported but not loaded {3}; not exported {4}" -f ($wantW -join ","), ($gotW -join ","), ($wrongW -join ","), ($notLoaded -join ","), ($missingExport -join ",")
            rendered = "HUMAN (sheet)"; gameplay_log = $(if ($sg.drawn) { "drawn=$($sg.drawn) fallback=$($sg.fallback)" } else { "" }) }
        $rtRows.Add([pscustomobject]$r)
        foreach ($c in "ui_selection", "gameplay", "body_resolves", "faction", "vehicle", "class", "loadout") { $v = "$($r[$c])"; $st = if ($v -like "FAIL*") { "FAIL" } elseif ($v -eq "ASSET MISSING") { "FAIL" } else { $v }
            Res "runtime.$cls.$c" $st ("{0}: UI {1} chassis {2}; Gameplay {3} (team {4} = {5}); expected {6}; robot body {7}; vehicle body {8}; {9}; {10}" -f $cls, $sg.ui_name, $sg.ui_chassis, $sg.game_chassis, $team, $fac, $want, ($body -join ","), ($vbody -join ","), $r.loadout_note, $r.gameplay_log) $(switch ($c) { "ui_selection" { "Frontend" } "gameplay" { "Frontend/Gameplay" } { $_ -in "body_resolves", "vehicle" } { "AssetTools/Gameplay/Rendering" } "loadout" { "Gameplay/AssetTools" } default { "Gameplay" } }) }
        $i++
    }
    if (-not (Test-Path $log)) { Res "runtime.all_presets_selectable" "SKIP" "no runtime log (report-only or GPU busy)" "Experimental" }
    $distinct = @($segs | ForEach-Object { $_.ui_class } | Select-Object -Unique).Count
    if (Test-Path $log) { Res "runtime.all_presets_selectable" $(if ($distinct -ge 4) { "PASS" } elseif ($segs.Count) { "FAIL" } else { "FAIL" }) ("{0} matches, distinct classes selected {1} of 4 (Scout / Scientist / Leader / Soldier)" -f $segs.Count, $distinct) "Frontend" }
    Res "runtime.decepticon_presets" "UNKNOWN" ("Decepticon chassis ({0}) need a team-1 spawn; no hook selects the team offline (proposal: WFC_TEAM=1 for Gameplay)" -f (($presetOrder | ForEach-Object { $X.class_presets.$_.Decepticon }) -join ", ")) "Gameplay"
    Write-WfcCsv $rtRows (Join-Path $OutDir "characters_runtime.csv")
    $tiles = @(Get-ChildItem $d -Filter "c*_*.bmp" -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = $_.BaseName } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_characters.png") 4 400 225 }
    Write-M07Matrix $rtRows @("n", "ui", "ui_selection", "gameplay", "expected_chassis", "selected_chassis", "body", "body_resolves", "vehicle_body", "vehicle", "faction", "class", "loadout", "rendered") (Join-Path $OutDir "CHARACTERS.md") "M07 character matrix" @("exe: ``$exe``", "", "A pawn that spawned with a different body FAILS (OPTIMUS FALLBACK is named). Rendered appearance: sheet_characters.png, human check.", "", "Data check of all $($dataRows.Count) exported characters: characters_data.csv ($(@($dataRows | Where-Object verdict -eq 'PASS').Count) PASS).")
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"M07 CHARACTERS: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
