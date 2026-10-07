# FIXED-CAM SWEEP (tier TARGETED; 2026-10-07: the Streets perf cam "100,-700,-680" looked into a wall for every fixed-cam row).
# Picks a VERIFIED overview camera per map: each candidate WFC_FIXEDCAM is booted directly into the map with bots (match URL),
# one frame is captured after the match is running, and the frame is scored (mean luma, % flat pixels, edge density = how much
# structure is in view). Candidates are aimed at the arena centre from raised points around the playable area; the playable
# area comes from BOTLOG positions of a previous run (-FromLog) or from -Center / -Extent. Output: a contact sheet + cams.csv.
# The person choosing still LOOKS at the sheet (HUMAN): the score only rules out walls / black / sky-only views.
#
#   .\tools\fidelity\cam-sweep.ps1 -Root work\ab\<target> -OutDir <dir> -Map MP_IAC_Streets -FromLog <wfc.log with BOTLOG> [-Bots 8]
#   .\tools\fidelity\cam-sweep.ps1 ... -Cams "x,y,z,yaw,pitch","x,y,z,yaw,pitch"     (explicit candidates)
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [Parameter(Mandatory)][string]$Map,
      [string]$FromLog = "", [string[]]$Cams = @(), [int]$Bots = 8, [double[]]$Heights = @(60, 100), [int]$Frame = 2400, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -AssemblyName System.Drawing
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "camsweep.$id" $status $null $note $owner }
$Cams = @($Cams | ForEach-Object { "$_" -split ';' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
# yaw convention (WFC_FIXEDCAM, matches Rendering's cams): yaw = atan2(-dx, -dz) for the direction (dx, dz) toward the target;
# y is up; pitch negative = looking down.
function Aim($cx, $cy, $cz, $tx, $ty, $tz) {
    $dx = $tx - $cx; $dy = $ty - $cy; $dz = $tz - $cz
    $yaw = [Math]::Atan2(-$dx, -$dz) * 180 / [Math]::PI; $pitch = [Math]::Atan2($dy, [Math]::Sqrt($dx * $dx + $dz * $dz)) * 180 / [Math]::PI
    return ("{0:N1},{1:N1},{2:N1},{3:N1},{4:N1}" -f $cx, $cy, $cz, $yaw, $pitch) -replace ' ', ''
}
if (-not $Cams.Count -and $FromLog) {
    $pts = foreach ($l in [IO.File]::ReadLines((Resolve-Path $FromLog).Path)) { $m = [regex]::Match($l, 'BOTLOG \S+ p\d+ \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)'); if ($m.Success) { , @([double]$m.Groups[1].Value, [double]$m.Groups[2].Value, [double]$m.Groups[3].Value) } }
    if (@($pts).Count -lt 50) { Res "extent" "UNKNOWN" "too few BOTLOG positions in $FromLog" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
    function Q($i, $f) { $a = @($pts | ForEach-Object { $_[$i] } | Sort-Object); return $a[[int]($f * ($a.Count - 1))] }
    $x0 = Q 0 0.02; $x1 = Q 0 0.98; $g = Q 1 0.5; $z0 = Q 2 0.02; $z1 = Q 2 0.98; $cx = ($x0 + $x1) / 2; $cz = ($z0 + $z1) / 2
    Res "extent" "INFO" ("playable area from {0} bot samples: x {1:N0}..{2:N0}, z {3:N0}..{4:N0}, ground y {5:N0}; centre ({6:N0}, {7:N0})" -f @($pts).Count, $x0, $x1, $z0, $z1, $g, $cx, $cz) "Experimental"
    # candidates: the four edge midpoints and four corners of the area, raised by -Heights, aimed at the centre (ground level)
    $edge = @(@($cx, $z0), @($cx, $z1), @($x0, $cz), @($x1, $cz), @($x0, $z0), @($x0, $z1), @($x1, $z0), @($x1, $z1))
    foreach ($h in $Heights) { foreach ($p in $edge) { $Cams += (Aim $p[0] ($g + $h) $p[1] $cx $g $cz) } }
}
if (-not $Cams.Count) { throw "no candidates: pass -Cams or -FromLog" }
$url = "{0}?GameModeTag=TDM?BotsAutobot={1}?BotsDecepticon={1}?BotDifficulty=1?ExtendedPlayers=1" -f $Map, $Bots
Add-Type -TypeDefinition @"
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class CamScore {
    // mean luma, % pixels within +-6 of the median luma (flat), % pixels with a strong local gradient (structure in view)
    public static double[] Score(string path) {
        using (var B = new Bitmap(path)) {
            int w = B.Width, h = B.Height; var r = B.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            byte[] p = new byte[r.Stride * h]; Marshal.Copy(r.Scan0, p, 0, p.Length); B.UnlockBits(r);
            Func<int, int, int> L = (x, y) => { int i = y * r.Stride + x * 3; return (p[i + 2] * 299 + p[i + 1] * 587 + p[i] * 114) / 1000; };
            long[] hist = new long[256]; double sum = 0; long n = 0, edges = 0;
            for (int y = 2; y < h - 2; y += 3) for (int x = 2; x < w - 2; x += 3) { int l = L(x, y); hist[l]++; sum += l; n++;
                if (Math.Abs(L(x + 2, y) - L(x - 2, y)) + Math.Abs(L(x, y + 2) - L(x, y - 2)) > 24) edges++; }
            long acc = 0; int med = 0; for (int k = 0; k < 256; k++) { acc += hist[k]; if (acc * 2 >= n) { med = k; break; } }
            long near = 0; for (int k = Math.Max(0, med - 6); k <= Math.Min(255, med + 6); k++) near += hist[k];
            return new double[] { sum / n, 100.0 * near / n, 100.0 * edges / n };
        }
    }
}
"@ -ReferencedAssemblies System.Drawing
$rows = New-Object System.Collections.Generic.List[object]; $tiles = @()
for ($i = 0; $i -lt $Cams.Count; $i++) {
    $cam = $Cams[$i]; $d = Join-Path $OutDir ("c{0:D2}" -f $i); New-Item -ItemType Directory -Force $d | Out-Null
    $shot = Join-Path $d ("f{0:D4}.bmp" -f $Frame)
    if (-not $ReportOnly -and -not (Test-Path $shot)) {
        if (-not (Wait-WfcGpu)) { Res "c$i.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_FIXEDCAM = $cam; WFC_SMOKE_FRAMES = "$($Frame + 5)"; WFC_LOGEVERY = "0"; WFC_NOMOUSE = "1"
                WFC_SHOTEVERY = "$d,$Frame,$Frame" }
        $null = Invoke-WfcExe $exe $d $e "run.log" 400
    }
    if (-not (Test-Path $shot)) { Res "c$i" "UNKNOWN" "$cam - no capture" "Experimental"; continue }
    $s = [CamScore]::Score($shot)
    $ok = $s[0] -ge 30 -and $s[1] -le 60 -and $s[2] -ge 4
    $png = Join-Path $d "view.png"; $b = New-Object System.Drawing.Bitmap $shot; $t = New-Object System.Drawing.Bitmap $b, 480, 270; $t.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); $t.Dispose(); $b.Dispose()
    $rows.Add([pscustomobject][ordered]@{ id = "c$i"; cam = $cam; luma = [Math]::Round($s[0], 1); flat_pct = [Math]::Round($s[1], 1); edge_pct = [Math]::Round($s[2], 1); usable = $ok })
    $tiles += @(@{ png = $png; label = ("c{0} {1} L{2:N0} F{3:N0}% E{4:N0}%{5}" -f $i, $cam, $s[0], $s[1], $s[2], $(if ($ok) { "" } else { " X" })) })
}
if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "cams.png") 4 480 270 }
Write-WfcCsv $rows (Join-Path $OutDir "cams.csv")
$use = @($rows | Where-Object { $_.usable } | Sort-Object edge_pct -Descending)
Res "pick" $(if ($use.Count) { "HUMAN" } else { "FAIL" }) $(if ($use.Count) { "usable candidates by structure in view: " + (($use | Select-Object -First 4 | ForEach-Object { "$($_.id) $($_.cam) (E $($_.edge_pct) %)" }) -join "; ") + " - look at cams.png and pick the widest lit view of the arena" } else { "no candidate passed the view check (luma >= 30, flat <= 60 %, edges >= 4 %)" }) "Experimental"
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"CAM SWEEP: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
