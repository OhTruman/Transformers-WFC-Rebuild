#!/bin/bash
# 509 rp 4K 300 s x3 + pooled on 65e856f (Integration 2026-10-12, after slot D): one discarded 150 s warm-up, same rules as Gorge.
# Waits for "all slots done" and re-arms PERF_HOLD within ~1 s of the slots script clearing it. Contaminated twice = stop.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
H=65e856f; O=work/permap/m1g-$H; CS=tools/fidelity/capacity-stress.ps1; LOG=tools/fidelity/results/PERFORMANCE_LOG.md; SL=work/slots_65e856f.log
until grep -q "all slots done\|ABORTED\|STOPPED" $SL; do [ -f work/PERF_HOLD ] || grep -q "all slots done" $SL || true; sleep 1; done
grep -q "all slots done" $SL || { echo "slots did not finish cleanly - 509 rows not run"; exit 1; }
echo "experimental PERF RUN M1 509 rp 4K 300s x3 $H $(date +%H:%M)" > work/PERF_HOLD; echo "hold re-armed $(date +%H:%M:%S)"
eval "$(sed -n '/^mem()/p' work/batch_regress.sh)"
foreign() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.Name -like 'wfc_rebuild*' -or \$_.Name -like 'hd*' -or (\$_.Name -eq 'python.exe' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') } | % { '{0} {1} {2}' -f \$_.ProcessId, \$_.Name, \$_.ExecutablePath }"; }
waitclear() { while true; do f=$(foreign); [ -z "$f" ] && break; echo "$(date +%H:%M:%S) waiting on: $(echo $f | tr '\n' ' ')"; sleep 15; done; }
export WFC_GATE_IGNORE_HOLD=1; mem m509-start
row() {  # row <rep> <seconds> [nolog]
  local d=$O-509-rp-2160-t$2-r$1 PL="-PerfLog '$LOG'"
  [ -n "$3" ] && PL=""
  for try in 1 2; do
    waitclear
    powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$CS' -Root 'work/ab/m9c_$H' -OutDir '$d' -Maps 509 -Pops t64 -Resolutions 1920x1080 -TimeLimit $2 -PlayerBot 1 -NoSplit -RenderSize 3840x2160 -ExtraEnv 'WFC_SLOWFRAME=3.33' $PL -Note 'M1 509 rp 4K ${2}s rep $1 $H'" > $d.out 2>&1
    local c=$(ls $d/*/CONTAMINATED.txt 2>/dev/null)
    echo "$(date +%H:%M) 509 rp 2160 ${2}s r$1 try $try: p90 $(powershell -NoProfile -Command "(Import-Csv '$d/capacity.csv' | ? { \$_.match -eq '2' } | Select-Object -First 1).p90_ms") $c"
    [ -z "$c" ] && return 0
    [ $try = 2 ] && { echo "STOP: $d contaminated twice"; return 1; }
    mv $d $d-contam1; mv $d.out $d-contam1.out
  done
}
stop() { mem m509-stop; unset WFC_GATE_IGNORE_HOLD; rm -f work/PERF_HOLD; echo "509 rows STOPPED $(date +%H:%M)"; exit 1; }
row 0 150 nolog || stop
for r in 1 2 3; do row $r 300 || stop; done
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/pooled-p90.ps1 -Dirs "$O-509-rp-2160-t300-r1,$O-509-rp-2160-t300-r2,$O-509-rp-2160-t300-r3" | tee $O-509-rp-pooled.txt
mem m509-end
for p in $(powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe' } | % { \$_.ProcessId }"); do echo "leftover $p"; done
unset WFC_GATE_IGNORE_HOLD; rm -f work/PERF_HOLD; echo "509 rows done $(date +%H:%M)"
