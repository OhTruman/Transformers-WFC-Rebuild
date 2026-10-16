# Shared helpers for the fidelity scripts (dot-source: . "$PSScriptRoot\lib\Run.ps1").

# Graphics pins for every harness launch (returns a copy of $Env; a key the caller sets wins, an empty value = leave the build /
# profile default). Unknown to older builds: no effect.
# - WFC_AUTODETECT=0 (09c d8b7a8e+): a profile without [PCSettings] and no frontend script auto-detects graphics (fullscreen native,
#   16x AF ...), and from 9116e4ff a run without it also lets the HD default rewrite the profile's HDTextures and save it.
# - WFC_AUTODETECT_WINDOWED=1: belt and braces - if a caller does ask for auto-detect, it stays windowed (Integration 2026-10-12: the
#   display is degraded, no fullscreen of any kind).
# - WFC_HDTEXTURES=0 (Integration 2026-10-12): the env override wins over the profile at boot and on apply, so neither the HD default
#   nor a found pack can turn HD on in a timed row. HD-on rows set it explicitly and must pin WFC_HD_ROOT (Assert-WfcHdRoot).
# (user 2026-10-13) silent by default for EVERY process a harness starts, including those launched without Add-WfcHarnessEnv
# (unit-test exes, bash launchers started from a harness): WFC_MUTE=1 in this process's environment unless the caller set it
if (-not $env:WFC_MUTE) { $env:WFC_MUTE = "1" }
# (user 2026-10-15) lane game windows never take the mouse / focus: WFC_NOINTERACT=1 (Gameplay, integration 4da148bb; keeps
# fullscreen requests windowed - a fullscreen row passes WFC_NOINTERACT = "" in its env, which removes it for that launch)
if (-not $env:WFC_NOINTERACT) { $env:WFC_NOINTERACT = "1" }
function Add-WfcHarnessEnv([hashtable]$Env) {
    $e = if ($Env) { $Env.Clone() } else { @{} }
    if (-not $e.ContainsKey("WFC_AUTODETECT")) { $e.WFC_AUTODETECT = "0" }
    if (-not $e.ContainsKey("WFC_AUTODETECT_WINDOWED")) { $e.WFC_AUTODETECT_WINDOWED = "1" }
    if (-not $e.ContainsKey("WFC_HDTEXTURES")) { $e.WFC_HDTEXTURES = "0" }
    # integration 45a8b9c0+: a GPU driver reset ends the process with a message box; a harness run must never wait on it
    if (-not $e.ContainsKey("WFC_GPULOST_DIALOG_S")) { $e.WFC_GPULOST_DIALOG_S = "0" }
    # (Integration 2026-10-13: the watchdog backstop is off by default in the product; harness runs opt in, so a TDR that leaves the
    #  render thread blocked in SwapBuffers ends the run with exit 3 + wfc_gpu_lost_<pid>.txt instead of hanging the harness)
    if (-not $e.ContainsKey("WFC_HANG_EXIT_S")) { $e.WFC_HANG_EXIT_S = "60" }
    # (user 2026-10-13: every game exe a lane launches must be silent - integration WFC_MUTE=1: zero device output, the mix and cue
    #  logic unchanged, so audio harness rows still measure the same; builds before it ignore the variable)
    if (-not $e.ContainsKey("WFC_MUTE")) { $e.WFC_MUTE = "1" }
    # (user 2026-10-15: lane game windows must not take the mouse / focus - interim until WFC_NOINTERACT lands: no mouse look in the
    #  game; a harness that drives the mouse sets WFC_NOMOUSE itself)
    if (-not $e.ContainsKey("WFC_NOMOUSE")) { $e.WFC_NOMOUSE = "1" }
    if (-not $e.ContainsKey("WFC_NOINTERACT")) { $e.WFC_NOINTERACT = "1" }
    Assert-WfcHdRoot $e
    return $e
}

