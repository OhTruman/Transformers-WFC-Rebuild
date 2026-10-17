<#
.SYNOPSIS
  Isolation check for a slim playtest package: does the game, run from the package, read every data file from the package?

.DESCRIPTION
  For each map (default Streets 508, Gorge 510, Debris 507) the package's exe boots cold through the frontend flow into a
  TDM match, plays MatchSeconds, and quits. Every file the exe opens is logged (WFC_FILELOG) and classified:

    package  - under <Package>\Transformers Rebuild (the subst F: target of Play.bat)
    run      - the run folder (copied bin, wfc.log, wfc_profile.ini, file log)
    system   - Windows / driver / user-profile paths (not data)
    outside  - under the real F:\Transformers Rebuild but not inside the package (ExtractedAssets, Game Dump, AssetTools,
               the dev Rebuild tree ...): the package depends on a file it does not ship
    other    - anything else (reported)

  FAIL when, for any map:
    - a read resolved outside the package (class outside);
    - the exe wrote into the package (the package must stay read-only);
    - a read failed inside the package for a file the full tree has (omission: the package is missing it), unless the
      package ships its compressed twin (X.xpr, platform/FileCompression.h) or, for X.wav, the FSB bank X.fsb;
    - the match was not reached, the exe did not shut down cleanly, or it had to be killed;
    - wfc.log has [error] lines;
    - localized waves: any "not played", or fewer than -ExpectedLocLines from the _LOC twin on Streets (the 282-line check).
  Failed opens of files that exist in neither the package nor the full tree are optional probes (reported, not a fail).

  On this machine F: is the real drive, so Play.bat's subst cannot run. Instead the exe runs from a copy of the
  package's bin folder (the package itself is never written to) with every data root pointed into the package
  (WFC_ASSETS / WFC_ASSET_ROOT / WFC_CONTENT / WFC_RENDER_DATA / WFC_MANIFESTS / WFC_FRONTEND_MANIFESTS /
  WFC_ASSETTOOLS_MANIFESTS / WFC_DDS_ROOT). A data path with no override, still hard-coded to F:\Transformers Rebuild,
  shows up as an "outside" read: on a laptop with Play.bat's subst it would resolve into the package, so outside reads
  are also checked against the package (reported as "outside, package has it" vs "outside, package LACKS it").
  "Package LACKS it" fails (the laptop would break). "Package has it" fails when the path is under ExtractedAssets,
  Game Dump or AssetTools (a data root that should have been overridden) and is a warning elsewhere (e.g. Rebuild\data
  read through the compile-time WFC_SOURCE_DIR, which Play.bat's subst resolves); -Strict makes every outside read fail.

  Limit: WFC_FILELOG sees opens made by the exe's own code (its import table), not by other DLLs.

.EXAMPLE
  .\tools\package\verify_slim.ps1 -Package "F:\Transformers Rebuild\Rebuild\work\playtest_slim"
.EXAMPLE
  Self-test (must FAIL with outside reads under ExtractedAssets; WFC_CONTENT alone is not enough, its default derives
  from the asset root, which still points into the package):
  .\tools\package\verify_slim.ps1 -Package <pkg> -Maps 508:Streets -OmitRoot WFC_ASSETS,WFC_CONTENT -SkipIntro
#>
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$WorkDir = "",
    [string]$Maps = "508:Streets,510:Gorge,507:Debris",
    [int]$MatchSeconds = 20,
    [int]$TimeoutSeconds = 300,
    [int]$ExpectedLocLines = 282,
    [string]$LobbyOptions = "",
    [string]$HoldFile = "",
    [switch]$SkipIntro,
    [switch]$Strict,
    [string[]]$OmitRoot = @(),   # self-test: leave these data-root variables unset (the check must then FAIL)
    [switch]$NoHd,               # skip the optional HD pack checks (static and runtime)
    [switch]$HdForced,           # force HD on (WFC_HDTEXTURES=<HdLevel>): the pack's textures load (else the first launch must keep HD off)
    [int]$HdLevel = 1,           # the HD level -HdForced sets: 1 Faithful, 2 Enhanced (Modern materials + the M6 roughness maps)
    [switch]$StaticOnly,         # file checks only (HD pack), no game runs
    [string]$ExeBin = "",           # candidate exe tree: its *.exe / *.dll replace the package's in the run copy (the package stays as is)
    [string]$RtBuildDir = "",    # the cmake build tree the package's exe came from: rt-build-check's build half (build.ninja, generated/rt)
    [string]$RtCheck = "",       # Experimental's tools/fidelity/rt-build-check.ps1 (default: beside this repo's tools/package)
    [switch]$NoRtCheck,          # skip the Ray Tracing gate (a deliberately RT-less package)
    [int]$SettingsExpected = 0,  # WFC_SETTINGSTEST: the number of renderer settings checks expected (0 = any count; all must pass)
    [switch]$NoSettingsTest,     # skip the settings self-test run
    [switch]$NoHdRuntime,        # skip the HD runtime row (a default run otherwise re-runs one map with HD forced on: the loader must accept the pack)
    [switch]$ForeignCwd,         # start the map runs from a NOT writable working directory (<map>\cwd, write denied to this user): wfc.log / wfc_profile.ini must land beside the exe instead (needs Systems' log-dir patch; the writable-cwd case keeps the cwd for harnesses)
    [string]$RtHoldFile = "",    # the RT gate (only) waits while this file exists: the RT-off steps run first, the gate starts once the RT slot is granted and the file is removed
    [string]$SubstDrive = ""     # e.g. X: map the package to this free drive letter for the run (as Play.bat maps F:), so the game
                                 # sees the laptop's path lengths: a package deep in a work folder pushes long HD paths past
                                 # MAX_PATH (260) that are 213 chars under Play.bat's F:. Removed at the end.
)
$ErrorActionPreference = 'Stop'

# Package 6 Realistic defaults (Integration's list, 6155925b; refreshed with the final head): the RT gate's Realistic run (every
# WFC_* cleared) must log "RT Realistic defaults (first Realistic frame): k=v k=v ..." (Rendering) with these values. Strings
# compare exactly, numbers within 1e-3. A key the line has but this table lacks is reported (INFO), not a fail.
$RealisticDefaults = [ordered]@{
    tone = 'on'          # X1 tone curve (WFC_RT_TONE=0 off; WFC_TONE_ADAPT / _BLOOM / _CONTRAST / _GAIN tune it)
    lmspecgain = 2.5     # X2 specular gain (WFC_RT_LMSPECGAIN)
    tlgain = 2           # row 4 transient light gain (WFC_RT_TLGAIN)
    gisat = 2.5          # X4 coloured bounce saturation (WFC_RT_GISAT)
    giemissive = 4       # X4 emissive bounce gain (WFC_RT_GIEMISSIVE; 4 from Rendering e233a641, the Molten lava)
    charao = 3           # X5-1 character AO radius, m (WFC_RT_CHARAORADIUS)
    softparticles = 'on' # WFC_SOFTPARTICLES=0 off
    tiled = 'on'         # tiled transient lights (WFC_TLTILED=0 off)
    charsun = 'on'       # row 1 (WFC_RT_CHARSUN)
    charlampcal = 'on'   # row 8 (WFC_RT_CHARLAMPCAL)
    refltl = 'on'        # row 6 (WFC_RT_REFLTL)
    sunshare = 'on'      # row 2 (WFC_RT_SUNSHARE)
    probefill = 'on'     # row 5 (WFC_RT_PROBEFILL)
    origspec = 'on'      # Enhanced: the Modern lobe only where the original had a specular input (WFC_PBR_ORIGSPEC=0 restores it)
    refltemporal = 'on'  # item 7 temporal reflections (WFC_RT_REFLTEMPORAL; Rendering 8d50190a)
    sunsharemax = 0.75   # row 2 character sun-shadow cap (WFC_RT_SUNSHAREMAX; Rendering 18483783)
    tonekey = 0.02       # X1 default key (WFC_TONE_KEY; Molten applies its own 0.03 at runtime, the line prints the default)
    toneadapt = 0.65     # X1 eye adaptation (WFC_TONE_ADAPT)
    tonegain = 1.3       # X1 gain (WFC_TONE_GAIN)
    tonesat = 1.1        # X1 saturation (WFC_TONE_SAT)
    tonecontrast = 1.15  # X1 contrast (WFC_TONE_CONTRAST)
    tonebloom = 0.8      # X1 bloom (WFC_TONE_BLOOM)
    bloommax = 16        # X1 soft-bloom input cap (WFC_TONE_BLOOMMAX)
    whiteroll = 'on'     # X1 very bright pixels desaturate toward white before the shoulder (WFC_TONE_WHITEROLL=0 off)
    whitelo = 1.5        # white-roll start (WFC_TONE_WHITELO; retuned after the Gorge recheck)
    whitehi = 6          # white-roll end (WFC_TONE_WHITEHI)
    whitemix = 0.5       # white-roll strength, keeps the warm tint (WFC_TONE_WHITEMIX)
    x3 = 'on'            # X3 froxel volumetrics (WFC_X3=0 off; Rendering 607baafd) - global defaults, the per-map table is not on the line
    x3density = 0.3      # medium = authored height fog x density (WFC_X3_DENSITY)
    x3haze = 0.003       # haze floor, 1 / m (WFC_X3_HAZE)
    x3sun = 0.6          # sun in-scatter, peak-normalised (WFC_X3_SUN)
    x3lamp = 2           # in-view lamps (WFC_X3_LAMP)
    x3trans = 1          # transient lights (WFC_X3_TRANS)
    x3g = 0.5            # phase g (WFC_X3_G)
    x3dist = 120         # froxel range, m (WFC_X3_DIST)
    x3history = 0.9      # temporal history (WFC_X3_HISTORY)
    x3lampcore = 2       # lamp core size (WFC_X3_LAMPCORE)
    x3tonecontrast = 1.2 # X1 contrast on X3 frames (WFC_X3_TONECONTRAST)
}

