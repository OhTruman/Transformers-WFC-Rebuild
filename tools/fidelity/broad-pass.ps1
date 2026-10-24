# Per-merge headless set (Experimental): build the ref, then ESC soak, CAMPAIGNTEST + D5 soak, KISMET, VOICE, five profiles, late join
# (+ pick early), relay, leave switch. Native Windows paths throughout. Usage: powershell -File tools/fidelity/broad-pass.ps1 -Ref <sha> -Tag <short> [-SkipBuild]
param([Parameter(Mandatory)][string]$Ref, [Parameter(Mandatory)][string]$Tag, [switch]$SkipBuild)
$r = 'F:\Transformers Rebuild\Rebuild-Experimental'; Set-Location $r
$L = "$r\work\udp\batch_$Tag.log"; function T($m) { $s = "$(Get-Date -Format HH:mm:ss) $m"; Add-Content -LiteralPath $L $s; $s }
$bash = 'C:\Program Files\Git\bin\bash.exe'; $py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
if (-not $SkipBuild) { T "build $Ref"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\fidelity\m05\build-target.ps1" -Ref $Ref -Name "fast_$Tag" -Jobs 3 -NoDebug -ThirdParty work/ab/_third_party *> "$r\work\build_$Tag.log"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\fidelity\link-render.ps1" -Target "work/ab/fast_$Tag" *>> "$r\work\build_$Tag.log" }
$b ="$r\work\ab\fast_$Tag\build-release\bin"; $E = "$b\wfc_rebuild.exe"; if (-not (Test-Path $E)) { T 'NO EXE'; exit 1 }
foreach ($f in 'tools/systems/net_udp_profiles.ps1', 'tools/net/netshim.py', 'tools/net/lobby_service.py') { git -C $r show "${Ref}:$f" | Set-Content -Encoding utf8 "$r\work\udp\$Tag`_$(Split-Path $f -Leaf)" }
function Run-Headless($dir, $envs, $secs) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $saved = @{}; Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
    $base = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_AUTODETECT = '0' }; foreach ($k in $envs.Keys) { $base[$k] = $envs[$k] }
    foreach ($k in $base.Keys) { Set-Item "env:$k" $base[$k] }
    $p = Start-Process -FilePath $E -WorkingDirectory $dir -WindowStyle Hidden -PassThru -RedirectStandardOutput "$dir\run.log"; $null = $p.Handle
    if (-not $p.WaitForExit($secs * 1000)) { Stop-Process -Id $p.Id -Force; T "  TIMEOUT $dir" }
    Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { Remove-Item -LiteralPath "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
    return $p.ExitCode
}
$W = "$r\work\batch_$Tag"
$rc = Run-Headless "$W\campaigntest" @{ WFC_CAMPAIGNTEST = '1'; WFC_MAP = 'D5_OME' } 300; T "CAMPAIGNTEST exit ${rc}: $((Select-String -LiteralPath "$W\campaigntest\run.log" -Pattern 'CAMPAIGNTEST SUMMARY.*').Matches.Value)"
$rc = Run-Headless "$W\kismet" @{ WFC_KISMETTEST = '1' } 300; T "KISMETTEST exit ${rc}: $((Select-String -LiteralPath "$W\kismet\run.log" -Pattern 'KISMETTEST SUMMARY.*').Matches.Value)"
$rc = Run-Headless "$W\voice" @{ WFC_VOICETEST = '1'; WFC_NULLRENDER_AUDIO = '1' } 240; T "VOICETEST exit ${rc}: $((Select-String -LiteralPath "$W\voice\run.log" -Pattern 'VOICETEST SUMMARY.*').Matches.Value)"
foreach ($c in @(@{n='D5_OME'; u='D5_OME_BASE_m?GameModeTag=SOLO?MapId=105?WfcDifficulty=1'}, @{n='A1_IAC'; u='A1_IAC_BASE_m?GameModeTag=SOLO?MapId=96?WfcDifficulty=1'})) { foreach ($s in 7, 1234) {
    $d = "$W\camp_$($c.n)_s$s"; $rc = Run-Headless $d @{ WFC_BOOT = 'match'; WFC_LOCKSTEP = '1'; WFC_SEED = "$s"; WFC_FLOWSEED = "$s"; WFC_PLAYERBOT = '1'; WFC_MATCH_SECONDS = '300'; WFC_MAP = $c.n; WFC_MATCH_URL = $c.u } 600
    $lg = Get-Content -LiteralPath "$d
un.log"
    T ("$($c.n) seed $s exit $rc, steps $(($lg -match 'HEADLESS done: \d+' | Select-Object -Last 1) -replace '.*HEADLESS done: (\d+).*', '$1'), errors $(@($lg -match '^\[error').Count), strands $(@($lg -match 'stranded off the mesh').Count), moved back $(@($lg -match 'moved back beside').Count), suicides $(@($lg -match ': suicide').Count), game-over $(@($lg -match 'InProgressOver|game over').Count), squad $(@($lg -match 'campaign: AI squad').Count)")
    $lg -match '^\[error' | Group-Object { $_ -replace '\d+', 'N' } | Sort-Object Count -Descending | Select-Object -First 3 | % { T "   $($_.Count)x $($_.Name.Substring(0, [Math]::Min(180, $_.Name.Length)))" }
} }
$escMaps = @('MP_ESC_Remnant', 'MP_ESC_BrokenHope') + @('MP_ESC_Tranquillity', 'MP_ESC_District', 'MP_ESC_Pulse', 'MP_ESC_Static' | ? { Test-Path ('F:/Transformers Rebuild/Rebuild/work/render/' + $_) })   # DLC ESC maps once shipped
foreach ($s in 7, 1234) { foreach ($m in $escMaps) {
    $d = "$W\esc_s$s`_$m"; $rc = Run-Headless $d @{ WFC_BOOT = 'match'; WFC_LOCKSTEP = '1'; WFC_SEED = "$s"; WFC_FLOWSEED = "$s"; WFC_ESCLOG = '1'; WFC_MATCH_SECONDS = '300'; WFC_MAP = $m; WFC_MATCH_URL = "${m}?GameModeTag=ESC?BotsFriendly=4?BotsEnemy=5" } 600
    $lg = Get-Content -LiteralPath "$d\run.log"; $k = @($lg -match 'ESCLOG kill')
    T ("ESC $m seed $s exit $rc, kills $($k.Count) (world $(@($k -match 'by p-1').Count)), waves $(@($lg -match 'wave \d+ begins').Count)/$(@($lg -match 'wave \d+ cleared').Count), errors $(@($lg -match '^\[error').Count), $(($lg -match 'game over at wave' | Select-Object -First 1) -replace '.*(game over at wave \d+).*', '$1')")
} }
T 'TDM 10 maps x 300 s x 2 seeds'; & $bash -c "cd '/f/Transformers Rebuild/Rebuild-Experimental' && bash tools/fidelity/stab-soak.sh '$($E.Replace([char]92, [char]47))' work/batch_$Tag/tdm '7 1234' TDM" 2>&1 | Select-Object -Last 1 | % { T $_ }
$csv = Import-Csv (Join-Path (Join-Path $W 'tdm') 'runs.csv'); T ("TDM: runs $($csv.Count), non-zero exit $(@($csv | ? { $_.rc -ne '0' }).Count), not at time $(@($csv | ? { $_.end -ne 'time' }).Count), errors $(($csv | Measure-Object errors -Sum).Sum), warns $(($csv | Measure-Object warns -Sum).Sum)")
T 'five profiles'; & "$r\work\udp\$Tag`_net_udp_profiles.ps1" -Bin $b -Profiles 'lan,typical,bad,burst,rttjump' -Seconds 60 -Out "$r\work\udp\run_$Tag" -Shim "$r\work\udp\$Tag`_netshim.py" 2>&1 | Select-String -Pattern '^(lan|typical|bad|burst|rttjump) :' | % { T $_.Line.Substring(0, [Math]::Min(200, $_.Line.Length)) }
T 'late join'; & "$r\tools\fidelity\udp-late-join.ps1" -Bin $b -Out "$r\work\udp\late_$Tag" -Seconds 90 -LateAfter 25 2>&1 | % { T "$_" }
T 'late join pick early'; & "$r\tools\fidelity\udp-late-join.ps1" -Bin $b -Out "$r\work\udp\latepick_$Tag" -Seconds 90 -LateAfter 25 -LateEnv 'WFC_UDPPICK_EARLY=1' 2>&1 | % { T "$_" }
T 'relay'; & "$r\tools\fidelity\udp-relay.ps1" -Bin $b -Out "$r\work\udp\relay_$Tag" -Service "$r\work\udp\$Tag`_lobby_service.py" -Seconds 70 2>&1 | % { T "$_" }
T 'leave switch'; & "$r\tools\fidelity\udp-leave.ps1" -Bin $b -Out "$r\work\udp\leave_$Tag" -Mode switch -Seats 3 -Seconds 120 -LeaveAt 60 2>&1 | % { T "$_" }
foreach ($a in "late_$Tag", "latepick_$Tag") { T "$a hashes: $(& $py -I tools/fidelity/netlog_join.py "work/udp/$a/host/net.csv" "work/udp/$a/member/net.csv" "work/udp/$a/late/net.csv" | Select-Object -First 1)" }
T "relay hashes: $(& $py -I tools/fidelity/netlog_join.py "work/udp/relay_$Tag/host/net.csv" "work/udp/relay_$Tag/member/net.csv" | Select-Object -First 1)"
T "leave hashes host~stay: $(& $py -I tools/fidelity/netlog_join.py "work/udp/leave_$Tag/host/net.csv" "work/udp/leave_$Tag/stay/net.csv" | Select-Object -First 1)"
T "own exes left: $(@(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | ? { $_.Path -like "$b*" }).Count)"
T 'DONE'
