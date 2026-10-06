# PER-MAP COMPILE / PRESENTATION SWEEP (tier TARGETED; ~30-60 s per map, one renderer at a time).
# For every launchable multiplayer map: one direct boot (Release, lockstep), then from its log and frames:
#   - original-material build failures ("failed to build; using glTF fallback"), particle materials not compiled
#     ("textured fallback"), Trail2 / Beam2 emitters not drawn, LEGACY RENDERER, out-of-bounds draws (driver-reset
#     class), framebuffer / decode / lightmap warnings - every [warn] / [error] line grouped by message type;
#   - VISUALCHECK (world / BSP draws, materials, noProgram, noDepth, glErr, glDebug, gpu ms);
#   - the world verdict of the frames (HUD band and player excluded), and a clean shutdown.
# Classification per map: PLAYABLE / PLAYABLE WITH VISUAL DEFECTS / STRUCTURAL DEFECT / BLOCKED BY MISSING SOURCE DATA.
#
#   .\tools\fidelity\map-sweep.ps1 -Root work\ab\<target> -OutDir <dir> [-Maps a,b] [-Frames 600] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Maps = @(), [int]$Frames = 600, [int]$ShotFrom = 200, [int]$ShotTo = 440, [int]$ShotStep = 120, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
if (-not $env:WFC_GATE_GPU_WAIT_MIN) { $env:WFC_GATE_GPU_WAIT_MIN = "60" }
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; $X = Get-M07Expectations
$Maps = @($Maps | ForEach-Object { $_ -split "," } | Where-Object { $_ }); if (-not $Maps.Count) { $Maps = @($X.maps | Where-Object { $_.launchable } | ForEach-Object { $_.runtime }) }
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "mapsweep.$id" $status $null $note $owner }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($m in $Maps) {
    $d = Join-Path $OutDir $m; New-Item -ItemType Directory -Force $d | Out-Null
    $lg = Join-Path $d "wfc.log"
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "$m.gpu" "UNKNOWN" "DRIVER / GPU CONTENTION: GPU busy past $($env:WFC_GATE_GPU_WAIT_MIN) min - not run (not a product result)" "Experimental"; continue }
        $t0 = Get-Date
        $r = Invoke-WfcSampled $exe $d @{ WFC_BOOT = "match"; WFC_MAP = $m; WFC_LOCKSTEP = "1"; WFC_SMOKE_FRAMES = "$Frames"; WFC_VISUALCHECK = "1"; WFC_LOGEVERY = "0"; WFC_SHOTEVERY = "$d,$ShotFrom,$ShotTo"; WFC_NOMOUSE = "1"; WFC_AUTOTURN = "0.3" } 240 1.0
        @{ seconds = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1); rc = $r.rc; timedOut = $r.timedOut } | ConvertTo-Json | Set-Content (Join-Path $d "process.json")
        # keep every $ShotStep-th frame of the captured window (several views while the camera turns)
        Get-ChildItem $d -Filter "f*.bmp" | Where-Object { ([int]($_.BaseName.Substring(1)) - $ShotFrom) % $ShotStep -ne 0 } | Remove-Item -Force
    }
    if (-not (Test-Path $lg)) { continue }
    $lines = @([IO.File]::ReadLines($lg))
    $proc = if (Test-Path (Join-Path $d "process.json")) { Get-Content -Raw (Join-Path $d "process.json") | ConvertFrom-Json } else { $null }
    $clean = [bool]($lines | Where-Object { $_ -match 'Shutdown complete' } | Select-Object -First 1)
    $cnt = { param($p) @($lines | Where-Object { $_ -match $p }).Count }
    $matFb = @($lines | Where-Object { $_ -match 'material (\S+) failed to build; using glTF fallback' } | ForEach-Object { [regex]::Match($_, 'material (\S+) failed').Groups[1].Value } | Select-Object -Unique)
    $ptFb = @($lines | Where-Object { $_ -match 'particle material (\S+) not compiled' } | ForEach-Object { [regex]::Match($_, 'particle material (\S+) not').Groups[1].Value } | Select-Object -Unique)
    $ribbon = @($lines | Where-Object { $_ -match 'is (Trail2|Beam2); not drawn yet' }).Count
    $legacy = & $cnt 'LEGACY RENDERER'; $oob = & $cnt 'out of bounds .*not drawn'
    $fbInc = & $cnt 'framebuffer incomplete'; $decode = & $cnt '(texture|cubemap) decode failed'; $vlm = & $cnt 'vertex lightmap .* not bound'
    $missing = @($lines | Where-Object { $_ -match 'bsp\.glb missing' } | ForEach-Object { "bsp.glb" })   # decals.glb with 0 source decals is authentic (INFO)
    $noDecals = [bool]($lines | Where-Object { $_ -match 'decals\.glb missing' } | Select-Object -First 1)
    $srcLm = Join-Path "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$m" "lightmaps"
    $lmMissing = @($lines | Where-Object { $_ -match 'texture decode failed: .*lightmaps[\/](\S+)\.png' } | ForEach-Object { [regex]::Match($_, 'lightmaps[\/](\S+)\.png').Groups[1].Value } | Select-Object -Unique | Where-Object { -not (Test-Path (Join-Path $srcLm "$_.png")) })
    # every warn / error grouped by message type (numbers and names stripped)
    $wl = @($lines | Where-Object { $_ -match '^\[(warn|error)' } | ForEach-Object { ($_ -replace '^\[(warn|error) *\] ', '$1: ') -replace '[A-Za-z0-9_]+\.[A-Za-z0-9_.]+', '<name>' -replace '\d+(\.\d+)?', 'N' } | Group-Object | Sort-Object Count -Descending)
    $vc = @($lines | Where-Object { $_ -match 'VISUALCHECK frame' })
    $num = { param($l, $k) $mm = [regex]::Match($l, "\b$k=([\d.]+)"); if ($mm.Success) { [double]$mm.Groups[1].Value } else { $null } }
    $vcLast = if ($vc.Count) { $vc[-1] } else { "" }
    $world = & $num $vcLast 'world'; $bsp = & $num $vcLast 'bsp'; $mats = & $num $vcLast 'materials'; $noProg = & $num $vcLast 'noProgram'
    $glErr = (@($vc | ForEach-Object { & $num $_ 'glErr' }) | Measure-Object -Maximum).Maximum
    $glDbg = (@($vc | ForEach-Object { & $num $_ 'glDebug' }) | Measure-Object -Maximum).Maximum
    $gpu = @($vc | ForEach-Object { & $num $_ 'gpu' } | Where-Object { $_ -ne $null }); $gpuMed = if ($gpu.Count) { ($gpu | Sort-Object)[[int]($gpu.Count / 2)] } else { $null }
    $path = if ($vcLast -match 'path=(\S+)') { $Matches[1] } else { "?" }
    $ws = @(Get-ChildItem $d -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { Present-World $_.FullName } | Where-Object { $_ })
    $wv = Present-WorldSetVerdict $ws
    # dark but intact: every frame textured (untextured < 0.15), noise-free, and dark (black >= 0.25) - with the original-material path drawing
    $dark = $ws.Count -and -not @($ws | Where-Object { $_.untexFrac -ge 0.15 -or $_.noise -ge 0.05 -or $_.black -lt 0.25 -or $_.black -ge 0.92 -or $_.detail -lt 0.02 }).Count -and $path -eq "original" -and -not $noProg
    $cls = if (-not $clean -or $oob -or $fbInc) { "STRUCTURAL DEFECT" }
           elseif ($missing.Count -or $legacy) { "BLOCKED BY MISSING SOURCE DATA" }
           elseif ($wv -eq "FAIL" -and $dark) { "PLAYABLE - DARK (HUMAN CHECK)" }   # textured, no noise, just dark: brightness vs the original is a human / reference question
           elseif ($wv -eq "FAIL") { "STRUCTURAL DEFECT" }
           elseif ($matFb.Count -or $ptFb.Count -or $decode -or $vlm -or $wv -eq "PARTIAL" -or ($glErr -gt 0)) { "PLAYABLE WITH VISUAL DEFECTS" }
           else { "PLAYABLE" }
    $row = [pscustomobject][ordered]@{ map = $m; class = $cls; clean_exit = $clean; seconds = $(if ($proc) { $proc.seconds }); path = $path; world_verdict = $wv
        world_detail = (($ws | ForEach-Object { $_.detail }) -join "/"); world_draws = $world; bsp = $bsp; materials = $mats; noProgram = $noProg; glErr = $glErr; glDebug = $glDbg; gpu_ms = $gpuMed
        material_fallbacks = ($matFb -join ","); particle_fallbacks = ($ptFb -join ","); ribbons_not_drawn = $ribbon; legacy = $legacy; out_of_bounds = $oob; fb_incomplete = $fbInc; decode_failed = $decode; lightmaps_absent_in_source = ($lmMissing -join ","); no_decals = $noDecals; vlm_unbound = $vlm; missing = ($missing -join ",")
        warn_types = (($wl | Select-Object -First 6 | ForEach-Object { "$($_.Count)x $($_.Name)" }) -join " | ") }
    $rows.Add($row)
    $st = switch ($cls) { "PLAYABLE" { "PASS" } "PLAYABLE WITH VISUAL DEFECTS" { "PARTIAL" } "PLAYABLE - DARK (HUMAN CHECK)" { "HUMAN" } default { "FAIL" } }
    Res "$m" $st ("{0}: world {1} ({2}); draws world {3} / BSP {4}, materials {5}, noProgram {6}, glErr {7}, glDebug {8}, gpu {9} ms; material fallbacks {10}; particle fallbacks {11}; ribbons not drawn {12}; legacy {13}; out-of-bounds {14}; clean exit {15}" -f $cls, $wv, $row.world_detail, $world, $bsp, $mats, $noProg, $glErr, $glDbg, $gpuMed, $(if ($matFb.Count) { $matFb -join "," } else { 0 }), $(if ($ptFb.Count) { $ptFb -join "," } else { 0 }), $ribbon, $legacy, $oob, $clean) $(if ($cls -eq "PLAYABLE") { "" } else { "Rendering" })
}
Write-WfcCsv $rows (Join-Path $OutDir "mapsweep.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
$tiles = @(Get-ChildItem $OutDir -Recurse -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object FullName | ForEach-Object { @{ png = $_.FullName; label = "$(Split-Path (Split-Path $_.FullName) -Leaf) $($_.BaseName)" } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_maps.png") 3 480 270 }
Write-M07Matrix $rows @("map", "class", "world_verdict", "world_detail", "world_draws", "materials", "noProgram", "glErr", "glDebug", "gpu_ms", "material_fallbacks", "particle_fallbacks", "ribbons_not_drawn", "legacy", "out_of_bounds", "clean_exit") (Join-Path $OutDir "MAPSWEEP.md") "Per-map compile / presentation sweep" @("build: ``$sha`` (``$exe``)", "", "One lockstep direct boot per map ($Frames frames). The world verdict uses the frames (HUD band and player excluded). Warning types per map: mapsweep.csv (warn_types).")
"MAP SWEEP: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
