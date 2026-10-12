#!/bin/bash
# Attribution of the Gorge rp 4K gain on 65e856f (Integration 2026-10-11): WFC_NOHUDSHADOWCACHE=1, 300 s x2, with one cache-ON 300 s
# control between them (off, on, off) for drift. Runs at the slot A/B boundary: waits for "slot B open" in the slots log, stops the
# slots script during B's (discarded) warm-up row - script shells first, then PowerShell, then the exe - keeps PERF_HOLD armed,
# runs the rows, then restarts the slots at B. A contaminated row is rerun once; twice = stop.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
H=65e856f; O=work/permap/m1a-$H; CS=tools/fidelity/capacity-stress.ps1; LOG=tools/fidelity/results/PERFORMANCE_LOG.md; SL=work/slots_65e856f.log
until grep -q "slot B open\|ABORTED\|STOPPED\|all slots done" $SL; do sleep 10; done
grep -q "slot B open" $SL || { echo "slots ended without reaching B - attribution not run"; exit 1; }
echo "experimental PERF RUN M1 attribution $H (WFC_NOHUDSHADOWCACHE) $(date +%H:%M)" > work/PERF_HOLD
ps1() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { $1 -and \$_.CommandLine -notlike '*Get-CimInstance*' } | % { \$_.ProcessId }"; }
for p in $(ps1 "\$_.CommandLine -like '*slots_65e856f.sh*'"); do taskkill //PID $p //F > /dev/null 2>&1 && echo "killed slots shell $p"; done
for p in $(ps1 "\$_.CommandLine -like '*capacity-stress.ps1*m9c_65e856f*'"); do taskkill //PID $p //F > /dev/null 2>&1 && echo "killed harness $p"; done
for p in $(ps1 "\$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe'"); do taskkill //PID $p //F > /dev/null 2>&1 && echo "killed exe $p"; done
sleep 5; left=$(ps1 "(\$_.CommandLine -like '*slots_65e856f.sh*' -or \$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe')"); [ -n "$left" ] && { echo "leftovers $left - stop"; exit 1; }
W=$(ls -d work/permap/m1f-$H-509-rp-2160-t150-r0 2>/dev/null); [ -n "$W" ] && { mkdir -p work/permap/m1f-$H-discarded; mv $W $W.out work/permap/m1f-$H-discarded/ 2>/dev/null; }
echo "slots stopped at B warm-up $(date +%H:%M:%S); PERF_HOLD kept"
eval "$(sed -n '/^mem()/p' work/batch_regress.sh)"
foreign() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.Name -like 'wfc_rebuild*' -or \$_.Name -like 'hd*' -or (\$_.Name -eq 'python.exe' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') } | % { '{0} {1} {2}' -f \$_.ProcessId, \$_.Name, \$_.ExecutablePath }"; }
waitclear() { while true; do f=$(foreign); [ -z "$f" ] && break; echo "$(date +%H:%M:%S) waiting on: $(echo $f | tr '\n' ' ')"; sleep 15; done; }
export WFC_GATE_IGNORE_HOLD=1; mem attrib-start
row() {  # row <tag> <extra env>
  local d=$O-510-rp-2160-t300-$1
  for try in 1 2; do
    waitclear
    powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$CS' -Root 'work/ab/m9c_$H' -OutDir '$d' -Maps 510 -Pops t64 -Resolutions 1920x1080 -TimeLimit 300 -PlayerBot 1 -NoSplit -RenderSize 3840x2160 -ExtraEnv 'WFC_SLOWFRAME=3.33$2' -PerfLog '$LOG' -Note 'M1 attribution $H Gorge rp 4K 300s $1'" > $d.out 2>&1
    local c=$(ls $d/*/CONTAMINATED.txt 2>/dev/null)
    echo "$(date +%H:%M) $1 try $try: p90 $(powershell -NoProfile -Command "(Import-Csv '$d/capacity.csv' | ? { \$_.match -eq '2' } | Select-Object -First 1).p90_ms") $c"
    [ -z "$c" ] && return 0
    [ $try = 2 ] && { echo "STOP: $d contaminated twice"; return 1; }
    mv $d $d-contam1; mv $d.out $d-contam1.out
  done
}
row off1 ";WFC_NOHUDSHADOWCACHE=1" && row on1 "" && row off2 ";WFC_NOHUDSHADOWCACHE=1" || { mem attrib-stop; unset WFC_GATE_IGNORE_HOLD; rm -f work/PERF_HOLD; echo "attribution STOPPED $(date +%H:%M)"; exit 1; }
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/pooled-p90.ps1 -Dirs "$O-510-rp-2160-t300-off1,$O-510-rp-2160-t300-off2" | tee $O-off-pooled.txt
mem attrib-end; echo "attribution done $(date +%H:%M) - restarting slots B C D"
unset WFC_GATE_IGNORE_HOLD
exec bash work/slots_65e856f.sh B C D >> work/slots_65e856f.log 2>&1
