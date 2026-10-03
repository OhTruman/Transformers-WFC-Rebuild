# Shared helpers for the fidelity scripts (dot-source: . "$PSScriptRoot\lib\Run.ps1").

# Run an exe with temporary process environment variables; returns the exit code (-999 = killed
# after -TimeoutSec).
function Invoke-WfcExe([string]$Exe, [string]$Dir, [hashtable]$Env, [string]$Log = "run.log", [int]$TimeoutSec = 0) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $saved = @{}
    foreach ($k in $Env.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process"); [Environment]::SetEnvironmentVariable($k, [string]$Env[$k], "Process") }
    try {
        $p = Start-Process -FilePath $Exe -WorkingDirectory $Dir -NoNewWindow -PassThru `
             -RedirectStandardOutput (Join-Path $Dir $Log) -RedirectStandardError (Join-Path $Dir "$Log.err")
        $null = $p.Handle   # PS 5.1: ExitCode is only kept when the handle was opened before exit
        if ($TimeoutSec -gt 0) {
            if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); $p.WaitForExit(); return -999 }
        } else { $p.WaitForExit() }
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
    foreach ($s in "PASS", "FAIL", "KNOWN", "INFO", "SKIP", "HUMAN") { $sum[$s.ToLower()] = @($List.ToArray() | Where-Object status -eq $s).Count }
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