function Norm([string]$p) {
    # long-path opens (platform::longPath) log as \\?\F:\... or \\?\UNC\server\...: classify them by the path itself
    if ($p.StartsWith('\\?\UNC\')) { $p = '\\' + $p.Substring(8) } elseif ($p.StartsWith('\\?\')) { $p = $p.Substring(4) }
    return ($p -replace '\\', '/').TrimEnd('/').ToLowerInvariant()
}
function Under([string]$path, [string]$root) { $r = $root + '/'; return $path.StartsWith($r) -or $path -eq $root }

$pkgTop = (Resolve-Path -LiteralPath $Package).Path
$substMade = $false
if ($SubstDrive) {
    $SubstDrive = $SubstDrive.TrimEnd('\', ':').ToUpperInvariant() + ':'
    if (Test-Path -LiteralPath "$SubstDrive\") { throw "drive $SubstDrive is already in use: pick a free letter" }
    $null = cmd /c subst $SubstDrive "$pkgTop"
    if (-not (Test-Path -LiteralPath "$SubstDrive\Transformers Rebuild")) { throw "subst $SubstDrive -> $pkgTop failed" }
    $substMade = $true
    Write-Host "package mapped to $SubstDrive (from $pkgTop)"
    $pkgTop = "$SubstDrive\"
}
function RemoveSubst { if ($substMade) { $null = cmd /c subst $SubstDrive /d; $script:substMade = $false; Write-Host "subst $SubstDrive removed" } }
$pkg = Join-Path $pkgTop 'Transformers Rebuild'
$pkgBin = Join-Path $pkg 'Rebuild\build\release\bin'
if (-not (Test-Path -LiteralPath (Join-Path $pkgBin 'wfc_rebuild.exe'))) { RemoveSubst; throw "no package exe at $pkgBin\wfc_rebuild.exe" }
$exeHash = (Get-FileHash -LiteralPath (Join-Path $(if ($ExeBin) { $ExeBin } else { $pkgBin }) 'wfc_rebuild.exe') -Algorithm SHA256).Hash.Substring(0, 16)   # before any subst is removed
$realTop = 'F:\Transformers Rebuild'
if (-not $WorkDir) { $WorkDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) ('work\verify_slim\' + (Get-Date -Format 'yyyyMMdd_HHmmss')) }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$WorkDir = (Resolve-Path -LiteralPath $WorkDir).Path
if ((Norm $WorkDir).StartsWith((Norm $pkgTop))) { throw "WorkDir must be outside the package" }

$nPkg = Norm $pkg; $nReal = Norm $realTop; $nWork = Norm $WorkDir
$systemRoots = @($env:SystemRoot, $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramData, $env:USERPROFILE, $env:TEMP) |
    Where-Object { $_ } | ForEach-Object { Norm $_ }

# Data roots inside the package (see .DESCRIPTION).
$roots = [ordered]@{
    WFC_ASSETS               = "$pkg\ExtractedAssets\VerticalSlice"
    WFC_ASSET_ROOT           = "$pkg\ExtractedAssets\VerticalSlice"
    WFC_CONTENT              = "$pkg\ExtractedAssets\content"
    WFC_RENDER_DATA          = "$pkg\Rebuild\work\render"
    WFC_MANIFESTS            = "$pkg\AssetTools\manifests"
    WFC_FRONTEND_MANIFESTS   = "$pkg\AssetTools\manifests"
    WFC_ASSETTOOLS_MANIFESTS = "$pkg\AssetTools\manifests"
    WFC_DDS_ROOT             = "$pkg\AssetTools\out\dds"
    WFC_FRONTEND_DATA        = "$pkg\Rebuild\data\frontend"    # Frontend's runtime lookup (logged "frontend data: <dir>")
}
foreach ($k in $OmitRoot) { if ($roots.Contains($k)) { $roots.Remove($k); Write-Warning "self-test: $k left unset" } else { throw "unknown root $k" } }
foreach ($k in $roots.Keys) { if (-not (Test-Path -LiteralPath $roots[$k])) { Write-Warning "$k -> $($roots[$k]) does not exist in the package" } }

# ---- the optional HD texture pack (M4; [PCSettings] HDTextures=1) ----
# The loader (WfcPipeline.cpp hdIndex) reads <hd root>/hd_index.json, default <dds root>/../hd = AssetTools\out\hd (what
# Play.bat's subst resolves), or WFC_HD_ROOT. It ignores the whole pack unless every row's file exists with its byte
# count and a supported format. Static check: every row present (+ bytes, format), every DDS under the root indexed (the
# pack's source dir also holds non-PASS textures that must not ship). Runtime: with a pack, the maps run with
# WFC_HDTEXTURES=1 and the log must say "HD texture pack available: <rows> textures". -NoHd skips both.
$hd = [pscustomobject]@{ Root = $null; Where = 'not shipped'; Rows = 0; Files = 0; Bytes = 0L; Problems = @(); PbrRows = 0 }
if (-not $NoHd) {
    $cands = @(@{ Dir = Join-Path $pkg 'AssetTools\out\hd'; Where = 'AssetTools\out\hd (loader default)' },
               @{ Dir = Join-Path $pkgTop 'hd'; Where = '<package>\hd (needs WFC_HD_ROOT: the loader default is AssetTools\out\hd)' })
    $found = @($cands | Where-Object { Test-Path -LiteralPath (Join-Path $_.Dir 'hd_index.json') })
    $strays = @($cands | Where-Object { (Test-Path -LiteralPath $_.Dir) -and -not (Test-Path -LiteralPath (Join-Path $_.Dir 'hd_index.json')) })
    foreach ($s in $strays) {
        $dev = Test-Path -LiteralPath (Join-Path $s.Dir 'pack\hd_index.json')
        $hd.Problems += "HD folder without hd_index.json at its root: $($s.Dir)$(if ($dev) { ' (dev layout pack\hd_index.json: the package ships the root form, indexed files only)' })"
    }
    if ($found.Count -gt 1) { $hd.Problems += 'HD pack shipped twice (AssetTools\out\hd and <package>\hd)' }
    # The shipped layout (Integration, 2026-10-11): the loader default with hd_index.json at its root; -Strict tests only that.
    if ($Strict -and $found.Count -and $found[0].Dir -ne $cands[0].Dir) { $hd.Problems += '-Strict: HD pack must ship at Transformers Rebuild\AssetTools\out\hd (the loader default Play.bat resolves), not <package>\hd' }
    if ($found.Count) {
        $hd.Root = $found[0].Dir; $hd.Where = $found[0].Where
        $J = Get-Content -LiteralPath (Join-Path $hd.Root 'hd_index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $rows = $null
        foreach ($k in 'textures', 'rows', 'entries') { $v = $J.$k; if ($v -is [array] -and $v.Count) { $rows = $v; break } }   # the loader's order
        if (-not $rows) { $hd.Problems += 'hd_index.json has no rows' }
        else {
            $hd.Rows = $rows.Count
            $indexed = New-Object 'System.Collections.Generic.HashSet[string]'
            $okFmt = @('DXT1', 'DXT3', 'DXT5', 'A8R8G8B8', 'G8')
            $miss = 0; $size = 0; $fmt = 0; $shown = 0
            foreach ($r in $rows) {
                if (-not $r.key -or -not $r.dds) { continue }
                [void]$indexed.Add((Norm (Join-Path $hd.Root $r.dds)))
                $f = $r.format -replace '^PF_', ''
                $f = switch ($f) { 'BC1' { 'DXT1' } 'BC2' { 'DXT3' } 'BC3' { 'DXT5' } default { $f } }
                if ($okFmt -notcontains $f) { $fmt++; if ($shown++ -lt 10) { $hd.Problems += "unsupported format $($r.format): $($r.dds)" } }
                $p = Join-Path $hd.Root $r.dds
                $fi = Get-Item -LiteralPath $p -ErrorAction SilentlyContinue
                if (-not $fi) { $miss++; if ($shown++ -lt 10) { $hd.Problems += "row file missing: $($r.dds)" } }
                elseif ($null -ne $r.bytes -and [long]$r.bytes -ne $fi.Length) { $size++; if ($shown++ -lt 10) { $hd.Problems += "byte count $($fi.Length) != $($r.bytes): $($r.dds)" } }
            }
            if ($miss) { $hd.Problems += "$miss of $($rows.Count) rows missing (the loader ignores the whole pack)" }
            if ($size) { $hd.Problems += "$size rows with a wrong byte count (the loader ignores the whole pack)" }
            if ($fmt) { $hd.Problems += "$fmt rows in an unsupported format (the loader ignores the whole pack)" }
            # M6 modern material maps (HD Enhanced; build_slim --hd-pbr): hd_pbr_index.json beside hd_index.json, dds relative to
            # the same root. The loader skips a bad row (that material derives its roughness) - a package ships none.
            $pbrPath = Join-Path $hd.Root 'hd_pbr_index.json'
            if (Test-Path -LiteralPath $pbrPath) {
                $PJ = Get-Content -LiteralPath $pbrPath -Raw -Encoding UTF8 | ConvertFrom-Json
                $prows = $null
                foreach ($k in 'rows', 'textures', 'entries') { $v = $PJ.$k; if ($v -is [array] -and $v.Count) { $prows = $v; break } }
                if (-not $prows) { $hd.Problems += 'hd_pbr_index.json has no rows' }
                else {
                    $hd.PbrRows = $prows.Count
                    $pbad = 0; $pshown = 0
                    foreach ($r in $prows) {
                        if ($r.dds) { [void]$indexed.Add((Norm (Join-Path $hd.Root $r.dds))) }
                        $f = $r.format -replace '^PF_', ''
                        $why = $null
                        if (-not $r.tex -or -not $r.role -or -not $r.dds) { $why = 'tex / role / dds missing' }
                        elseif ($f -ne 'BC4' -and $f -ne 'BC7') { $why = "format $($r.format)" }
                        elseif ($r.space -and $r.space -ne 'linear') { $why = "space $($r.space)" }
                        else {
                            $fi = Get-Item -LiteralPath (Join-Path $hd.Root $r.dds) -ErrorAction SilentlyContinue
                            if (-not $fi) { $why = 'file missing' }
                            elseif ($null -ne $r.bytes -and [long]$r.bytes -ne $fi.Length) { $why = "byte count $($fi.Length) != $($r.bytes)" }
                        }
                        if ($why) { $pbad++; if ($pshown++ -lt 10) { $hd.Problems += "M6 row $($r.dds): $why" } }
                    }
                    if ($pbad) { $hd.Problems += "$pbad of $($prows.Count) M6 modern material rows bad (the loader skips each: that material derives its roughness)" }
                }
            }
            $files = @(Get-ChildItem -LiteralPath $hd.Root -Recurse -File)
            $hd.Files = $files.Count; $hd.Bytes = ($files | Measure-Object -Property Length -Sum).Sum
            $extra = @($files | Where-Object { $_.Name -ne 'hd_index.json' -and $_.Name -ne 'hd_pbr_index.json' -and $_.Extension -ne '.txt' -and -not $indexed.Contains((Norm $_.FullName)) })
            if ($extra.Count) {
                $hd.Problems += "$($extra.Count) files in the HD folder not in the index (non-PASS textures must not ship)"
                foreach ($x in ($extra | Select-Object -First 10)) { $hd.Problems += "unindexed: $($x.FullName.Substring($hd.Root.Length + 1))" }
            }
        }
    }
}
Write-Host ("HD pack: {0}{1}" -f $hd.Where, $(if ($hd.Root) { ": $($hd.Rows) rows + $($hd.PbrRows) M6 modern material rows, $($hd.Files) files, $([math]::Round($hd.Bytes / 1GB, 2)) GB, $($hd.Problems.Count) problems" } else { '' }))

# ---- the package's bin against build_slim.json (tools/package/build_slim.py): every listed file present, nothing else; the
# M5 runtime (FidelityFX DLLs + D3D12\D3D12Core.dll) exactly when the manifest's m5_runtime is set; never the D3D12 debug
# layer. Packages made before the manifest existed are reported, not failed.
$pk = [pscustomobject]@{ Manifest = $null; M5 = $false; Files = 0; Problems = @(); Note = '' }
$manPath = Join-Path $pkgTop 'build_slim.json'
if (Test-Path -LiteralPath $manPath) {
    $pk.Manifest = Get-Content -LiteralPath $manPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $pk.M5 = [bool]$pk.Manifest.m5_runtime
    $want = @{}; foreach ($f in $pk.Manifest.bin_files) { $want[(Norm $f)] = $f }
    $have = @{}
    foreach ($fi in Get-ChildItem -LiteralPath $pkgBin -Recurse -File) {
        if ($fi.Name -match '^(wfc\.log|wfc_profile\.ini|wfc_characters\.ini|wfc_crash_.*|filelog.*\.txt)$') { continue }   # a playtester's run artefacts
        $rel = $fi.FullName.Substring($pkgBin.Length).TrimStart('\', '/') -replace '\\', '/'
        $have[(Norm $rel)] = $rel
    }
    foreach ($k in $want.Keys) { if (-not $have.ContainsKey($k)) { $pk.Problems += "bin file missing (listed in build_slim.json): $($want[$k])" } }
    foreach ($k in $have.Keys) { if (-not $want.ContainsKey($k)) { $pk.Problems += "bin file not in build_slim.json (leaked into the package): $($have[$k])" } }
    foreach ($f in 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll', 'd3d12/d3d12core.dll') {
        if ($pk.M5 -and -not $have.ContainsKey($f)) { $pk.Problems += "M5 runtime flagged but $f is not in the package" }
        if (-not $pk.M5 -and $have.ContainsKey($f)) { $pk.Problems += "M5 runtime not flagged but $f is in the package" }
    }
    if ($have.ContainsKey('d3d12/d3d12sdklayers.dll')) { $pk.Problems += 'D3D12\d3d12SDKLayers.dll (the D3D12 debug layer) must not ship' }
    $pk.Files = $have.Count
    $playBat = Join-Path $pkgTop 'Play.bat'
    if (-not (Test-Path -LiteralPath $playBat)) { $pk.Problems += 'no Play.bat' }
    else {
        $pb = Get-Content -LiteralPath $playBat -Raw
        if ($pb -match 'goto run') { $pk.Problems += 'Play.bat is the old template (runs any F:\...\wfc_rebuild.exe it finds, not the package exe)' }
        if ($pb -notmatch 'pushd "%L%:\\Transformers Rebuild\\Rebuild\\build\\release\\bin"') { $pk.Problems += 'Play.bat does not start the package exe from its own bin' }
    }
    Write-Host ("package bin: {0} files vs build_slim.json {1}; M5 runtime {2}; {3} problems" -f $have.Count, $want.Count, $(if ($pk.M5) { 'shipped' } else { 'not shipped' }), $pk.Problems.Count)
} else {
    $pk.Note = 'no build_slim.json (a package made before tools/package/build_slim.py wrote one): bin contents and the M5 runtime not checked'
    Write-Host "package bin: $($pk.Note)"
}

# ---- glTF overlay (Integration 2026-10-16): the barrier glTF carries extras.wfc_material = WEP_Shield_p.Barrier.WEP_Barrier_Defrag_MATINST
# (decompressed from its .xpr twin) and every file of the FROZEN overlay recorded in build_slim.json ships byte-identical
# (tools/package/check_overlay.py; AssetTools' python). A FAIL line is a package problem.
$ovPy = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
$ovLines = @(& $ovPy (Join-Path $PSScriptRoot 'check_overlay.py') $pkgTop 2>&1 | ForEach-Object { "$_" })
foreach ($l in $ovLines) { Write-Host "overlay: $l"; if ($l -notlike 'PASS *') { $pk.Problems += "glTF overlay: $l" } }
if (-not $ovLines.Count) { $pk.Problems += 'glTF overlay: check_overlay.py printed nothing' }

# ---- render data: every map folder with materials_glsl.json also ships materials_pbr.json (the M6 Modern mode, HD Enhanced;
# Rendering's matc_pbr.py output). Missing: a package problem (the loader keeps HD Faithful working without it).
$renderDir = Join-Path $pkg 'Rebuild\work\render'
$pbrMaps = 0
if (Test-Path -LiteralPath $renderDir) {
    foreach ($d in Get-ChildItem -LiteralPath $renderDir -Directory) {
        if (-not (Test-Path -LiteralPath (Join-Path $d.FullName 'materials_glsl.json'))) { continue }
        if (Test-Path -LiteralPath (Join-Path $d.FullName 'materials_pbr.json')) { $pbrMaps++ }
        else { $pk.Problems += "render data: $($d.Name)\materials_pbr.json missing (the M6 Modern mode; tools/render/matc_pbr.py)" }
    }
    Write-Host "render data: materials_pbr.json in $pbrMaps map folder(s)"
}

function WaitHold {
    if (-not $HoldFile) { return }
    $said = $false
    while (Test-Path -LiteralPath $HoldFile) { if (-not $said) { Write-Host "hold file present ($HoldFile): waiting"; $said = $true }; Start-Sleep -Seconds 2 }
}

# Map a package / run-bin path to its full-tree twin (for the omission check), or $null.
function RealTwin([string]$n) {
    if (Under $n $nPkg) { return $realTop + $n.Substring($nPkg.Length) }
    return $null
}
function PkgTwin([string]$n) {
    if (Under $n $nReal) { return $pkg + $n.Substring($nReal.Length) }
    return $null
}

$results = @()
$setTest = [pscustomobject]@{ Status = $(if ($NoSettingsTest -or $StaticOnly) { 'SKIP' } else { 'FAIL' }); Pass = 0; Fail = 0; Note = 'not run' }
$rtGate = [pscustomobject]@{ Status = $(if ($NoRtCheck -or $StaticOnly) { 'SKIP' } else { 'FAIL' }); Lines = @(); Note = $(if ($NoRtCheck) { '-NoRtCheck' } elseif ($StaticOnly) { '-StaticOnly' } else { 'not run' }) }
$saved = @{}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
# every verify run is silent (user 2026-10-13: Integration's WFC_MUTE=1 - the device gets silence, the mix / cue logic / PCM
# stats unchanged, so the checks still see the audio); env-only, never a package default
$env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'   # silent, never takes the mouse / focus (user 2026-10-13 / 2026-10-15; env-only)
try {
    foreach ($entry in $(if ($StaticOnly) { @() } else { $Maps.Split(',') })) {
        $id, $name = $entry.Split(':')
        WaitHold
        $run = Join-Path $WorkDir $name
        if (Test-Path -LiteralPath $run) { Remove-Item -LiteralPath $run -Recurse -Force }
        $bin = Join-Path $run 'bin'
        New-Item -ItemType Directory -Force -Path $bin | Out-Null
        Copy-Item -Path (Join-Path $pkgBin '*') -Destination $bin -Recurse -Force -Exclude 'wfc.log', 'wfc_profile.ini', 'wfc_crash_*', 'filelog*.txt'
        if ($ExeBin) { Copy-Item -Path (Join-Path $ExeBin '*.exe'), (Join-Path $ExeBin '*.dll') -Destination $bin -Force }   # a candidate build over the package's bin
        $exe = Join-Path $bin 'wfc_rebuild.exe'
        $fileLog = Join-Path $run 'filelog.txt'
        $env:WFC_FILELOG = $fileLog
        foreach ($k in $roots.Keys) { Set-Item -LiteralPath "env:$k" -Value ($roots[$k] -replace '\\', '/') }
        $env:WFC_BOOT = 'frontend'; $env:WFC_NOMOUSE = '1'; $env:WFC_LOGTIME = '1'
        if ($SkipIntro) { $env:WFC_SKIPINTRO = '1' } else { Remove-Item env:WFC_SKIPINTRO -ErrorAction SilentlyContinue }
        if ($LobbyOptions) { $env:WFC_LOBBY_OPTIONS = $LobbyOptions } else { Remove-Item env:WFC_LOBBY_OPTIONS -ErrorAction SilentlyContinue }
        Remove-Item env:WFC_HDTEXTURES -ErrorAction SilentlyContinue
        Remove-Item env:WFC_AUTODETECT, env:WFC_AUTODETECT_WINDOWED -ErrorAction SilentlyContinue
        if ($hd.Root) {
            # user 2026-10-12 (reversed the same day): HD is OFF by default - a first launch (no profile, auto-detect as
            # shipped) must keep it off while the pack is accepted; -HdForced checks that the pack's textures load
            if ($HdForced) { $env:WFC_HDTEXTURES = "$HdLevel" }
            # (Frontend 391706f: a scripted boot keeps the seeded defaults unless auto-detect is asked for; windowed, so no
            # display mode switch. The HD default needs the pack and >= 6000 MB of VRAM: frontend::hdTexturesDefault)
            else { $env:WFC_AUTODETECT = '1'; $env:WFC_AUTODETECT_WINDOWED = '1' }
            if ((Norm $hd.Root) -ne (Norm (Join-Path $pkg 'AssetTools\out\hd'))) { $env:WFC_HD_ROOT = $hd.Root -replace '\\', '/' }
        }
        $env:WFC_FRONTEND_SCRIPT = "wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;call:Online.SetSelectedMapID,$id;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=$MatchSeconds;quit"

        Write-Host "[$name] map ${id}: starting $exe"
        $t0 = Get-Date
        $cwd = $bin
        if ($ForeignCwd) {
            $cwd = Join-Path $run 'cwd'; New-Item -ItemType Directory -Force -Path $cwd | Out-Null
            $null = icacls $cwd /deny "$($env:USERNAME):(W)"   # (a start folder the game cannot write: a shortcut's System32, say)
        }
        $p = Start-Process -FilePath $exe -WorkingDirectory $cwd -PassThru
        $killed = $false
        if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
            $killed = $true
            Write-Warning "[$name] timeout after $TimeoutSeconds s: killing PID $($p.Id)"
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            $p.WaitForExit(15000) | Out-Null
        }
        $secs = [int]((Get-Date) - $t0).TotalSeconds
        if (Get-Process -Id $p.Id -ErrorAction SilentlyContinue) { Write-Warning "[$name] PID $($p.Id) still alive after kill" }

        # ---- the exe's log ----
        $logPath = Join-Path $bin 'wfc.log'
        $log = if (Test-Path -LiteralPath $logPath) { Get-Content -LiteralPath $logPath } else { @() }
        $errors = @($log | Where-Object { $_ -match '\[error\]' })
        $matchLine = ($log | Select-String -SimpleMatch 'FLOW level.begin level=Match' | Select-Object -First 1)
        $inGame = [bool]($log | Select-String -Pattern 'ui\.state from=\S+ to=InGame' | Select-Object -First 1)
        $shutdown = [bool]($log | Select-String -SimpleMatch 'Shutdown complete' | Select-Object -Last 1)
        $hdLine = $log | Select-String -Pattern 'HD texture pack (available: (\d+) textures|incomplete.*|not installed.*|format .* not supported.*|index has no rows.*)' | Select-Object -First 1
        $hdLog = if ($hdLine) { $hdLine.Matches[0].Value } else { 'no HD pack line' }
        $feLine = $log | Select-String -Pattern 'frontend data: (.+)$' | Select-Object -First 1
        $feDir = if ($feLine) { $feLine.Matches[0].Groups[1].Value.Trim() } else { $null }
        $feNote = if (-not $feDir) { 'not logged (exe before the frontend-data lookup)' }
                  elseif (Under (Norm $feDir) $nPkg -or Under (Norm $feDir) $nWork) { 'inside the package' }
                  else { 'OUTSIDE the package' }
        $loc = $null
        $locLines = @($log | Select-String -Pattern 'localized waves \(language (\S+)\): (\d+) from the _LOC twin, (\d+) merged copy of that twin, (\d+) not played')
        if ($matchLine) { $after = @($locLines | Where-Object { $_.LineNumber -gt $matchLine.LineNumber }); if ($after.Count) { $locLines = $after } }
        if ($locLines.Count) {
            $g = $locLines[-1].Matches[0].Groups
            $loc = [pscustomobject]@{ Lang = $g[1].Value; Twin = [int]$g[2].Value; Merged = [int]$g[3].Value; Skipped = [int]$g[4].Value }
        }

        # ---- the file log ----
        $rows = @()
        if (Test-Path -LiteralPath $fileLog) {
            foreach ($line in (Get-Content -LiteralPath $fileLog)) {
                if ($line -notmatch '^(r|w) (ok|missing) (.+)$') { continue }
                $mode = $Matches[1]; $ok = $Matches[2] -eq 'ok'; $raw = $Matches[3]
                $n = Norm $raw
                $cls = 'other'; $note = ''
                if (Under $n $nWork) { $cls = 'run' }
                elseif ((Under $n $nPkg) -or ($hd.Root -and (Under $n (Norm $hd.Root)))) { $cls = 'package' }
                elseif (Under $n $nReal) { $cls = 'outside' }
                elseif ($n -notmatch '^[a-z]:/' -or ($systemRoots | Where-Object { Under $n $_ })) { $cls = 'system' }
                $fail = $false
                if ($cls -eq 'package' -and $mode -eq 'w') { $fail = $true; $note = 'write into the package' }
                elseif ($cls -eq 'package' -and -not $ok) {
                    $twin = RealTwin $n
                    if (Test-Path -LiteralPath ($raw + '.xpr')) { $note = 'compressed twin shipped (.xpr)' }
                    elseif ($raw -match '\.wav$' -and (Test-Path -LiteralPath ($raw.Substring(0, $raw.Length - 4) + '.fsb'))) { $note = 'FSB bank shipped (.fsb)' }
                    # the HD loader's dev-layout fallback (WfcPipeline: <root>/pack/hd_index.json, opened only when <root> has no
                    # hd_index.json): a package without the HD pack (--no-hd) makes this probe; the dev tree has the file
                    elseif ($n -match '/assettools/out/hd/pack/hd_(pbr_|modern_)?index\.json$') { $note = 'optional probe (the HD loader''s dev-layout fallback; the package ships no HD root index)' }
                    elseif ($twin -and (Test-Path -LiteralPath $twin)) { $fail = $true; $note = 'omission: the full tree has it' }
                    else { $note = 'optional probe (in neither tree)' }
                }
                elseif ($cls -eq 'outside' -and $mode -eq 'r') {
                    $pt = PkgTwin $n
                    $has = $pt -and (Test-Path -LiteralPath $pt)
                    if ($ok -or $has) {
                        $dataRoot = (Under $n "$nReal/extractedassets") -or (Under $n "$nReal/game dump") -or (Under $n "$nReal/assettools")
                        $note = if (-not $has) { 'outside, package LACKS it' } elseif ($dataRoot) { 'outside data root, package has it (override missing)' } else { 'outside, package has it (resolves under Play.bat subst)' }
                        $fail = (-not $has) -or $dataRoot -or $Strict
                    } else { $note = 'outside, failed open (probe)' }
                }
                elseif ($cls -eq 'outside' -and $mode -eq 'w') { $fail = $true; $note = 'write outside the run folder' }
                $rows += [pscustomobject]@{ Mode = $mode; Ok = $ok; Class = $cls; Fail = $fail; Note = $note; Path = $raw }
            }
        }
        $rows | Export-Csv -LiteralPath (Join-Path $run 'opens.csv') -NoTypeInformation -Encoding UTF8

        $reasons = @()
        if (-not (Test-Path -LiteralPath $fileLog)) { $reasons += 'no file log (WFC_FILELOG not active?)' }
        # the playtest's log must be findable (2026-10-13: a playtest left no wfc.log anywhere): wfc.log beside the exe, and
        # with -ForeignCwd nothing written into the working directory instead
        if (-not (Test-Path -LiteralPath $logPath)) { $reasons += 'no wfc.log beside the exe' }
        if ($ForeignCwd) {
            $null = icacls $cwd /remove:d "$($env:USERNAME)"
            foreach ($f in 'wfc.log', 'wfc_profile.ini') { if (Test-Path -LiteralPath (Join-Path $cwd $f)) { $reasons += "$f written to the not-writable working directory" } }
            if (-not ($log | Select-String -SimpleMatch "the working directory is not writable" | Select-Object -First 1)) { $reasons += "no 'log file: ... (the working directory is not writable: the exe's folder)' line (exe before the log-dir patch?)" }
        }
        if ($killed) { $reasons += "killed after $TimeoutSeconds s" }
        if (-not $matchLine -or -not $inGame) { $reasons += 'match not reached' }
        if (-not $shutdown) { $reasons += 'no clean shutdown' }
        if ($errors.Count) { $reasons += "$($errors.Count) [error] log lines" }
        $failRows = @($rows | Where-Object { $_.Fail })
        $warnRows = @($rows | Where-Object { -not $_.Fail -and $_.Class -eq 'outside' -and $_.Note -like '*resolves under*' })
        if ($failRows.Count) { $reasons += "$($failRows.Count) failing opens" }
        # HD off by default (user 2026-10-12): the pipeline reads the index only with HD on, so a first launch never logs the
        # 'available' line - there the evidence is the runtime's detection ('optional runtimes: ... HD texture pack yes');
        # -HdForced runs (HD on) must load the whole index (rows match)
        $hdDetected = [bool]($log | Select-String -SimpleMatch 'HD texture pack yes' | Select-Object -First 1)
        if ($hd.Root -and -not $HdForced -and -not $hdDetected) { $reasons += "HD pack not detected by the game (no 'HD texture pack yes' runtimes line; index rows $($hd.Rows))" }
        if ($hd.Root -and $HdForced -and -not ($hdLine -and $hdLine.Matches[0].Groups[2].Success -and [int]$hdLine.Matches[0].Groups[2].Value -eq $hd.Rows)) {
            $reasons += "HD pack not accepted by the game: $hdLog (index rows $($hd.Rows))"
        }
        if ($hd.Root -and -not $HdForced -and -not $NoHd) {   # the default: the original look - HD textures stay off
            $hdOn = $log | Select-String -SimpleMatch 'renderer: HD textures on' | Select-Object -First 1
            if ($hdOn) { $reasons += 'HD textures ON at a first launch (the default is off: the original look; see settings.autodetect in the log)' }
        }
        if ($hd.Root -and $HdForced -and -not $NoHd) {
            $hdOn = $log | Select-String -SimpleMatch 'renderer: HD textures on' | Select-Object -Last 1
            if (-not $hdOn) { $reasons += "HD textures forced on (WFC_HDTEXTURES=$HdLevel) but the renderer did not turn them on" }
        }
        # HD Enhanced (level 2) with the M6 maps shipped: every row loads ("HD modern material maps: <rows> (hd_pbr_index.json)",
        # no "rows skipped") and the map runs the Modern materials ("Modern materials on for <map>")
        if ($hd.Root -and $HdForced -and $HdLevel -eq 2 -and -not $NoHd) {
            $mm = $log | Select-String -Pattern 'wfc: HD modern material maps: (\d+) \(([^)]+)\)(.*)' | Select-Object -First 1
            if ($hd.PbrRows -gt 0) {
                if (-not $mm) { $reasons += 'HD Enhanced: no "HD modern material maps" line (the M6 index was never read)' }
                elseif ([int]$mm.Matches[0].Groups[1].Value -ne $hd.PbrRows -or $mm.Matches[0].Groups[2].Value -ne 'hd_pbr_index.json' -or $mm.Matches[0].Groups[3].Value -match 'skipped') {
                    $reasons += "HD Enhanced: M6 maps not all loaded: $($mm.Line.Trim()) (index rows $($hd.PbrRows))"
                }
            }
            $modernOn = $log | Select-String -Pattern 'wfc: Modern materials on for MP_' | Select-Object -First 1
            if (-not $modernOn) { $reasons += 'HD Enhanced: no "Modern materials on for MP_<map>" line (the match map ran the original materials)' }
        }
        # Modern materials (HD Enhanced): the loader falls back to the original materials per map when materials_pbr.json is
        # missing / unparsable / empty (Rendering a2067e5d) - a package that ships the file must never log the fallback
        $pbrBad = $log | Select-String -SimpleMatch 'wfc: Modern materials unavailable for' | Select-Object -First 1
        if ($pbrBad) { $reasons += "Modern materials fell back: $($pbrBad.Line.Trim())" }
        # ray tracing stays off unless switched on (user 2026-10-12)
        $rtOn = $log | Select-String -Pattern 'RT round trip: on|RT shadow \+ AO: on|RT characters: on' | Select-Object -First 1
        if ($rtOn) { $reasons += "ray tracing ran without being switched on: $($rtOn.Line.Trim())" }
        # the optional graphics rows follow what the game finds beside the exe (logged once at boot): FSR 3.1 and frame
        # generation exactly when the package ships the M5 runtime, the HD textures row exactly when it ships the HD pack
        $rt = $log | Select-String -Pattern 'graphics: optional runtimes: FSR 3\.1 (yes|no), frame generation (yes|no), HD texture pack (yes|no)' | Select-Object -First 1
        $rtLog = if ($rt) { $rt.Matches[0].Value } else { 'no optional-runtimes line' }
        if ($pk.Manifest) {
            if ($rt) {
                $g = $rt.Matches[0].Groups
                if (($g[1].Value -eq 'yes') -ne $pk.M5) { $reasons += "FSR 3.1 row: the game reports $($g[1].Value), the package $(if ($pk.M5) { 'ships' } else { 'does not ship' }) the runtime" }
                if (($g[2].Value -eq 'yes') -ne $pk.M5) { $reasons += "frame generation row: the game reports $($g[2].Value), the package $(if ($pk.M5) { 'ships' } else { 'does not ship' }) the runtime" }
                if (-not $NoHd -and (($g[3].Value -eq 'yes') -ne [bool]$hd.Root)) { $reasons += "HD textures row: the game reports $($g[3].Value), the package $(if ($hd.Root) { 'ships' } else { 'does not ship' }) the pack" }
            } elseif ($Strict) { $reasons += 'no "graphics: optional runtimes" log line (an exe before that line): rows unchecked' }
        }
        if ($Strict -and $feNote -ne 'inside the package') { $reasons += "frontend data dir $(if ($feDir) { $feDir } else { '-' }): $feNote" }
        if (-not $loc) { $reasons += 'no localized-waves line' }
        else {
            if ($loc.Skipped -gt 0) { $reasons += "$($loc.Skipped) localized waves not played" }
            if ($id -eq '508' -and $loc.Twin -lt $ExpectedLocLines) { $reasons += "$($loc.Twin) localized waves from the twin (expected >= $ExpectedLocLines)" }
        }
        $results += [pscustomobject]@{
            Map = $name; Id = $id; Seconds = $secs; Pass = ($reasons.Count -eq 0); Reasons = $reasons; Loc = $loc
            Opens = $rows.Count; Package = @($rows | Where-Object { $_.Class -eq 'package' }).Count
            Run = @($rows | Where-Object { $_.Class -eq 'run' }).Count; System = @($rows | Where-Object { $_.Class -eq 'system' }).Count
            Outside = @($rows | Where-Object { $_.Class -eq 'outside' }).Count; Other = @($rows | Where-Object { $_.Class -eq 'other' }).Count
            Probes = @($rows | Where-Object { $_.Note -like 'optional probe*' -or $_.Note -like '*(probe)' }).Count
            FailRows = $failRows; WarnRows = $warnRows; Errors = $errors; FrontendData = $(if ($feDir) { "$feDir ($feNote)" } else { $feNote }); HdLog = $hdLog; RuntimeLog = $rtLog
            Twins = @($rows | Where-Object { $_.Note -like '*shipped*' }).Count; OtherRows = @($rows | Where-Object { $_.Class -eq 'other' })
        }
        Write-Host ("[{0}] {1} in {2} s: {3}" -f $name, $(if ($reasons.Count) { 'FAIL' } else { 'PASS' }), $secs, ($reasons -join '; '))
    }
    # ---- the settings self-test (Integration 2026-10-13): WFC_SETTINGSTEST=1 sets every renderer setting the frontend hands
    # over (display gamma, FSR, anisotropy, HD, FSR 3.1, Modern, Ray Tracing, DLSS in DLSS builds) to a non-default value,
    # runs release() (a map change) and logs "settings-test: <n> pass, <m> fail" - every row must survive in the package.
    if (-not $NoSettingsTest -and -not $StaticOnly) {
        WaitHold
        $stRun = Join-Path $WorkDir 'settingstest'
        if (Test-Path -LiteralPath $stRun) { Remove-Item -LiteralPath $stRun -Recurse -Force }
        $stBin = Join-Path $stRun 'bin'
        New-Item -ItemType Directory -Force -Path $stBin | Out-Null
        Copy-Item -Path (Join-Path $pkgBin '*') -Destination $stBin -Recurse -Force -Exclude 'wfc.log', 'wfc_profile.ini', 'wfc_crash_*', 'filelog*.txt'
        if ($ExeBin) { Copy-Item -Path (Join-Path $ExeBin '*.exe'), (Join-Path $ExeBin '*.dll') -Destination $stBin -Force }
        Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
        $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'   # silent, never takes the mouse / focus (user 2026-10-13 / 2026-10-15; env-only)
        foreach ($k in $roots.Keys) { Set-Item -LiteralPath "env:$k" -Value ($roots[$k] -replace '\\', '/') }
        $env:WFC_SETTINGSTEST = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_SKIPINTRO = '1'; $env:WFC_NOMOUSE = '1'; $env:WFC_BOOT = 'frontend'
        $env:WFC_GPULOST_DIALOG_S = '0'; $env:WFC_FRONTEND_SCRIPT = 'wait:frontend;wait:t=1;quit'
        $sp = Start-Process -FilePath (Join-Path $stBin 'wfc_rebuild.exe') -WorkingDirectory $stBin -PassThru
        if (-not $sp.WaitForExit(180000)) { Stop-Process -Id $sp.Id -Force -ErrorAction SilentlyContinue; $setTest.Note = 'timeout (killed)' }
        $stLog = Join-Path $stBin 'wfc.log'
        $sl = if (Test-Path -LiteralPath $stLog) { Get-Content -LiteralPath $stLog } else { @() }
        $sum = $sl | Select-String -Pattern 'settings-test: (\d+) pass, (\d+) fail' | Select-Object -Last 1
        if ($sum) {
            $setTest.Pass = [int]$sum.Matches[0].Groups[1].Value; $setTest.Fail = [int]$sum.Matches[0].Groups[2].Value
            $failed = @($sl | Select-String -SimpleMatch 'settings-test FAIL:' | ForEach-Object { $_.Line.Trim() })
            $countOk = $SettingsExpected -le 0 -or ($setTest.Pass + $setTest.Fail) -eq $SettingsExpected
            $setTest.Status = if ($setTest.Fail -eq 0 -and $countOk) { 'PASS' } else { 'FAIL' }
            $setTest.Note = "$($setTest.Pass) pass, $($setTest.Fail) fail$(if ($SettingsExpected -gt 0) { " (expected $SettingsExpected checks)" })$(if ($failed.Count) { ': ' + ($failed -join '; ') })"
        } elseif ($setTest.Note -eq 'not run') { $setTest.Note = 'no settings-test summary line in the log' }
        Write-Host "[Settings] $($setTest.Status): $($setTest.Note)"
    }
    # ---- the Ray Tracing gate (Integration 2026-10-13; Experimental's rt-build-check.ps1): a direct-boot Streets match with the
    # profile's Ray Tracing row = Realistic must build its RT scene (a TLAS line) and never log "without the RT shaders" - an exe
    # built without third_party/dxc + ffx_denoiser shows the row and does nothing. Run from a copy of the package's bin with the
    # package's data roots (as the maps above). FAIL blocks the package; UNKNOWN (no DXR 1.1 device here) is reported, not a fail.
    if (-not $NoRtCheck -and -not $StaticOnly) {
        $rtScript = if ($RtCheck) { $RtCheck } else { Join-Path $PSScriptRoot '..\fidelity\rt-build-check.ps1' }
        if (-not (Test-Path -LiteralPath $rtScript)) { $rtGate.Status = 'FAIL'; $rtGate.Note = "rt-build-check.ps1 not found at $rtScript (-RtCheck <path>, or -NoRtCheck)" }
        else {
            WaitHold
            if ($RtHoldFile -and (Test-Path -LiteralPath $RtHoldFile)) {
                Write-Host "RT hold file present ($RtHoldFile): the RT gate waits for the RT slot"
                while (Test-Path -LiteralPath $RtHoldFile) { Start-Sleep -Seconds 2 }
            }
            $rtRun = Join-Path $WorkDir 'rt'
            if (Test-Path -LiteralPath $rtRun) { Remove-Item -LiteralPath $rtRun -Recurse -Force }
            $rtBin = Join-Path $rtRun 'bin'
            New-Item -ItemType Directory -Force -Path $rtBin | Out-Null
            Copy-Item -Path (Join-Path $pkgBin '*') -Destination $rtBin -Recurse -Force -Exclude 'wfc.log', 'wfc_profile.ini', 'wfc_crash_*', 'filelog*.txt'
            if ($ExeBin) { Copy-Item -Path (Join-Path $ExeBin '*.exe'), (Join-Path $ExeBin '*.dll') -Destination $rtBin -Force }
            Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
            $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'   # silent, never takes the mouse / focus (user 2026-10-13 / 2026-10-15; env-only)
            foreach ($k in $roots.Keys) { Set-Item -LiteralPath "env:$k" -Value ($roots[$k] -replace '\\', '/') }
            $env:WFC_GPULOST_DIALOG_S = '0'
            $env:WFC_RT_MODE = '2'   # (the start mode of a direct boot: the frontend, which applies the profile row, does not run - Rendering)
            $rtArgs = @{ Exe = (Join-Path $rtBin 'wfc_rebuild.exe'); OutDir = (Join-Path $rtRun 'check') }
            if ($RtBuildDir) { $rtArgs.BuildDir = $RtBuildDir }
            $rtOut = @(& $rtScript @rtArgs 2>&1 | ForEach-Object { "$_" })
            $rtGate.Lines = @($rtOut | Where-Object { $_ -match '^(PASS|FAIL|UNKNOWN|SKIP) rtcheck\.' })
            $rtGate.Status = if ($rtGate.Lines | Where-Object { $_ -like 'FAIL *' }) { 'FAIL' } elseif ($rtGate.Lines | Where-Object { $_ -like 'UNKNOWN *' }) { 'UNKNOWN' } elseif ($rtGate.Lines.Count) { 'PASS' } else { 'FAIL' }
            if (-not $rtGate.Lines.Count) { $rtGate.Note = 'rt-build-check printed no result lines: ' + (($rtOut | Select-Object -Last 3) -join ' / ') }
            # R1 sun swap (Rendering 2026-10-13): when the package ships <render data>/MP_IAC_Streets/vbake (build_slim --vbake),
            # the gate's Realistic run must log "RT R1 sun swap data: ... (k ...)" at load and "RT R1 sun swap: <N> V_bake pages"
            # with N > 0 after the warm-up (N 0 = the PNG names do not match the coefficient-0 pages); never "failed with the
            # sun-swap text". Without vbake in the package: reported, not a fail (Realistic then has no swap).
            $vbFit = Join-Path $roots['WFC_RENDER_DATA'] 'MP_IAC_Streets\vbake\fit.json'
            $rtLogPath = Join-Path $rtRun 'check\rt_on\wfc.log'
            if (Test-Path -LiteralPath $vbFit) {
                $rl = if (Test-Path -LiteralPath $rtLogPath) { Get-Content -LiteralPath $rtLogPath } else { @() }
                $dataLine = $rl | Select-String -Pattern 'RT R1 sun swap data: .*\(k ([0-9.]+)' | Select-Object -First 1
                $pageLine = $rl | Select-String -Pattern 'RT R1 sun swap: (\d+) V_bake pages' | Select-Object -First 1
                $bad = $rl | Select-String -Pattern 'RT R1: program .* failed with the sun-swap text|RT R1 .*unused|RT R1: glDrawBuffers / glColorMaski / glClearBufferfv missing|RT R1: the sun-swap composite failed' | Select-Object -First 1
                $pages = if ($pageLine) { [int]$pageLine.Matches[0].Groups[1].Value } else { 0 }
                # Rendering c232c380+ (GL entry points tested after the GL load) also logs the first Realistic frame's state:
                # it is required: "sun swap ON (V_bake pages N" with N > 0 (a build before c232c380 fails this row)
                $firstLine = $rl | Select-String -Pattern 'RT R1 \(first Realistic frame\): (.*)' | Select-Object -First 1
                $firstOk = $firstLine -and $firstLine.Line -match 'sun swap ON \(V_bake pages ([1-9]\d*)'
                if ($firstLine -and -not $firstOk -and -not $bad) { $bad = $firstLine }
                # R4 (Rendering 30b2fa74+: the runtime RT lightmap lights the whole map in Realistic) replaces the R1 swap:
                # r1Active() is false while it runs, so "sun swap OFF" is expected; the gate then requires R4's own chain -
                # "RT GI: <n> lightmap texels" with n > 0 and "RT GI: sun share k per page: <m> pages from fit.json pages[]"
                $giTex = $rl | Select-String -Pattern 'RT GI: (\d+) lightmap texels' | Select-Object -First 1
                $giK = $rl | Select-String -Pattern 'RT GI: sun share k per page: (\d+) pages from fit\.json' | Select-Object -First 1
                $r4 = [bool]$giTex
                if ($r4) {
                    $texels = [int64]$giTex.Matches[0].Groups[1].Value
                    $kPages = if ($giK) { [int]$giK.Matches[0].Groups[1].Value } else { 0 }
                    $bad = $rl | Select-String -Pattern 'RT R1: program .* failed with the sun-swap text|RT R1: the sun-swap composite failed|RT GI.*(failed|error)' | Select-Object -First 1
                    $ok = $dataLine -and $pages -gt 0 -and $texels -gt 0 -and $kPages -gt 0 -and -not $bad
                    $rtGate.Lines += "$(if ($ok) { 'PASS' } else { 'FAIL' }) rtcheck.r4: V_bake shipped; data line $(if ($dataLine) { 'k ' + $dataLine.Matches[0].Groups[1].Value } else { 'MISSING' }), $pages V_bake pages; R4 runtime lightmap $texels texels, per-page k from fit.json on $kPages pages (R1 swap off by design while R4 runs)$(if ($bad) { '; ' + $bad.Line.Trim() })"
                } else {
                $ok = $dataLine -and $pages -gt 0 -and $firstOk -and -not $bad
                $rtGate.Lines += "$(if ($ok) { 'PASS' } else { 'FAIL' }) rtcheck.r1: V_bake shipped; data line $(if ($dataLine) { 'k ' + $dataLine.Matches[0].Groups[1].Value } else { 'MISSING' }), $pages V_bake pages$(if (-not $firstLine) { '; no RT R1 (first Realistic frame) line' })$(if ($bad) { '; ' + $bad.Line.Trim() })"
                }
                if (-not $ok) { $rtGate.Status = 'FAIL' }
            } else { $rtGate.Lines += 'INFO rtcheck.r1: the package ships no MP_IAC_Streets\vbake (Realistic has no sun swap)' }
            # Package 6 default flip (Systems SYSTEMS_FXLIGHTS_DEFAULT_ON): Realistic turns the RT-era effect lights and the vehicle
            # headlights on by default (WFC_FXLIGHTS=0 / WFC_HEADLIGHTS=0 turn them off; Off / Faithful never have them). The gate's
            # Realistic run (no WFC_FXLIGHTS / WFC_HEADLIGHTS set) must log both on at its first Realistic frame.
            $rl2 = if (Test-Path -LiteralPath $rtLogPath) { Get-Content -LiteralPath $rtLogPath } else { @() }
            $rtl = $rl2 | Select-String -Pattern 'RT-era lights \(first Realistic frame\): effect lights (on|off), headlights (on|off)' | Select-Object -First 1
            $rtlOk = $rtl -and $rtl.Matches[0].Groups[1].Value -eq 'on' -and $rtl.Matches[0].Groups[2].Value -eq 'on'
            $rtGate.Lines += "$(if ($rtlOk) { 'PASS' } else { 'FAIL' }) rtcheck.rtlights: $(if ($rtl) { $rtl.Line.Trim() } else { 'no RT-era lights line in the Realistic run (an exe before the package 6 default flip)' })"
            if (-not $rtlOk) { $rtGate.Status = 'FAIL' }
            # Package 6 Realistic defaults ($RealisticDefaults above)
            $rdl = $rl2 | Select-String -Pattern 'RT Realistic defaults \(first Realistic frame\): (.*)$' | Select-Object -First 1
            if (-not $rdl) { $rtGate.Lines += "FAIL rtcheck.defaults: no 'RT Realistic defaults' line in the Realistic run (an exe before Rendering's defaults line)"; $rtGate.Status = 'FAIL' }
            else {
                $got = @{}
                foreach ($kv in ($rdl.Matches[0].Groups[1].Value.Trim() -split '\s+')) { $i = $kv.IndexOf('='); if ($i -gt 0) { $got[$kv.Substring(0, $i)] = $kv.Substring($i + 1) } }
                $bad = @(); $info = @()
                foreach ($k in $RealisticDefaults.Keys) {
                    $want = $RealisticDefaults[$k]
                    if (-not $got.ContainsKey($k)) { $bad += "$k missing"; continue }
                    $v = $got[$k]; $ok = $false
                    if ($want -is [string]) { $ok = $v -eq $want }
                    else { $d = 0.0; $ok = [double]::TryParse($v, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$d) -and [math]::Abs($d - [double]$want) -le 1e-3 }
                    if (-not $ok) { $bad += "$k=$v (want $want)" }
                }
                foreach ($k in $got.Keys) { if (-not $RealisticDefaults.Contains($k)) { $info += "$k=$($got[$k])" } }
                $rtGate.Lines += "$(if ($bad.Count) { 'FAIL' } else { 'PASS' }) rtcheck.defaults: $($RealisticDefaults.Count - $bad.Count) of $($RealisticDefaults.Count) as expected$(if ($bad.Count) { ' - ' + ($bad -join ', ') })$(if ($info.Count) { '; not in the table: ' + ($info -join ' ') })"
                if ($bad.Count) { $rtGate.Status = 'FAIL' }
            }
            Write-Host "[Ray Tracing] $($rtGate.Status): $(($rtGate.Lines + $rtGate.Note | Where-Object { $_ }) -join '; ')"
        }
    }
} finally {
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    foreach ($k in $saved.Keys) { Set-Item -LiteralPath "env:$k" -Value $saved[$k] }
    RemoveSubst
}

# ---- report ----
$overall = -not ($results | Where-Object { -not $_.Pass }) -and -not $hd.Problems.Count -and -not $pk.Problems.Count -and $rtGate.Status -ne 'FAIL' -and $setTest.Status -ne 'FAIL'
$md = @()
$md += "# Slim package isolation check: $(if ($overall) { 'PASS' } else { 'FAIL' })"
$md += ''
$md += "Package: $(if ($SubstDrive) { "$Package (run as $SubstDrive)" } else { $pkgTop })$(if ($ExeBin) { "; exe from $ExeBin" }) (exe sha256 $exeHash...). Run folder: $WorkDir. $(Get-Date -Format 'yyyy-MM-dd HH:mm')."
$md += "Maps: $Maps; match $MatchSeconds s; intro $(if ($SkipIntro) { 'skipped' } else { 'played' }); lobby options: $(if ($LobbyOptions) { $LobbyOptions } else { 'default' })."
$md += ''
if ($OmitRoot.Count) { $md += "SELF-TEST: data roots left unset: $($OmitRoot -join ', ')." }
$md += "Strict: $(if ($Strict) { 'on (every outside read fails)' } else { 'off (outside reads the package has, outside the data roots, are warnings)' })."
$md += ''
$md += '| Map | Result | Time | Opens | Package | via .xpr / .fsb | Run | System | Outside | Other | Probes | Localized (twin / merged / not played) |'
$md += '|---|---|---|---|---|---|---|---|---|---|---|---|'
foreach ($r in $results) {
    $l = if ($r.Loc) { "$($r.Loc.Twin) / $($r.Loc.Merged) / $($r.Loc.Skipped) ($($r.Loc.Lang))" } else { '-' }
    $md += "| $($r.Map) ($($r.Id)) | $(if ($r.Pass) { 'PASS' } else { 'FAIL' }) | $($r.Seconds) s | $($r.Opens) | $($r.Package) | $($r.Twins) | $($r.Run) | $($r.System) | $($r.Outside) | $($r.Other) | $($r.Probes) | $l |"
}
$md += ''
foreach ($r in $results) { $md += "- $($r.Map): frontend data dir: $($r.FrontendData)$(if ($hd.Root) { "; HD: $($r.HdLog)" })" }
$md += ''
$md += "HD pack: $($hd.Where)$(if ($hd.Root) { " ($($hd.Root)): $($hd.Rows) index rows, $($hd.Files) files, $([math]::Round($hd.Bytes / 1GB, 2)) GB; $(if ($hd.Problems.Count) { 'FAIL' } else { 'every row present with its byte count, every DDS indexed' })" })$(if ($NoHd) { ' (-NoHd: not checked)' })"
foreach ($x in $hd.Problems) { $md += "- $x" }
$md += ''
if ($pk.Manifest) {
    $md += "Package bin vs build_slim.json: $($pk.Files) files, M5 runtime $(if ($pk.M5) { 'shipped' } else { 'not shipped' }) (head $($pk.Manifest.head)$(if ($pk.Manifest.hd_label) { ", HD pack $($pk.Manifest.hd_label)" })); $(if ($pk.Problems.Count) { 'FAIL' } else { 'exact match' })."
    foreach ($x in $pk.Problems) { $md += "- $x" }
    foreach ($r in $results) { $md += "- $($r.Map): $($r.RuntimeLog)" }
} else { $md += "Package bin: $($pk.Note)." }
foreach ($r in $results) {
    if ($r.Pass -and -not $r.OtherRows.Count -and -not $r.WarnRows.Count) { continue }
    $md += ''
    $md += "## $($r.Map)"
    foreach ($x in $r.Reasons) { $md += "- $x" }
    foreach ($f in ($r.FailRows | Select-Object -First 50)) { $md += "  - $($f.Mode) $(if ($f.Ok) { 'ok' } else { 'missing' }) ``$($f.Path)``: $($f.Note)" }
    if ($r.FailRows.Count -gt 50) { $md += "  - ... $($r.FailRows.Count - 50) more in $($r.Map)\opens.csv" }
    foreach ($e in ($r.Errors | Select-Object -First 20)) { $md += "  - log: ``$($e.Trim())``" }
    foreach ($w in $r.WarnRows) { $md += "- warning: ``$($w.Path)``: $($w.Note)" }
    if ($r.OtherRows.Count) { $md += "- other paths (not failing): " + (($r.OtherRows | Select-Object -First 10 | ForEach-Object { "``$($_.Path)``" }) -join ', ') }
}
$md += ''
$md += "Settings self-test (WFC_SETTINGSTEST: every renderer setting survives a map change): $($setTest.Status) - $($setTest.Note)"
$md += ''
$md += "Ray Tracing gate (rt-build-check.ps1, profile RayTracing=2, direct-boot Streets): $($rtGate.Status)$(if ($rtGate.Note) { " - $($rtGate.Note)" })"
foreach ($x in $rtGate.Lines) { $md += "- $x" }
$md += ''
$md += 'Per-map detail: <map>\opens.csv (every open, classified), <map>\bin\wfc.log, <map>\filelog.txt.'
# ---- the HD runtime row (Integration 2026-10-16): detection ('HD texture pack yes') passes even when the loader then rejects
# the pack (package 4 / 5: one 89 MB row's byte count failed a float compare, so HD never loaded and no row noticed). A default
# run therefore re-runs the first map with HD forced on (this script, -HdForced, the same package / subst letter after the
# parent released it, no RT gate / settings test): it must log "HD texture pack available: <all rows> textures".
if ($hd.Root -and -not $HdForced -and -not $StaticOnly -and -not $NoHd -and -not $NoHdRuntime) {
    $hdArgs = @{ Package = $Package; WorkDir = (Join-Path $WorkDir 'hd_runtime'); Maps = ($Maps.Split(',')[0]); MatchSeconds = $MatchSeconds;
                 HdForced = $true; NoRtCheck = $true; NoSettingsTest = $true; NoHdRuntime = $true; TimeoutSeconds = $TimeoutSeconds
                 HdLevel = $(if ($hd.PbrRows -gt 0) { 2 } else { 1 }) }   # Enhanced when the M6 maps ship: D pack + M6 maps + Modern materials
    if ($SkipIntro) { $hdArgs.SkipIntro = $true }
    if ($SubstDrive) { $hdArgs.SubstDrive = $SubstDrive }
    if ($HoldFile) { $hdArgs.HoldFile = $HoldFile }
    if ($ExeBin) { $hdArgs.ExeBin = $ExeBin }
    Write-Host "HD runtime row: re-running $($hdArgs.Maps) with HD forced on (level $($hdArgs.HdLevel))"
    $hdOut = @(& $PSCommandPath @hdArgs 2>&1 | ForEach-Object { "$_" })
    $hdOk = $LASTEXITCODE -eq 0
    $hdRep = Join-Path $hdArgs.WorkDir 'REPORT.md'
    $hdLine = if (Test-Path -LiteralPath $hdRep) { (Select-String -LiteralPath $hdRep -Pattern 'HD: ' | Select-Object -First 1).Line } else { 'no report' }
    $md += ''
    $md += "HD runtime row (HD forced on at level $($hdArgs.HdLevel), $($hdArgs.Maps)): $(if ($hdOk) { 'PASS' } else { 'FAIL' }) - $hdLine (report $hdRep)"
    if (-not $hdOk) { $overall = $false }
    Write-Host "HD runtime row: $(if ($hdOk) { 'PASS' } else { 'FAIL' })"
}
$reportPath = Join-Path $WorkDir 'REPORT.md'
$md | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "$(if ($overall) { 'PASS' } else { 'FAIL' }): report $reportPath"
if (-not $overall) { exit 1 }
