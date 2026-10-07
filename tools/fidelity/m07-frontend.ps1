# MILESTONE 07 FRONTEND PRESENTATION: cold boot to return, every screen, as a player sees it.
#
#   .\tools\fidelity\m07-frontend.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Config Debug|Release] [-ReportOnly]
#
# A  movie aspect   cold boot (no skip) in 16:9 (1280x720), 4:3 (1024x768) and 16:10 (1680x1050) window profiles: frames
#                   in each logo and in FMV_intro; the movie content box must keep 16:9 and the bars must be black - the
#                   frontend must not show beside / above the movie (M06b report: frontend visible at the side edges)
# B  screen tour    title, main menu, Multiplayer party lobby, mode list, host options, game lobby, Choose Character,
#                   in game, pause, results, lobby return, Settings, Accounts, Extras, title after return. Per screen:
#                   background (black / blank / untextured / noise), missing render data (product "NO RENDER DATA" /
#                   legacy fallback lines), Frontend's nav.check invariants when the build has navcheck: (one focus
#                   owner, visible input owner, nothing over gameplay, no stray modal), stale overlays (a movie of the
#                   previous screen still open), focus present in the screen's GFx dump
# C  return routes  match quit -> party lobby (pass 4: Quit Game? Yes) or title (older builds); party Back -> title
# D  account name   Accounts -> Create -> type a name -> Select / Ok: the name appears in Accounts and in the party lobby
# E  character name the character the Choose Character screen showed focused = the one the selection event reports
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("Release", "Debug")][string]$Config = "Debug", [switch]$ReportOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe; $src = Join-Path $Root "src"
function SrcHas([string]$pat) { return [bool](Get-ChildItem $src -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue | Select-String -Pattern $pat -SimpleMatch -List | Select-Object -First 1) }
$quitBox = SrcHas "TnQuitMessageBox"; $navOn = SrcHas 'rfind("navcheck:", 0)'; $typeOn = SrcHas '"type:"'
$res = New-WfcResults
function Res($id, $status, $note, $owner = "Frontend") { Add-WfcResult $res "m07fe.$id" $status $null $note $owner }
function Nav([string]$label) { if ($navOn) { "navcheck:$label" } else { "" } }
function RunFE([string]$d, [string]$script, [hashtable]$x = @{}, [string]$profile = "") {
    New-Item -ItemType Directory -Force $d | Out-Null
    if ($ReportOnly) { return }
    if (-not (Wait-WfcGpu)) { "GPU busy" | Set-Content (Join-Path $d "SKIPPED.txt"); return }
    if ($profile) { [IO.File]::WriteAllText((Join-Path $d "wfc_profile.ini"), $profile) }
    $e = @{ WFC_BOOT = "frontend"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = (Convert-QuitRouting $script $quitBox); WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOWSEED = "1"; WFC_FLOW_TIMEOUT = "600" }
    if ($H.Contains("WFC_VISUALCHECK")) { $e.WFC_VISUALCHECK = "1" }; foreach ($k in $x.Keys) { $e[$k] = $x[$k] }
    $null = Invoke-WfcSampled $exe $d $e 900 1.0
}
function ParseNav([string]$log) { if (-not (Test-Path $log)) { return @() }; return @(Grep-Log $log 'FLOW nav\.check ' | ForEach-Object { $kv = @{}; foreach ($t in ($_.text -replace '^.*FLOW nav\.check ', '') -split ' ') { $p = $t -split '=', 2; if ($p.Count -eq 2) { $kv[$p[0]] = $p[1] } }; [pscustomobject]$kv }) }

