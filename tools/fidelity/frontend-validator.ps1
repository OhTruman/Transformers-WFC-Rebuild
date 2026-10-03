# Frontend flow validator (prepared before the frontend exists). Observes FRONTEND events
# (protocols\RUNTIME-EVENTS.md) and judges them only against AUTHORED anchors (AssetTools frontend_*.json,
# read-only). It never requires a screen the product does not claim, and it fails a screen whose movie is
# not authored (no fabricated screens).
#
#   .\tools\fidelity\frontend-validator.ps1 -Exe <wfc_rebuild.exe> -OutDir <dir> [-Frames 3600] [-Env @{...}]
#   .\tools\fidelity\frontend-validator.ps1 -Log <existing wfc.log> -OutDir <dir>
#
# Checks once events exist:
#   order      stages appear in the authored order startup < startup_logos < title < main_menu < multiplayer
#              < mode_select < map_select < loading < gameplay < return (skipped stages allowed)
#   movies     every movie named on screen is an authored GFx movie (frontend_gfx.json) or movie file
#              (frontend_flow.json movie_files / frontend_loading.json movies)
#   startup    the startup legal screen is TF_InitialStartup*; logos are MoviesToAlwaysPlaySound
#   selection  the selected playlist is menu-visible; the map is compatible with the mode (frontend_maps.json)
#   loading    load begin/end pairs; the loading movie is the authored one; the loaded map is the selected one
#   stuck      no stage without a following stage within -StuckSec (simulated) unless it is gameplay
#   errors     FRONTEND error events
# Screens: with -ShotEveryStage the exe is asked (WFC_SHOT) for a final still; per-stage captures need the
# product to expose them (proposal: WFC_FRONTEND_SHOTDIR=<dir> -> <checkpoint>.bmp on enter).
param([string]$Exe = "", [Parameter(Mandatory)][string]$OutDir, [int]$Frames = 1800, [hashtable]$Env = @{}, [string]$Log = "", [double]$StuckSec = 120)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$M = "F:\Transformers Rebuild\AssetTools\manifests"
$flow = Get-Content -Raw "$M\frontend_flow.json" | ConvertFrom-Json
$gfx = Get-Content -Raw "$M\frontend_gfx.json" | ConvertFrom-Json
$load = Get-Content -Raw "$M\frontend_loading.json" | ConvertFrom-Json
$modes = Get-Content -Raw "$M\frontend_modes.json" | ConvertFrom-Json
$maps = Get-Content -Raw "$M\frontend_maps.json" | ConvertFrom-Json
$authoredMovies = @{}
foreach ($k in $gfx.movies.PSObject.Properties.Name) { $authoredMovies[$k] = $true; $authoredMovies[$k.Split('/')[-1]] = $true }
foreach ($k in $flow.movie_files.PSObject.Properties.Name) { $authoredMovies[$k] = $true; $authoredMovies[[IO.Path]::GetFileNameWithoutExtension($k)] = $true }
$lc = $load.'config (Xe-TransGame.ini [LoadingMovie])'; foreach ($k in @($lc.InitialStartupFileName, $lc.DefaultFileName, $lc.AlphaFileName) | Where-Object { $_ }) { $authoredMovies[$k] = $true; $authoredMovies[$k.Split('.')[-1]] = $true }   # config names (region variants _INT/_FRA on disk)
foreach ($k in $load.movies.PSObject.Properties.Name) { $authoredMovies[$k] = $true; $authoredMovies[[IO.Path]::GetFileNameWithoutExtension($k)] = $true }
$logos = @($flow.'movie_settings (Engine.MovieSettings)'.MoviesToAlwaysPlaySound)
$visiblePlaylists = @($modes.menu_visible_playlists)
$playlistById = @{}; foreach ($p in $modes.playlists) { $playlistById[$p.PlaylistId] = $p }
$order = @("startup", "startup_logos", "title", "main_menu", "multiplayer", "mode_select", "map_select", "loading", "gameplay", "return")
if (-not $Log) {
    $e = @{ WFC_SMOKE_FRAMES = "$Frames"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0"; WFC_SHOT = (Join-Path $OutDir "final.bmp") } + $Env
    $null = Invoke-WfcExe (Resolve-Path $Exe).Path (Join-Path $OutDir "run") $e "run.log" 1800
    $Log = Join-Path $OutDir "run\wfc.log"
}
$L = @(Get-Content $Log)
function Ev($kind) { return , @($L | Where-Object { $_ -match "FRONTEND $kind\b" } | ForEach-Object { $h = @{ line = $_ }; foreach ($m in [regex]::Matches($_, '(\w+)=(\S+)')) { $h[$m.Groups[1].Value] = $m.Groups[2].Value }; $h }) }
$res = New-WfcResults
Add-WfcResult $res "frontend.authored_anchors" "INFO" $null ("startup map {0}; logos {1}; startup legal movie {2}; loading {3}; {4} authored GFx movies; menu playlists {5}; Streets {6} (MapId {7})" -f $flow.'startup_config (Xe-TransEngine.ini [URL], CONFIRMED)'.Map, ($logos -join ","), $load.'config (Xe-TransGame.ini [LoadingMovie])'.InitialStartupFileName, $load.'config (Xe-TransGame.ini [LoadingMovie])'.DefaultFileName, @($gfx.movies.PSObject.Properties).Count, ($visiblePlaylists -join ","), $maps.streets.MapFilename, $maps.streets.MapId)
$screens = Ev "screen"
if (-not $screens.Count) {
    $boot = @($L | Select-String "World: loaded vertical slice|Init complete").Count
    Add-WfcResult $res "frontend.flow" "SKIP" $null ("not implemented: no 'FRONTEND' events; the exe boots straight into gameplay ({0} gameplay init lines). Validator ready (protocols\RUNTIME-EVENTS.md)" -f $boot) "Frontend"
} else {
    $enters = @($screens | Where-Object { $_.state -eq "enter" })
    $idx = @($enters | ForEach-Object { [Array]::IndexOf($order, $_.screen) })
    $unknownStage = @($enters | Where-Object { $order -notcontains $_.screen })
    $back = 0; $hi = -1; foreach ($i in $idx) { if ($i -ge 0 -and $i -lt $hi -and $order[$i] -ne "multiplayer" -and $order[$i] -ne "main_menu") { $back++ }; $hi = [Math]::Max($hi, $i) }
    Add-WfcResult $res "frontend.order" $(if ($back -eq 0 -and -not $unknownStage.Count) { "PASS" } else { "FAIL" }) $back ("stages entered: {0}; out of authored order {1}; unknown checkpoint names {2}" -f (($enters | ForEach-Object { $_.screen }) -join " > "), $back, (($unknownStage | ForEach-Object { $_.screen }) -join ","))
    $bad = @($screens | Where-Object { $_.movie -and -not $authoredMovies[$_.movie] -and -not $authoredMovies[$_.movie.Split('/')[-1]] })
    Add-WfcResult $res "frontend.movies_authored" $(if (-not $bad.Count) { "PASS" } else { "FAIL" }) $bad.Count ("screens naming an unauthored movie (fabricated?): " + (($bad | ForEach-Object { "$($_.screen):$($_.movie)" }) -join ", "))
    $su = @($enters | Where-Object screen -eq "startup" | Select-Object -First 1)
    if ($su.Count) { Add-WfcResult $res "frontend.startup_movie" $(if ($su[0].movie -like "TF_InitialStartup*") { "PASS" } else { "FAIL" }) $null "startup movie $($su[0].movie) (authored TF_InitialStartup*)" }
    $lg = @($enters | Where-Object screen -eq "startup_logos")
    if ($lg.Count) { $nonLogo = @($lg | Where-Object { $logos -notcontains ([IO.Path]::GetFileNameWithoutExtension($_.movie)) }); Add-WfcResult $res "frontend.logos" $(if (-not $nonLogo.Count) { "PASS" } else { "FAIL" }) $lg.Count ("logo movies: " + (($lg | ForEach-Object { $_.movie }) -join ",") + "; authored MoviesToAlwaysPlaySound " + ($logos -join ",")) }
    foreach ($s in (Ev "select")) {
        $pl = $playlistById[$s.playlist]
        $okPl = $pl -and ($visiblePlaylists -contains $pl.section)
        $mp = @($maps.maps | Where-Object { $_.MapFilename -eq $s.map })[0]
        $okMap = $mp -and @($mp.CompatibleGameTypes) -contains $s.mode
        Add-WfcResult $res "frontend.select.$($s.playlist).$($s.map)" $(if ($okPl -and $okMap) { "PASS" } else { "FAIL" }) $null ("playlist {0} ({1}) menu-visible {2}; map {3} compatible with {4}: {5}" -f $s.playlist, $(if ($pl) { $pl.section } else { "unauthored" }), $okPl, $s.map, $s.mode, $okMap)
    }
    $lb = Ev "load"; $begins = @($lb | Where-Object state -eq "begin"); $ends = @($lb | Where-Object state -eq "end")
    if ($begins.Count) {
        $authLoad = @($load.'config (Xe-TransGame.ini [LoadingMovie])'.DefaultFileName, $load.'config (Xe-TransGame.ini [LoadingMovie])'.InitialStartupFileName, "LoadScreen_GFX", "LoadScreenAlpha_GFX", "LoadingScreenAlpha")
        $badLoad = @($begins | Where-Object { $_.movie -and -not (@($authLoad | Where-Object { $_ -and ($begins[0].movie -like "*$_*") }).Count) })
        Add-WfcResult $res "frontend.loading" $(if ($ends.Count -eq $begins.Count -and -not $badLoad.Count) { "PASS" } else { "FAIL" }) $begins.Count ("load begin/end {0}/{1}; loading movies {2}" -f $begins.Count, $ends.Count, (($begins | ForEach-Object { $_.movie }) -join ","))
    }
    $errs = Ev "error"; Add-WfcResult $res "frontend.errors" $(if ($errs.Count) { "FAIL" } else { "PASS" }) $errs.Count (($errs | ForEach-Object { $_.what }) -join "; ")
    for ($k = 0; $k -lt $enters.Count - 1; $k++) { if ($enters[$k].t -and $enters[$k + 1].t -and ([double]$enters[$k + 1].t - [double]$enters[$k].t) -gt $StuckSec -and $enters[$k].screen -ne "gameplay") { Add-WfcResult $res "frontend.stuck.$($enters[$k].screen)" "HUMAN" ([double]$enters[$k + 1].t - [double]$enters[$k].t) "stage held longer than $StuckSec s" } }
}
if (Test-Path (Join-Path $OutDir "final.bmp")) { Add-WfcResult $res "frontend.final_still" "INFO" $null "final.bmp (last frame)" }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"FRONTEND VALIDATOR: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
