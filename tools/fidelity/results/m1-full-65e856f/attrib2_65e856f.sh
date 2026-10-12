#!/bin/bash
# Attribution of the Gorge rp 4K gain on 65e856f (Integration 2026-10-11): WFC_NOHUDSHADOWCACHE=1, 300 s x2, with one cache-ON 300 s
# control between them (off, on, off) for drift. Runs at the slot A/B boundary: waits for "slot B open" in the slots log, stops the
# slots script during B's (discarded) warm-up row - script shells first, then PowerShell, then the exe - keeps PERF_HOLD armed,
# runs the rows, then restarts the slots at B. A contaminated row is rerun once; twice = stop.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
H=65e856f; O=work/permap/m1a-$H; CS=tools/fidelity/capacity-stress.ps1; LOG=tools/fidelity/results/PERFORMANCE_LOG.md; SL=work/slots_65e856f.log
# restart after the in-script stop failed (taskkill on the msys shells did not take; stopped by PID with Stop-Process at 22:53:59)
echo "attribution resumed $(date +%H:%M:%S); PERF_HOLD kept"
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