# ---------------- A. movie aspect
$aspects = [ordered]@{ a169 = @(1280, 720); a43 = @(1024, 768); a1610 = @(1680, 1050) }
foreach ($k in $aspects.Keys) {
    $w, $h = $aspects[$k]; $d = Join-Path $OutDir "movies_$k"
    $s = "wait:t=2;shot:$d\m1_activision.bmp;wait:t=9;shot:$d\m2_hasbro.bmp;ui:Accept;wait:t=3;shot:$d\m3_highmoon.bmp;ui:Accept;wait:t=6;shot:$d\m4_fmv.bmp;ui:Accept;wait:frontend;wait:ui=FrontEnd;wait:t=2;shot:$d\m5_title.bmp;quit"
    RunFE $d $s @{} "[PCSettings]`r`nWidth=$w`r`nHeight=$h`r`nFullscreen=0`r`nTextureQuality=2`r`nVSync=0`r`n"
    foreach ($f in @(Get-ChildItem $d -Filter "m[1-4]_*.bmp" -ErrorAction SilentlyContinue | Sort-Object Name)) {
        Add-Type -AssemblyName System.Drawing; $b = [System.Drawing.Bitmap]::FromFile($f.FullName)
        try { $W = $b.Width; $Hh = $b.Height; $x0 = $W; $x1 = 0; $y0 = $Hh; $y1 = 0
            for ($y = 0; $y -lt $Hh; $y += 4) { for ($x = 0; $x -lt $W; $x += 4) { $c = $b.GetPixel($x, $y); if (($c.R + $c.G + $c.B) -gt 45) { if ($x -lt $x0) { $x0 = $x }; if ($x -gt $x1) { $x1 = $x }; if ($y -lt $y0) { $y0 = $y }; if ($y -gt $y1) { $y1 = $y } } } } } finally { $b.Dispose() }
        $box = if ($x1 -gt $x0) { [Math]::Round(($x1 - $x0) / [Math]::Max(1, ($y1 - $y0)), 2) } else { $null }
        $bars = @(); if ($W / $Hh -gt 1.8) { $bars = @(@(0, 0.1, 0.08, 0.9), @(0.92, 0.1, 1, 0.9)) } elseif ($W / $Hh -lt 1.74) { $bars = @(@(0.1, 0, 0.9, 0.06), @(0.1, 0.94, 0.9, 1)) } else { $bars = @(@(0, 0.1, 0.06, 0.9), @(0.94, 0.1, 1, 0.9)) }
        $barDetail = ($bars | ForEach-Object { (Present-Measure $f.FullName $_).detail } | Measure-Object -Maximum).Maximum
        # a dark logo frame has a small content box; judge aspect only when the content spans most of one axis
        $span = if ($x1 -gt $x0) { [Math]::Max(($x1 - $x0) / $W, ($y1 - $y0) / $Hh) } else { 0 }
        $aspectOk = if ($span -lt 0.9) { $null } else { [Math]::Abs($box - 1.78) -le 0.12 }
        Res "movie.$k.$($f.BaseName)" $(if ($barDetail -gt 0.05) { "FAIL" } elseif ($aspectOk -eq $false) { "FAIL" } elseif ($aspectOk -eq $null) { "INFO" } else { "PASS" }) ("{0}x{1}: movie content box aspect {2} (16:9 = 1.78; judged when the content spans the frame: {3}); bar / edge texture detail {4} (frontend showing beside the movie FAILS)" -f $w, $h, $box, ([Math]::Round($span, 2)), $barDetail) "Frontend"
    }
}