# An HD-on launch must load a FROZEN pack: the default root (<AssetTools>\out\hd, \pack) is rewritten per batch by the HD pass, so
# two arms could load different texture sets, and a half-written index or file drops the whole pack to the originals (the loader
# verifies all or nothing). Throws unless WFC_HD_ROOT (the arm's, else the process's) names a copy outside AssetTools\out\hd that
# holds an hd_index.json.
function Assert-WfcHdRoot([hashtable]$Env) {
    $hd = if ($Env.ContainsKey("WFC_HDTEXTURES")) { "$($Env.WFC_HDTEXTURES)" } else { "$env:WFC_HDTEXTURES" }
    if (-not $hd -or [int]($hd -replace '[^\d-]', '0') -eq 0) { return }
    $r = if ($Env.ContainsKey("WFC_HD_ROOT")) { "$($Env.WFC_HD_ROOT)" } else { "$env:WFC_HD_ROOT" }
    if (-not $r) { throw "WFC_HDTEXTURES=$hd without a pinned WFC_HD_ROOT: HD-on runs need a frozen copy of the pack (not the live AssetTools\out\hd)" }
    if (-not (Test-Path (Join-Path $r "hd_index.json")) -and -not (Test-Path (Join-Path $r "pack\hd_index.json"))) { throw "WFC_HD_ROOT $r has no hd_index.json (or pack\hd_index.json)" }
    $full = (Resolve-Path $r).Path.TrimEnd('\')
    if ($full -match '\\AssetTools\\out\\hd(\\pack)?$') { throw "WFC_HD_ROOT $full is the live AssetTools pack (rewritten per batch): copy it to a frozen folder first" }
}

# The run's HD state from its wfc.log: on (the renderer's "renderer: HD textures on"), pack = the textures the verified pack holds
# ("wfc: HD texture pack available: N textures"; -1 = no such line, i.e. HD off or the pack was ignored). HD-on A/B arms must agree.
function Get-WfcHdState([string]$WfcLog) {
    $o = [pscustomobject]@{ on = $false; pack = -1; root = "" }
    if (-not (Test-Path $WfcLog)) { return $o }
    foreach ($l in [IO.File]::ReadLines((Resolve-Path $WfcLog).Path)) {
        if ($l.Contains('renderer: HD textures ')) { $o.on = $l -match 'renderer: HD textures on' }
        elseif ($l.Contains('HD texture pack available:')) { $m = [regex]::Match($l, 'HD texture pack available: (\d+) textures \((.*)\)'); if ($m.Success) { $o.pack = [int]$m.Groups[1].Value; $o.root = $m.Groups[2].Value } }
    }
    return $o
}

# Ray Tracing state from a wfc.log (RT stage 1, integration 5d1aecd0+): mode = the last "renderer: Ray Tracing <mode>" line (the
# renderer logs only CHANGES, so no line = the mode the map loaded with: Off unless the profile / WFC_RT_MODE set it); scene = an
# RT scene was built ("... TLAS <n> instances"); noShaders = the build has no RT shaders (a no-op RT row).
function Get-WfcRtState([string]$WfcLog) {
    $o = [pscustomobject]@{ mode = 0; modeLogged = $false; scene = $false; noShaders = $false }
    if (-not (Test-Path $WfcLog)) { return $o }
    foreach ($l in [IO.File]::ReadLines((Resolve-Path $WfcLog).Path)) {
        if ($l.Contains('renderer: Ray Tracing ')) { $m = [regex]::Match($l, 'renderer: Ray Tracing (Off|Faithful|Realistic)'); if ($m.Success) { $o.mode = @{ Off = 0; Faithful = 1; Realistic = 2 }[$m.Groups[1].Value]; $o.modeLogged = $true } }
        # (the static world's BLAS / prepass lines, not only the masked world's TLAS line: a map without masked subs logs no TLAS line - Systems)
        elseif ($l -match 'TLAS \d+ instances|RT bridge: static set \(mask|RT world: \d+ prepassed subs') { $o.scene = $true }
        elseif ($l.Contains('built without the RT shaders') -or $l.Contains('this build has no RT shaders')) { $o.noShaders = $true }
        elseif ($l.Contains('reflection hit data')) { $o | Add-Member -NotePropertyName refl -NotePropertyValue $true -Force }
    }
    # a mode set at init (WFC_RT_MODE / WFC_RT_SHADOWAO) is not logged as a change: infer it from the scene (Realistic builds the
    # reflection hit data)
    if (-not $o.modeLogged -and $o.scene) { $o.mode = if ($o.PSObject.Properties['refl']) { 2 } else { 1 } }
    return $o
}

# Run an exe with temporary process environment variables; returns the exit code (-999 = killed
# after -TimeoutSec).
# A python process that can load this machine's lanes' work: one of the project's (its exe or command line under
# F:\Transformers Rebuild, e.g. AssetTools' runners) and not this worktree's own. 2026-10-14: the watchdog flagged the user's
# unrelated Codex / APEX python (C:\Users\...\codex-runtimes, ~1 % of one CPU) on every regress row; those are not a lane's work.
function Test-WfcLanePython($proc) {
    $t = "$($proc.ExecutablePath) $($proc.CommandLine)"
    return ($t -like '*Transformers Rebuild*') -and ("$($proc.CommandLine)" -notlike '*Rebuild-Experimental*')
}
# Whether a CONTAMINATED.txt names a lane's process (another game exe, or a lane python per Test-WfcLanePython); files written
# before 2026-10-14 also list unrelated pythons, so the report re-reads them line by line.
# The game window's client rect (Integration 2026-10-16: under NOINTERACT a 2560x1440 request came out 2560x1417 on some runs,
# which breaks pixel diffs and frame-time comparisons). Heads from dd4bbca1 log "NOINTERACT: client WxH at (x,y)" at boot and after
# each setDisplayMode, with " - DIFFERS from the request" when the client is not the requested size: that row is INVALID. Older
# heads log no such line (unknown, not invalid).
function Get-WfcClientRect([string]$WfcLog) {
    $o = [pscustomobject]@{ known = $false; w = 0; h = 0; differs = $false; line = "" }
    if (-not (Test-Path $WfcLog)) { return $o }
    foreach ($l in [IO.File]::ReadLines((Resolve-Path $WfcLog).Path)) {
        $m = [regex]::Match($l, 'NOINTERACT: client (\d+)x(\d+) at \((-?\d+),(-?\d+)\)( - DIFFERS from the request)?')
        if ($m.Success) { $o.known = $true; $o.w = [int]$m.Groups[1].Value; $o.h = [int]$m.Groups[2].Value; if ($m.Groups[5].Success) { $o.differs = $true }; $o.line = $m.Value }
    }
    return $o
}
function Test-WfcContaminated([string]$Path) {
    if (-not (Test-Path $Path)) { return $false }
    foreach ($l in (Get-Content $Path)) {
        if ($l -match 'wfc_rebuild[^ ]*\.exe' -or $l -match 'Transformers Rebuild') { if ($l -notmatch 'python\.exe' -or $l -notmatch 'Rebuild-Experimental') { return $true } }
    }
    return $false
}

function Invoke-WfcExe([string]$Exe, [string]$Dir, [hashtable]$Env, [string]$Log = "run.log", [int]$TimeoutSec = 0) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $Env = Add-WfcHarnessEnv $Env   # graphics pins: auto-detect off (+ windowed), HD off unless the caller sets it (frozen root)
    # 09c M2 close (2026-10-11): the default bot AI flips Classic -> Smart. Harness runs pin Classic so rows stay comparable with
    # the baselines from before the flip. WFC_BOTAI wins over ?BotAI / ?BotAIAutobot / ?BotAIDecepticon and the profile's BotAI, on
    # every launch path (direct URL and frontend lobby); builds before Smart ignore it (Classic only). A caller sets WFC_BOTAI=Smart
    # (lockstep arm extra / capacity-stress -ExtraEnv) for Smart rows, or WFC_BOTAI= (empty) to leave the build's own default.
    if (-not $Env.ContainsKey("WFC_BOTAI")) { $Env = $Env.Clone(); $Env.WFC_BOTAI = "Classic" }
    $saved = @{}
    foreach ($k in $Env.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process"); [Environment]::SetEnvironmentVariable($k, [string]$Env[$k], "Process") }
    try {
        $p = Start-Process -FilePath $Exe -WorkingDirectory $Dir -NoNewWindow -PassThru `
             -RedirectStandardOutput (Join-Path $Dir $Log) -RedirectStandardError (Join-Path $Dir "$Log.err")
        $null = $p.Handle   # PS 5.1: ExitCode is only kept when the handle was opened before exit
        # contamination watchdog (2026-10-10: a foreign exe started between another lane's reps, after the start gate had passed):
        # while the row runs, poll every second for any OTHER game exe (any wfc_rebuild*.exe not this process) or a python
        # process outside this worktree; the first sighting is written to <Dir>\CONTAMINATED.txt - harnesses report such a row
        # as UNKNOWN (rerun). WFC_NO_WATCHDOG=1 disables it (e.g. for runs that are themselves shared by design).
        $watch = -not $env:WFC_NO_WATCHDOG; $mark = Join-Path $Dir "CONTAMINATED.txt"; $t0 = Get-Date
        while (-not $p.HasExited) {
            if ($TimeoutSec -gt 0 -and ((Get-Date) - $t0).TotalSeconds -gt $TimeoutSec) { $p.Kill(); $p.WaitForExit(); return -999 }
            if ($watch -and -not (Test-Path $mark)) {
                $other = @(Get-CimInstance Win32_Process -Filter "Name LIKE 'wfc_rebuild%.exe' OR Name = 'python.exe'" -ErrorAction SilentlyContinue | Where-Object {
                    $_.ProcessId -ne $p.Id -and ($_.Name -ne 'python.exe' -or (Test-WfcLanePython $_)) })
                # (2026-10-11: a python.exe nobody claimed - the line also names its command line and parent, so the owner can be found)
                if ($other.Count) { ($other | ForEach-Object {
                    $pp = Get-CimInstance Win32_Process -Filter "ProcessId = $($_.ParentProcessId)" -ErrorAction SilentlyContinue
                    $cl = "$($_.CommandLine)"; if ($cl.Length -gt 400) { $cl = $cl.Substring(0, 400) + "..." }
                    "{0:HH:mm:ss} foreign process {1} {2} (started {3:HH:mm:ss}): {4} | cmd: {5} | parent {6} {7}: {8}" -f (Get-Date), $_.ProcessId, $_.Name, $_.CreationDate, "$($_.ExecutablePath)", $cl,
                        $_.ParentProcessId, $(if ($pp) { $pp.Name } else { "(exited)" }), $(if ($pp) { $c2 = "$($pp.CommandLine)"; if ($c2.Length -gt 300) { $c2.Substring(0, 300) + "..." } else { $c2 } } else { "" }) }) | Set-Content -Encoding UTF8 $mark }
            }
            Start-Sleep -Milliseconds 1000
        }
        $p.WaitForExit()
        return $p.ExitCode
    } finally {
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], "Process") }
    }
}

# Parse the exe's per-frame log lines ("[info ] frame N pos x y z key=value ...") from wfc.log.
function Read-WfcFrames([string]$WfcLog) {
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($ln in [IO.File]::ReadLines($WfcLog)) {
        $i = $ln.IndexOf("] frame ")
        if ($i -lt 0) { continue }
        $q = $ln.Substring($i + 8).Split(" ")
        $f = [ordered]@{ frame = [int]$q[0]; x = [double]$q[2]; y = [double]$q[3]; z = [double]$q[4] }
        for ($k = 5; $k -lt $q.Length; $k++) {
            $kv = $q[$k].Split("=", 2)
            if ($kv.Length -eq 2) { $v = 0.0; if ([double]::TryParse($kv[1], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v)) { $f[$kv[0]] = $v } else { $f[$kv[0]] = $kv[1] } }
        }
        $out.Add([pscustomobject]$f)
    }
    return $out.ToArray()
}

function Get-WfcRoot { (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path }

# Write rows (objects) as CSV with invariant formatting.
function Write-WfcCsv($Rows, [string]$Path) {
    $old = [Threading.Thread]::CurrentThread.CurrentCulture
    [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture
    try { $Rows | Export-Csv -NoTypeInformation -Path $Path } finally { [Threading.Thread]::CurrentThread.CurrentCulture = $old }
}

# Contact sheet from tiles @{ png = path; label = text } (System.Drawing).
function New-WfcSheet($Tiles, [string]$Path, [int]$Cols = 4, [int]$W = 480, [int]$H = 270) {
    Add-Type -AssemblyName System.Drawing
    $lh = 18; $rows = [Math]::Ceiling($Tiles.Count / $Cols)
    $bmp = New-Object System.Drawing.Bitmap ($Cols * $W), ([Math]::Max(1, $rows) * ($H + $lh))
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::Black)
    $font = New-Object System.Drawing.Font "Consolas", 8
    for ($i = 0; $i -lt $Tiles.Count; $i++) {
        $x = ($i % $Cols) * $W; $y = [Math]::Floor($i / $Cols) * ($H + $lh)
        if ($Tiles[$i].png -and (Test-Path $Tiles[$i].png)) { $im = [System.Drawing.Image]::FromFile($Tiles[$i].png); $g.DrawImage($im, $x, $y + $lh, $W, $H); $im.Dispose() }
        $brush = if ($Tiles[$i].flag) { [System.Drawing.Brushes]::OrangeRed } else { [System.Drawing.Brushes]::White }
        $g.DrawString($Tiles[$i].label, $font, $brush, $x + 2, $y + 2)
    }
    $g.Dispose(); $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

# BMP -> PNG (deletes the BMP).
function Convert-WfcBmp([string]$Bmp) {
    Add-Type -AssemblyName System.Drawing
    $png = [IO.Path]::ChangeExtension($Bmp, ".png")
    $img = [System.Drawing.Image]::FromFile($Bmp); try { $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $img.Dispose() }
    Remove-Item -LiteralPath $Bmp
    return $png
}

# Result collector in the wfc_fidelity report schema ({summary, results[{id,status,measured,expected,unit,owner,note}]}).
function New-WfcResults { return ,(New-Object System.Collections.Generic.List[object]) }   # comma: do not unroll
function Add-WfcResult($List, [string]$Id, [string]$Status, $Measured = $null, [string]$Note = "", [string]$Owner = "", $Expected = $null, [string]$Unit = "") {
    $o = [ordered]@{ id = $Id; status = $Status }
    if ($null -ne $Measured) { $o.measured = [double]$Measured }
    if ($null -ne $Expected) { $o.expected = [double]$Expected }
    if ($Unit) { $o.unit = $Unit }
    if ($Owner) { $o.owner = $Owner }
    $o.note = $Note
    $List.Add([pscustomobject]$o)
}
function Write-WfcReport($List, [string]$Path) {
    $sum = [ordered]@{}
    # HUMAN = HUMAN CHECK REQUIRED: a measured anomaly candidate whose correctness only a person can judge.
    # UNKNOWN = original behaviour not established (evidence pending); WAITING = hook / presentation not landed yet; PARTIAL = partly present
    foreach ($s in "PASS", "FAIL", "KNOWN", "INFO", "SKIP", "HUMAN", "UNKNOWN", "WAITING", "PARTIAL") { $sum[$s.ToLower()] = @($List.ToArray() | Where-Object status -eq $s).Count }
    [ordered]@{ summary = $sum; results = $List.ToArray() } | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $Path
    return $sum
}

# Locate a toolchain binary: <tree>\.toolchain\llvm-mingw-*\bin first, then the same under each parent
# directory (an ab.ps1 export lives inside a worktree that has the toolchain), then PATH.
function Find-WfcTool([string]$Name, [string]$From = "") {
    if (-not $From) { $From = Get-WfcRoot }
    $d = Get-Item $From
    while ($d) {
        $hit = Get-ChildItem -Path (Join-Path $d.FullName ".toolchain\llvm-mingw-*\bin\$Name.exe") -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
        $d = $d.Parent
    }
    $c = Get-Command $Name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    throw "tool not found: $Name"
}
