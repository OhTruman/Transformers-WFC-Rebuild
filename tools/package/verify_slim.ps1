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
    [switch]$StaticOnly,         # file checks only (HD pack), no game runs
    [string]$ExeBin = "",           # candidate exe tree: its *.exe / *.dll replace the package's in the run copy (the package stays as is)
    [string]$SubstDrive = ""     # e.g. X: map the package to this free drive letter for the run (as Play.bat maps F:), so the game
                                 # sees the laptop's path lengths: a package deep in a work folder pushes long HD paths past
                                 # MAX_PATH (260) that are 213 chars under Play.bat's F:. Removed at the end.
)
$ErrorActionPreference = 'Stop'

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
$hd = [pscustomobject]@{ Root = $null; Where = 'not shipped'; Rows = 0; Files = 0; Bytes = 0L; Problems = @() }
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
            $files = @(Get-ChildItem -LiteralPath $hd.Root -Recurse -File)
            $hd.Files = $files.Count; $hd.Bytes = ($files | Measure-Object -Property Length -Sum).Sum
            $extra = @($files | Where-Object { $_.Name -ne 'hd_index.json' -and $_.Extension -ne '.txt' -and -not $indexed.Contains((Norm $_.FullName)) })
            if ($extra.Count) {
                $hd.Problems += "$($extra.Count) files in the HD folder not in the index (non-PASS textures must not ship)"
                foreach ($x in ($extra | Select-Object -First 10)) { $hd.Problems += "unindexed: $($x.FullName.Substring($hd.Root.Length + 1))" }
            }
        }
    }
}
Write-Host ("HD pack: {0}{1}" -f $hd.Where, $(if ($hd.Root) { ": $($hd.Rows) rows, $($hd.Files) files, $([math]::Round($hd.Bytes / 1GB, 2)) GB, $($hd.Problems.Count) problems" } else { '' }))

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
    Write-Host ("package bin: {0} files vs build_slim.json {1}; M5 runtime {2}; {3} problems" -f $have.Count, $want.Count, $(if ($pk.M5) { 'shipped' } else { 'not shipped' }), $pk.Problems.Count)
} else {
    $pk.Note = 'no build_slim.json (a package made before tools/package/build_slim.py wrote one): bin contents and the M5 runtime not checked'
    Write-Host "package bin: $($pk.Note)"
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
$saved = @{}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
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
        if ($hd.Root) {
            $env:WFC_HDTEXTURES = '1'
            if ((Norm $hd.Root) -ne (Norm (Join-Path $pkg 'AssetTools\out\hd'))) { $env:WFC_HD_ROOT = $hd.Root -replace '\\', '/' }
        }
        $env:WFC_FRONTEND_SCRIPT = "wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;call:Online.SetSelectedMapID,$id;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=$MatchSeconds;quit"

        Write-Host "[$name] map ${id}: starting $exe"
        $t0 = Get-Date
        $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru
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
        if ($killed) { $reasons += "killed after $TimeoutSeconds s" }
        if (-not $matchLine -or -not $inGame) { $reasons += 'match not reached' }
        if (-not $shutdown) { $reasons += 'no clean shutdown' }
        if ($errors.Count) { $reasons += "$($errors.Count) [error] log lines" }
        $failRows = @($rows | Where-Object { $_.Fail })
        $warnRows = @($rows | Where-Object { -not $_.Fail -and $_.Class -eq 'outside' -and $_.Note -like '*resolves under*' })
        if ($failRows.Count) { $reasons += "$($failRows.Count) failing opens" }
        if ($hd.Root -and -not ($hdLine -and $hdLine.Matches[0].Groups[2].Success -and [int]$hdLine.Matches[0].Groups[2].Value -eq $hd.Rows)) {
            $reasons += "HD pack not accepted by the game: $hdLog (index rows $($hd.Rows))"
        }
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
} finally {
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    foreach ($k in $saved.Keys) { Set-Item -LiteralPath "env:$k" -Value $saved[$k] }
    RemoveSubst
}

# ---- report ----
$overall = -not ($results | Where-Object { -not $_.Pass }) -and -not $hd.Problems.Count -and -not $pk.Problems.Count
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
$md += 'Per-map detail: <map>\opens.csv (every open, classified), <map>\bin\wfc.log, <map>\filelog.txt.'
$reportPath = Join-Path $WorkDir 'REPORT.md'
$md | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "$(if ($overall) { 'PASS' } else { 'FAIL' }): report $reportPath"
if (-not $overall) { exit 1 }
