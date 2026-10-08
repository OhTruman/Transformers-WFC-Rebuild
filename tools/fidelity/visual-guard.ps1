# VISUAL GUARD (tier TARGETED; user 2026-10-07: optimizations may cut only INVISIBLE work). Per optimization merge, the same
# deterministic match is captured on the REFERENCE build and the OPTIMIZED build and compared frame by frame:
#   direct boot, WFC_MATCH_URL (Streets TDM with bots), WFC_LOCKSTEP (one 60 Hz step per frame), WFC_SEED fixed, the same
#   scripted input -> identical simulation on both builds, so frame N shows the same moment.
# Two camera sets:
#   fixed   WFC_FIXEDCAM (Rendering's Streets overview): static view, characters moving through it
#   moving  the player's own camera with walk + strafe + turn: geometry / characters entering the view (pop-in shows up as a
#           transient A/B difference on the frames where an object appears earlier / later on one build)
# Every -Stride-th frame in [-From, -To] is compared: mean abs diff (0-255) and the share of pixels differing by > -Threshold.
# A frame is FLAGGED when more than -FlagPct % of pixels differ; flagged frames get a diff heatmap and a side-by-side.
# HUMAN verdict: a person judges whether flagged differences are visible quality changes; unflagged sequences PASS.
# ALIGNMENT: when BOTH builds have Gameplay's WFC_SHOTMATCH, frames are captured by STEPS SINCE THE MATCH WENT InProgress
# (m<step>.bmp; -From / -To / -Stride are match steps) and the run ends at a fixed match time (WFC_MATCH_SECONDS), so the same
# file is the same simulation moment on both builds whatever the load took. Otherwise WFC_SHOTEVERY counts frames FROM BOOT:
# the load-frame count varies per run, so only a static scene compares cleanly (use -Sets fixed -Bots 0 and a -From well past
# the load, e.g. 7000); characters / the moving cam are then offset and flagged frames are not evidence of a change.
# Note: the simulation must be deterministic across the two builds (sim-determinism.ps1); if a gameplay change alters
# the simulation, characters will diverge and the moving set flags everything - then compare the fixed set's static parts.
#
#   .\tools\fidelity\visual-guard.ps1 -Ref work\ab\<reference> -Opt work\ab\<optimized> -OutDir <dir> [-From 300] [-To 900] [-Stride 10] [-Bots 8]
param([Parameter(Mandatory)][string]$Ref, [Parameter(Mandatory)][string]$Opt, [Parameter(Mandatory)][string]$OutDir, [int]$From = 300, [int]$To = 900,
      [int]$Stride = 10, [int]$Bots = 8, [int]$Seed = 123, [int]$Threshold = 24, [double]$FlagPct = 0.5, [string]$Cam = "100,-700,-680,-141.6,-12",
      [string[]]$Sets = @("fixed", "moving"), [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$Sets = @($Sets | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "visual.$id" $status $null $note $owner }
$url = "MP_IAC_Streets?GameModeTag=TDM?BotsAutobot={0}?BotsDecepticon={0}?BotDifficulty=1?ExtendedPlayers=1" -f $Bots
function Sha($root) { $f = Join-Path $root "M05_TARGET.txt"; if (Test-Path $f) { (((Get-Content $f) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '').Substring(0, 7) } else { Split-Path $root -Leaf } }
$builds = @(@{ role = "ref"; root = (Resolve-Path $Ref).Path }, @{ role = "opt"; root = (Resolve-Path $Opt).Path })
foreach ($b in $builds) { $b.sha = Sha $b.root; $b.hooks = Get-ExeHooks (Join-Path $b.root "build-release\bin\wfc_rebuild.exe") }
$matchAligned = @($builds | Where-Object { $_.hooks.Contains("WFC_SHOTMATCH") }).Count -eq 2
$shotGlob = if ($matchAligned) { "m*.bmp" } else { "f*.bmp" }
Res "alignment" "INFO" $(if ($matchAligned) { "WFC_SHOTMATCH on both builds: frames are match steps $From..$To since InProgress (aligned)" } else { "WFC_SHOTEVERY (frames from boot, NOT aligned across runs): only static content (fixed cam, 0 bots) compares; moving / character differences are offset artefacts" }) "Experimental"
foreach ($set in $Sets) { foreach ($b in $builds) {
    $d = Join-Path $OutDir "$set\$($b.role)"; New-Item -ItemType Directory -Force $d | Out-Null
    if ($ReportOnly -or (Test-Path (Join-Path $d "done.txt"))) { continue }
    if (-not (Wait-WfcGpu)) { Res "$set.$($b.role).gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
    $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_LOCKSTEP = "1"; WFC_SEED = "$Seed"; WFC_SMOKE_FRAMES = "$($To + 5)"; WFC_LOGEVERY = "0"; WFC_NOMOUSE = "1"
            WFC_VISUALCHECK = "1" }
    if ($matchAligned) { $e.WFC_SHOTMATCH = "$d,$From,$To,$Stride"; $e.WFC_MATCH_SECONDS = "$([Math]::Ceiling($To / 60.0) + 1)"; $e.WFC_SMOKE_FRAMES = "1000000" }
    else { $e.WFC_SHOTEVERY = "$d,$From,$To" }
    if ($b.hooks.Contains("WFC_FLOWSEED")) { $e.WFC_FLOWSEED = "$Seed" }   # GameFlow RNG is clock-seeded otherwise
    if ($set -eq "fixed") { $e.WFC_FIXEDCAM = $Cam } else { $e.WFC_AUTOWALK = "1"; $e.WFC_AUTOSTRAFE = "1"; $e.WFC_AUTOTURN = "0.6" }
    $exe = Join-Path $b.root "build-release\bin\wfc_rebuild.exe"
    $null = Invoke-WfcExe $exe $d $e "run.log" 1200
    # keep every -Stride-th frame (every frame is written; 1080p BMPs are ~6 MB)
    if (-not $matchAligned) { Get-ChildItem $d -Filter "f*.bmp" | Where-Object { ([int]($_.BaseName.Substring(1)) - $From) % $Stride -ne 0 } | Remove-Item }
    "done" | Set-Content (Join-Path $d "done.txt")
} }
# compare
Add-Type -TypeDefinition @"
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class VgDiff {
    public static double[] Compare(string a, string b, int thr, string heat) {
        using (var A = new Bitmap(a)) using (var B = new Bitmap(b)) {
            int w = Math.Min(A.Width, B.Width), h = Math.Min(A.Height, B.Height);
            var ra = A.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            var rb = B.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            byte[] pa = new byte[ra.Stride * h], pb = new byte[rb.Stride * h];
            Marshal.Copy(ra.Scan0, pa, 0, pa.Length); Marshal.Copy(rb.Scan0, pb, 0, pb.Length);
            A.UnlockBits(ra); B.UnlockBits(rb);
            double sum = 0; long over = 0; var H = heat != null ? new Bitmap(w, h, PixelFormat.Format24bppRgb) : null;
            byte[] ph = H != null ? new byte[ra.Stride * h] : null;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                int i = y * ra.Stride + x * 3, j = y * rb.Stride + x * 3;
                int d = Math.Max(Math.Abs(pa[i] - pb[j]), Math.Max(Math.Abs(pa[i + 1] - pb[j + 1]), Math.Abs(pa[i + 2] - pb[j + 2])));
                sum += d; if (d > thr) over++;
                if (ph != null) { byte v = (byte)Math.Min(255, d * 4); ph[i] = 0; ph[i + 1] = (byte)(v / 3); ph[i + 2] = v; }
            }
            if (H != null) { var rh = H.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb); Marshal.Copy(ph, 0, rh.Scan0, ph.Length); H.UnlockBits(rh); H.Save(heat, ImageFormat.Png); H.Dispose(); }
            // view sanity: mean luma of A and the share of A's pixels within +-6 of A's median luma ("flat")
            long[] hist = new long[256]; double lsum = 0;
            for (int y = 0; y < h; y += 4) for (int x = 0; x < w; x += 4) { int i = y * ra.Stride + x * 3; int l = (pa[i + 2] * 299 + pa[i + 1] * 587 + pa[i] * 114) / 1000; hist[l]++; lsum += l; }
            long n = 0; foreach (long c in hist) n += c; long acc = 0; int med = 0; for (int k = 0; k < 256; k++) { acc += hist[k]; if (acc * 2 >= n) { med = k; break; } }
            long near = 0; for (int k = Math.Max(0, med - 6); k <= Math.Min(255, med + 6); k++) near += hist[k];
            return new double[] { sum / ((double)w * h), 100.0 * over / ((double)w * h), lsum / n, 100.0 * near / n };
        }
    }
}
"@ -ReferencedAssemblies System.Drawing
$rows = New-Object System.Collections.Generic.List[object]
foreach ($set in $Sets) {
    $dr = Join-Path $OutDir "$set\ref"; $do = Join-Path $OutDir "$set\opt"
    $frames = @(Get-ChildItem $dr -Filter $shotGlob -ErrorAction SilentlyContinue | ForEach-Object { $_.Name } | Where-Object { Test-Path (Join-Path $do $_) } | Sort-Object)
    if (-not $frames.Count) { Res "$set" "UNKNOWN" "no frame pairs captured" "Experimental"; continue }
    $flagged = @()
    foreach ($f in $frames) {
        $heat = Join-Path $OutDir "$set\heat_$($f -replace '\.bmp$', '.png')"
        $r = [VgDiff]::Compare((Join-Path $dr $f), (Join-Path $do $f), $Threshold, $heat)
        $flag = $r[1] -gt $FlagPct
        if (-not $flag) { Remove-Item $heat -ErrorAction SilentlyContinue } else { $flagged += $f }
        $rows.Add([pscustomobject][ordered]@{ set = $set; frame = ($f -replace '\.bmp$', ''); mean_abs_diff = [Math]::Round($r[0], 3); pct_over = [Math]::Round($r[1], 3); flagged = $flag
            ref_luma = [Math]::Round($r[2], 1); ref_flat_pct = [Math]::Round($r[3], 1) })
    }
    if ($flagged.Count) {   # side-by-side sheet of the flagged frames (ref | opt | heat), up to 8
        $tiles = @(); foreach ($f in @($flagged | Select-Object -First 8)) { $n = $f -replace '\.bmp$', ''
            $tiles += @(@{ png = (Join-Path $dr $f); label = "$set $n ref" }); $tiles += @(@{ png = (Join-Path $do $f); label = "$set $n opt" }); $tiles += @(@{ png = (Join-Path $OutDir "$set\heat_$n.png"); label = "$set $n diff" }) }
        New-WfcSheet $tiles (Join-Path $OutDir "flagged_$set.png") 3 480 270 }
    $mx = ($rows | Where-Object { $_.set -eq $set } | Measure-Object pct_over -Maximum).Maximum
    $sheetNote = if ($flagged.Count) { "; see flagged_$set.png (ref | opt | diff)" } else { "" }
    $note = "$($builds[0].sha) vs $($builds[1].sha), $($frames.Count) frames compared ($(if ($matchAligned) { 'match steps' } else { 'boot frames' }) $From..$To every $Stride); flagged (> $FlagPct % pixels differ by > $Threshold): $($flagged.Count) [$(($flagged | ForEach-Object { $_ -replace '\.bmp$', '' }) -join ' ')]; max $mx %$sheetNote"
    # a comparison of near-black / flat frames proves nothing: void it (mean luma < 30 or > 85 % of pixels within +-6 of the median)
    $setRows = @($rows | Where-Object { $_.set -eq $set }); $bad = @($setRows | Where-Object { $_.ref_luma -lt 30 -or $_.ref_flat_pct -gt 85 })
    if ($bad.Count * 2 -gt $setRows.Count) {
        Res "$set" "UNKNOWN" ("VIEW INVALID - {0} of {1} reference frames are near-black / flat (median luma {2}, flat {3} %): the camera sees nothing to compare (check -Cam with a screenshot); {4}" -f $bad.Count, $setRows.Count, (($setRows | Sort-Object ref_luma)[[int]($setRows.Count / 2)]).ref_luma, (($setRows | Sort-Object ref_flat_pct)[[int]($setRows.Count / 2)]).ref_flat_pct, $note) "Experimental"; continue }
    Res "$set" $(if ($flagged.Count) { "HUMAN" } else { "PASS" }) $note "Rendering"
}
Write-WfcCsv $rows (Join-Path $OutDir "visual.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"VISUAL GUARD: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
