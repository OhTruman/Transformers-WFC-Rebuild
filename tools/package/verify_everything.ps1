# The everything-build's verify (Integration 2026-10-24), after: SPLIT_HD=1 NOPROMOTE=1 bash work/refresh.sh <head> <hd> D
#   powershell -File tools/package/verify_everything.ps1 -Head <head> [-MenuMapsOnly]
# Pass 1 (W:): the base package alone, HD off (what a player without the pack runs): the three menu-booted TDM maps plus direct boots
# that reach the other code paths' file opens (Integration 2026-10-24) - campaign D1, Escalation Remnant, a DLC map in TDM (Horizon)
# and in Escalation (Pulse); -MenuMapsOnly runs the three only. Pass 2 (X:): the HD pack merged into the base by
# a junction (instant, no 22 GB copy; removed again at the end - the folders stay as built), the HD rows on. Each pass maps its own
# letter (verify_slim -SubstDrive) and unmaps only that letter. Never deletes a package folder; the junction is removed with rmdir on
# the link itself (never a recursive delete through it).
param([Parameter(Mandatory = $true)][string]$Head, [string]$RtBuildDir = '', [switch]$MenuMapsOnly)
$ErrorActionPreference = 'Stop'
$work = 'F:\Transformers Rebuild\Rebuild-Systems\work'
$base = Join-Path $work "playtest_slim_$Head"
$hdp = Join-Path $work "playtest_slim_${Head}_HDpack"
$verify = Join-Path $work "refresh_$Head\tools\package\verify_slim.ps1"
if (-not $RtBuildDir) { $RtBuildDir = Join-Path $work "refresh_$Head\build" }
$pass1Maps = '508:Streets,510:Gorge,507:Debris'
if (-not $MenuMapsOnly) {   # verify_slim's "direct|<name>|<match url>|<campaign chapter>" rows (D1's MapId 101: Gameplay)
    $pass1Maps += ',direct|D1|D1_ORB_Base_m?GameModeTag=SOLO?MapId=101|D1_ORB,direct|Remnant_ESC|MP_ESC_Remnant?GameModeTag=ESC|' +
                  ',direct|Horizon_TDM|MP_IAC_Horizon?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2|,direct|Pulse_ESC|MP_ESC_Pulse?GameModeTag=ESC|'
}
foreach ($p in @($base, $hdp, $verify)) { if (-not (Test-Path -LiteralPath $p)) { throw "missing: $p" } }
$link = Join-Path $base 'Transformers Rebuild\AssetTools\out\hd'
$target = Join-Path $hdp 'Transformers Rebuild\AssetTools\out\hd'
if (Test-Path -LiteralPath $link) { throw "the base already has AssetTools\out\hd ($link): not a split package?" }

Write-Host "== pass 1 (W:): base, HD off"
& $verify -Package $base -WorkDir (Join-Path $work "verify_${Head}_base") -SubstDrive W: -SkipIntro -ForeignCwd -NoHd -RtBuildDir $RtBuildDir -Maps $pass1Maps
$r1 = $LASTEXITCODE

Write-Host "== pass 2 (X:): base + HD pack (junction)"
cmd /c "mklink /J `"$link`" `"$target`"" | Out-Null
try {
    & $verify -Package $base -WorkDir (Join-Path $work "verify_${Head}_hd") -SubstDrive X: -SkipIntro -ForeignCwd -RtBuildDir $RtBuildDir
    $r2 = $LASTEXITCODE
} finally {
    $item = Get-Item -LiteralPath $link -ErrorAction SilentlyContinue
    if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { cmd /c "rmdir `"$link`"" | Out-Null }   # the link only
}
Write-Host ("VERIFY EVERYTHING: base (W:) exit {0}, base + HD (X:) exit {1}" -f $r1, $r2)
if ($r1 -ne 0 -or $r2 -ne 0) { exit 1 }