# ---------------- B + C + E. screen tour (Debug, PC SKU)
$d = Join-Path $OutDir "tour"
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Down;wait:t=0.6;shot:{D}\08_charselect.bmp;dump:CustomTransformers;$(Nav 'm1.charselect');ui:Accept;" } else { "" }
$returnToParty = if ($quitBox) { "wait:t=1.5;shot:{D}\12b_quitbox.bmp;$(Nav 'm1.quitbox');ui:Accept;wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;shot:{D}\13_return_party.bmp;snapshot:return_route;$(Nav 'm1.party_after');ui:Back;wait:t=1.5;shot:{D}\14b_quitbox_title.bmp;ui:Accept;wait:level=FrontEnd" } else { "wait:level=FrontEnd" }
$tour = @((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=3", "shot:{D}\01_title.bmp", (Nav "c1.main"), "dump:FrontEnd",
    "ui:Down", "wait:t=0.5", "ui:Accept", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=2.5", "shot:{D}\02_party.bmp", (Nav "c1.party"), "dump:Lobbies",
    "call:Online.EditGameMode,TDM", "wait:t=1", "shot:{D}\03_modes.bmp", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2", "shot:{D}\05_gamelobby.bmp", (Nav "c1.gamelobby"), "dump:Lobbies",
    "call:Online.SetSelectedMapID,508", "wait:t=1", "call:Online.BeginLobbyExitCountdown", "wait:loading=1", "wait:t=1", "shot:{D}\07_loading.bmp", "wait:level=Match", $cs, "wait:ui=InGame", "wait:t=2", "shot:{D}\09_ingame.bmp", (Nav "m1.ingame"),
    "showmenu", "wait:ui=Paused", "wait:t=1", "shot:{D}\10_pause.bmp", (Nav "m1.pause"), "ui:Accept", "wait:ui=InGame", "wait:t=2", "shot:{D}\11_resumed.bmp", (Nav "m1.resumed"),
    "wait:ui=GameEnded", "wait:t=2", "shot:{D}\12_results.bmp", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2", "shot:{D}\12c_lobby_return.bmp", (Nav "m1.lobby"),
    "call:Online.BeginLobbyExitCountdown", "wait:level=Match", $(if ($cs) { "wait:movie=CustomTransformers;wait:t=1;ui:Accept;" } else { "" }), "wait:ui=InGame", "wait:t=2", "showmenu", "wait:ui=Paused", "wait:t=0.5", "call:Game.QuitToMainMenu", $returnToParty, "wait:ui=FrontEnd", "wait:t=3", "shot:{D}\15_title_after.bmp", (Nav "c2.main"),
    "ui:Down;wait:t=0.5;ui:Down;wait:t=0.5;ui:Down;wait:t=0.5", "ui:Accept", "wait:t=2.5", "shot:{D}\16_settings.bmp", (Nav "c2.settings"), "ui:Back", "wait:t=2",
    "ui:Down;wait:t=0.5", "ui:Accept", "wait:t=2.5", "shot:{D}\17_extras.bmp", (Nav "c2.extras"), "ui:Back", "wait:t=2", "shot:{D}\18_main_again.bmp", (Nav "c3.main"), "quit") | Where-Object { $_ }
RunFE $d (($tour -join ";").Replace("{D}", $d)) @{ WFC_SKIPINTRO = "1"; WFC_LIFECYCLE = "2"; WFC_CHARSELECT = "1"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "60" }
$log = Join-Path $d "wfc.log"; $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $navs = ParseNav $log
$screens = [ordered]@{ "01_title" = "scene"; "02_party" = "ui"; "03_modes" = "ui"; "05_gamelobby" = "ui"; "07_loading" = "ui"; "08_charselect" = "ui"; "09_ingame" = "world"; "10_pause" = "ui"; "11_resumed" = "world"; "12_results" = "ui"; "12c_lobby_return" = "ui"; "13_return_party" = "ui"; "15_title_after" = "scene"; "16_settings" = "ui"; "17_extras" = "ui"; "18_main_again" = "scene" }
foreach ($k in $screens.Keys) { $f = Join-Path $d "$k.bmp"; if (-not (Test-Path $f)) { Res "screen.$k" $(if (Test-Path (Join-Path $d "SKIPPED.txt")) { "SKIP" } else { "FAIL" }) "screen never reached (soft lock, wrong route or missing transition)"; continue }
    switch ($screens[$k]) {
        "scene" { $m = Present-Measure $f $script:PresentRegions.title_scene; $v = Present-SceneVerdict $m }
        "world" { $m = Present-World $f; $v = Present-WorldVerdict $m }
        default { $m = Present-Measure $f $script:PresentRegions.full; $v = Present-UiVerdict $m } }
    $diag = Read-ShotDiag $f; $legacy = $diag -and "$($diag.reasons)" -match 'legacy fallback|no render data'
    Res "screen.$k" $(if ($legacy) { "FAIL" } elseif ($v -eq "PARTIAL") { "HUMAN" } else { $v }) ("{0}: {1} verdict {2} (detail {3}, black {4}, blank {5}, untextured {6}/{7}, noise {8}){9}" -f $k, $screens[$k], $v, $m.detail, $m.black, $m.maxFlat, $m.untexMax, $m.untexFrac, $m.noise, $(if ($legacy) { "; renderer: $($diag.reasons)" } else { "" })) $(if ($screens[$k] -eq "world") { "Rendering/Frontend" } else { "Frontend/Rendering" }) }
$nrd = @(Grep-Log $log 'NO RENDER DATA|legacy fallback')
Res "render_data_present" $(if ($nrd.Count) { "FAIL" } else { "PASS" }) ("render data lookups failing / legacy fallback lines: {0} {1}" -f $nrd.Count, (($nrd | Select-Object -First 2 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | ")) "Rendering/Integration"
if ($navOn) { foreach ($c in $navs) { $why = @()
        if ([int]"0$($c.focusExtras)" -gt 1) { $why += "focusExtras=$($c.focusExtras)" }
        if ($c.popup -eq "open" -and $c.label -notmatch 'quitbox|prompt') { $why += "modal popup left open" }
        if ($c.level -ne "Match" -and $c.movies -ne "-" -and $c.inputOwnerVisible -ne "1") { $why += "input owner $($c.inputOwner) not visible (focus lost)" }
        if ($c.label -match '\.ingame$|\.resumed$' -and ($c.movies -ne "-" -or $c.extras -ne "-")) { $why += "screens over gameplay: movies=$($c.movies) extras=$($c.extras) (stale overlay)" }
        if ($c.label -match 'main$' -and $c.movies -ne "UI_GFxFrontEnd_p.FrontEnd_GFX_1") { $why += "main menu movies=$($c.movies)" }
        Res "nav.$($c.label)" $(if ($why.Count) { "FAIL" } else { "PASS" }) ("nav.check {0}: uiState {1}, movies {2}, extras {3}, input owner {4} (visible {5}), popup {6}{7}" -f $c.label, $c.uiState, $c.movies, $c.extras, $c.inputOwner, $c.inputOwnerVisible, $c.popup, $(if ($why.Count) { " -> " + ($why -join "; ") } else { "" })) } }
else { Res "nav" "SKIP" "build has no navcheck: script step; focus / ownership judged from flow + pixels only" "Experimental" }
# stale overlays without navcheck: any menu movie still open at the in-game snapshots
$open = @{}; $staleAt = @(); foreach ($ev in $F) { if ($ev.ev -eq "gfx.movie" -and "$($ev.opened)" -eq "True") { $open[$ev.movie] = $true }; if ($ev.ev -eq "gfx.movieClosed") { $open.Remove($ev.movie) }
    if ($ev.ev -eq "ui.state" -and $ev.to -eq "InGame") { $bad = @($open.Keys | Where-Object { $_ -notmatch 'Hud_GFX|MovieLoader' }); if ($bad.Count) { $staleAt += ("{0:N1}s: {1}" -f [double]$ev.t, ($bad -join ",")) } } }
Res "stale_overlay" $(if ($staleAt.Count) { "FAIL" } else { "PASS" }) ("menu movies still open when gameplay owns the screen: " + $(if ($staleAt.Count) { $staleAt -join " | " } else { "none" }))
$rr = @(Flow-Ev $F "snapshot" | Where-Object why -eq "return_route")[0]
Res "return_route.match_quit" $(if (-not $quitBox) { if (Test-Path (Join-Path $d "15_title_after.bmp")) { "PASS" } else { "FAIL" } } elseif ($rr -and $rr.level -eq "PartyLobby") { "PASS" } else { "FAIL" }) ("Quit from a match: expected {0}; observed level {1}" -f $(if ($quitBox) { "Quit Game? Yes -> party lobby (pass 4)" } else { "title" }), $(if ($rr) { $rr.level } else { "n/a" }))
$sel = @(Flow-Ev $F "match.characterSelected")[0]; $dm = @((Read-GfxDumps $log) | Where-Object { $_.movie -like "*CustomTransformers*" })[0]
if ($sel -and $dm) { $shown = @($dm.texts | Where-Object { $_ -match '^(Scout|Scientist|Leader|Soldier)$' }); Res "selected_character_name" $(if ($dm.texts -contains $sel.name -or $dm.texts -contains $sel.specialty) { "PASS" } else { "FAIL" }) ("Choose Character showed {0}; selection event {1} ({2}, chassis {3})" -f (($shown | Select-Object -Unique) -join ","), $sel.name, $sel.specialty, $sel.chassis) }
else { Res "selected_character_name" "SKIP" "no selection event or no Choose Character dump" "Experimental" }

# ---------------- D. account name
$d2 = Join-Path $OutDir "account"; $name = "QA" + (Get-Random -Minimum 1000 -Maximum 9999)
$typing = if ($typeOn) { "type:$name;wait:t=0.5" } else { ($name.ToCharArray() | ForEach-Object { if ([char]::IsDigit($_)) { "key:$([int][char]$_)" } else { "key:$([int][char]::ToUpper($_))" } }) -join ";wait:t=0.25;" }
$acc = "wait:frontend;wait:ui=FrontEnd;wait:t=3;ui:Down;wait:t=0.5;ui:Down;wait:t=0.5;ui:Down;wait:t=0.5;ui:Down;wait:t=0.5;ui:Down;wait:t=0.5;ui:Accept;wait:t=2;ui:Accept;wait:t=2;$typing;wait:t=1;shot:$d2\a_typed.bmp;dump:FrontEnd;ui:Accept;wait:t=2;shot:$d2\b_after_select.bmp;dump:FrontEnd;ui:Accept;wait:t=2;shot:$d2\c_accounts.bmp;dump:FrontEnd;ui:Back;wait:t=2;ui:Up;wait:t=0.5;ui:Up;wait:t=0.5;ui:Up;wait:t=0.5;ui:Up;wait:t=0.5;ui:Accept;wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;shot:$d2\d_party.bmp;dump:Lobbies;quit"
RunFE $d2 $acc @{ WFC_SKIPINTRO = "1" }
$alog = Join-Path $d2 "wfc.log"; $ad = @(Read-GfxDumps $alog); $inAccounts = @($ad | Where-Object { $_.movie -like "*FrontEnd*" -and ($_.texts -contains $name -or @($_.texts | Where-Object { $_ -like "*$name*" }).Count) }).Count; $inParty = @($ad | Where-Object { $_.movie -like "*Lobbies*" -and @($_.texts | Where-Object { $_ -like "*$name*" }).Count }).Count
Res "account.typed_name_shown" $(if ($inAccounts) { "PASS" } elseif (Test-Path $alog) { "FAIL" } else { "SKIP" }) ("typed '{0}' ({1}): name present in an Accounts dump {2}" -f $name, $(if ($typeOn) { "type:" } else { "key:" }), [bool]$inAccounts)
Res "account.name_in_party_lobby" $(if ($inParty) { "PASS" } elseif (Test-Path $alog) { "FAIL" } else { "SKIP" }) ("the created account name '{0}' appears in the party lobby roster: {1} (wrong account name = the lobby shows another name)" -f $name, [bool]$inParty)

# ---------------- F. title Matinee over 60 s: animated, authored tracks only, camera moving, every sample healthy
$d3 = Join-Path $OutDir "title60"; $X = Get-M07Expectations
RunFE $d3 ((@((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd") + @(1..12 | ForEach-Object { "wait:t=5;shot:$d3\t{0:D2}.bmp" -f ($_ * 5) }) + @("quit")) -join ";") @{ WFC_SKIPINTRO = "1" }
$tf = @(Get-ChildItem $d3 -Filter "t*.bmp" -ErrorAction SilentlyContinue | Sort-Object Name)
if ($tf.Count) {
    $verd = @($tf | ForEach-Object { Present-SceneVerdict (Present-Measure $_.FullName $script:PresentRegions.title_scene) })
    $chg = @(); for ($i = 1; $i -lt $tf.Count; $i++) { $chg += (Present-Similar $tf[$i - 1].FullName $tf[$i].FullName $script:PresentRegions.title_scene).grad }
    $still = @($chg | Where-Object { $_ -gt 0.995 }).Count
    Res "title.matinee.frames" $(if (@($verd | Where-Object { $_ -eq "FAIL" }).Count) { "FAIL" } elseif ($still -gt 2) { "FAIL" } else { "PASS" }) ("12 samples over 60 s: scene verdicts {0}; consecutive frames practically identical {1} (a frozen title FAILS)" -f (($verd | Group-Object | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", "), $still) "Frontend/Rendering"
    $F3 = Read-FlowLog (Join-Path $d3 "flow.jsonl"); $views = @(Flow-Ev $F3 "scene.view")
    $authored = @($X.title_matinees | ForEach-Object { $_.comment } | Where-Object { $_ } | Select-Object -Unique)
    $playing = @($views | ForEach-Object { "$($_.playing)" -split "," } | Where-Object { $_ } | Select-Object -Unique)
    $invented = @($playing | Where-Object { $authored -notcontains $_ }); $core = @("Camera Orbiter", "Primary Camera" | Where-Object { $playing -notcontains $_ })
    $pos = @($views | ForEach-Object { "$($_.pos)" } | Select-Object -Unique)
    Res "title.matinee.tracks" $(if (-not $views.Count) { "UNKNOWN" } elseif ($invented.Count -or $core.Count) { "FAIL" } else { "PASS" }) ("playing {0}; not authored {1}; core title loops missing {2}; authored title Interps (all UI levels): {3}" -f ($playing -join ", "), ($invented -join ","), ($core -join ","), ($authored -join ", ")) "Frontend"
    Res "title.matinee.camera" $(if (-not $views.Count) { "UNKNOWN" } elseif ($pos.Count -ge [Math]::Min(6, $views.Count)) { "PASS" } else { "FAIL" }) ("title camera positions over 60 s: {0} distinct of {1} scene.view events (the orbit must keep moving)" -f $pos.Count, $views.Count) "Frontend"
    Res "title.matinee.appearance" "HUMAN" "timing, composition and look of the title animation (fireworks, fly-by, orbit): HUMAN-CHECK-M07 item 1" "Frontend"
}
foreach ($dd in @(Get-ChildItem $OutDir -Directory)) { $tiles = @(Get-ChildItem $dd.FullName -Filter *.bmp | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = "$($dd.Name) $($_.BaseName)" } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_$($dd.Name).png") 4 400 225 } }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$md = @("# M07 frontend presentation", "", "exe: ``$exe``; quit box $quitBox; navcheck $navOn; type: $typeOn", "") + @($res.ToArray() | ForEach-Object { "- $($_.status) **$($_.id)**: $($_.note)" }); $md | Set-Content -Encoding UTF8 (Join-Path $OutDir "FRONTEND.md")
"M07 FRONTEND: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
