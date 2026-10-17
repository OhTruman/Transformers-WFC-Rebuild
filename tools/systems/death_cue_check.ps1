# Local death cue check (Systems, SYSTEMS_LOCAL_DEATH_SOUND): (1) headless BOTTEST SIMHASH base vs new (null render, seed 7):
# identical (audio only); (2) new exe WFC_EVENTTEST (null render): 2 matches x 3 local deaths (killed, suicide, KillZ) ->
# the hook fires 6 times (no audio device headless: cues read 'none'); (2b) the same windowed + muted: 6 started; (3) new exe paced Rust TDM 4 v 5 + playerbot (lockstep + WFC_FRAMELIMIT=60, RT off,
# windowed, muted, NOINTERACT, WFC_CUELOG): every local death line pairs with a CUE HUD_DEATH_POWER_DOWN start.
# One exe at a time, killed by PID on timeout, waits for PERF_HOLD.
param([switch]$HeadlessOnly, [switch]$PacedOnly, [Parameter(Mandatory)] [string]$BaseBin, [Parameter(Mandatory)] [string]$NewBin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\death_cue',
      [int]$Seconds = 120)
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
function Run([string]$bin, [string]$dir, [hashtable]$envs) {
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $base = @{ WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
               WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_GPULOST_DIALOG_S = '0'; WFC_DEATHAUDIOLOG = '1' }
    foreach ($k in $base.Keys) { Set-Item "env:$k" $base[$k] }
    foreach ($k in $envs.Keys) { if ($envs[$k] -ne '') { Set-Item "env:$k" $envs[$k] } }
    # windowed runs as off_identity.ps1 / eyepass_run.ps1 launch them (a redirected, no-new-window start drew no frames there)
    $p = if ($envs.ContainsKey('WFC_NULLRENDER')) { Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $dir -RedirectStandardOutput "$dir\out.txt" -RedirectStandardError "$dir\err.txt" -NoNewWindow -PassThru }
         else { Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru }
    $null = $p.Handle
    if (-not $p.WaitForExit(600000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT (killed PID $($p.Id))" }
}
function Hashes([string]$log) { @(Select-String -LiteralPath $log -Pattern 'SIMHASH \d+ [0-9a-f]{16}$' | ForEach-Object { $_.Line -replace '^.*SIMHASH', '' }) }
if (-not $PacedOnly) {
# (1) SIMHASH
Run $BaseBin (Join-Path $Out 'simhash_base') @{ WFC_NULLRENDER = '1'; WFC_BOTTEST = '1'; WFC_SIMHASH = '1' }
Run $NewBin (Join-Path $Out 'simhash_new') @{ WFC_NULLRENDER = '1'; WFC_BOTTEST = '1'; WFC_SIMHASH = '1' }
$a = Hashes (Join-Path $Out 'simhash_base\wfc.log'); $b = Hashes (Join-Path $Out 'simhash_new\wfc.log')
$diff = 0; for ($i = 0; $i -lt [Math]::Min($a.Count, $b.Count); $i++) { if ($a[$i] -ne $b[$i]) { $diff++ } }
Write-Host ("(1) SIMHASH base {0} steps, new {1}, differing {2}: {3}" -f $a.Count, $b.Count, $diff, $(if ($diff -eq 0 -and $a.Count -eq $b.Count -and $a.Count) { 'IDENTICAL' } else { 'DIFFER' }))
# (2) EVENTTEST
$d2 = Join-Path $Out 'eventtest_new'
Run $NewBin $d2 @{ WFC_NULLRENDER = '1'; WFC_EVENTTEST = '1' }
$l2 = Join-Path $d2 'wfc.log'
$started = @(Select-String -LiteralPath $l2 -Pattern 'LOCAL DEATH cue started').Count
$none = @(Select-String -LiteralPath $l2 -Pattern 'LOCAL DEATH cue none').Count
$ev = @(Select-String -LiteralPath $l2 -Pattern 'EVENTTEST (PASS|FAIL)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
Write-Host ("(2) EVENTTEST headless: local death hook fired {0} times (expect 6; the null render has no audio device, so every cue reads 'none' by design: started {1}, none {2}); EVENTTEST rows PASS {3} FAIL {4}" -f ($started + $none), $started, $none,
            @($ev | Where-Object { $_ -eq 'PASS' }).Count, @($ev | Where-Object { $_ -eq 'FAIL' }).Count)
}
if (-not $HeadlessOnly) {
# (2b) EVENTTEST with the audio device (windowed, muted): the 6 cues must start
$d2b = Join-Path $Out 'eventtest_audio_new'
Run $NewBin $d2b @{ WFC_EVENTTEST = '1'; WFC_SKIPINTRO = '1'; WFC_RT_MODE = '0'; WFC_CUELOG = '1' }
$l2b = Join-Path $d2b 'wfc.log'
$s2b = @(Select-String -LiteralPath $l2b -Pattern 'LOCAL DEATH cue started').Count; $n2b = @(Select-String -LiteralPath $l2b -Pattern 'LOCAL DEATH cue none').Count
Write-Host ("(2b) EVENTTEST with audio: cues started {0}, none {1} (expect 6 / 0): {2}" -f $s2b, $n2b, $(if ($s2b -eq 6 -and $n2b -eq 0) { 'PASS' } else { 'FAIL' }))
# (3) paced match
$d3 = Join-Path $Out 'paced_rust_new'
Run $NewBin $d3 @{ WFC_SKIPINTRO = '1'; WFC_LOCKSTEP = '1'; WFC_FRAMELIMIT = '60'; WFC_PLAYERBOT = '1'; WFC_RT_MODE = '0'; WFC_LOGTIME = '1'; WFC_CUELOG = '1';
                   WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_SMOKE_FRAMES = '2000000'; WFC_MATCH_URL = 'MP_IAC_Rust?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000' }
$l3 = Join-Path $d3 'wfc.log'
$deaths = @(Select-String -LiteralPath $l3 -Pattern 'LOCAL DEATH cue started').Count
$cues = @(Select-String -LiteralPath $l3 -Pattern 'CUE BL_LVL_HUD_INTERFACE\.HUD_DEATH_POWER_DOWN ').Count
$nochan = @(Select-String -LiteralPath $l3 -Pattern 'HUD_DEATH_POWER_DOWN.*no channel|no channel.*HUD_DEATH_POWER_DOWN').Count
Write-Host ("(3) paced Rust {0} s: local deaths with the cue started {1}; mixer CUE starts of HUD_DEATH_POWER_DOWN {2}; 'no channel' {3}: {4}" -f $Seconds, $deaths, $cues, $nochan,
            $(if ($deaths -gt 0 -and $cues -ge $deaths -and $nochan -eq 0) { 'PASS' } elseif ($deaths -eq 0) { 'NO LOCAL DEATH (extend the run)' } else { 'FAIL' }))
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
