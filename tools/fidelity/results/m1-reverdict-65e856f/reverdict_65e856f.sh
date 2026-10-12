#!/bin/bash
# M1 RE-VERDICT 65e856f (Integration 2026-10-11, TIMED slot): head 65e856f = 9c3560e + Rendering (b)+(d)+(d2) + Systems HUD shadow
# scissor / cache (byte-identical; off switches WFC_NOHUDSHADOWCACHE=1, WFC_HUDSHADOWFULLCLEAR=1).
#  (1) regress-8map 9c3560e -> 65e856f, -Batching (BLOCK > +0.15)
#  (2) one DISCARDED warm-up Gorge rp 4K row (GPU clock warm-up, Systems' ABBA), then Gorge rp 4K 300 s x3 + pooled p90
# A contaminated row is rerun once; contaminated again = abort (Integration). Step (3), the 48 cells, is planned separately.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
B=9c3560e; H=65e856f; O=work/permap/m1r-$H; CS=tools/fidelity/capacity-stress.ps1; LOG=tools/fidelity/results/PERFORMANCE_LOG.md
eval "$(sed -n '/^mem()/p' work/batch_regress.sh)"
[ -f work/PERF_HOLD ] || echo "experimental PERF RUN M1 re-verdict $H $(date +%H:%M)" > work/PERF_HOLD
foreign() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.Name -like 'wfc_rebuild*' -or \$_.Name -like 'hd*' -or (\$_.Name -eq 'python.exe' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') } | % { '{0} {1} {2}' -f \$_.ProcessId, \$_.Name, \$_.ExecutablePath }"; }
waitclear() { while true; do f=$(foreign); [ -z "$f" ] && break; echo "$(date +%H:%M:%S) waiting on: $(echo $f | tr '\n' ' ')"; sleep 15; done; }
waitclear; echo "window open $(date +%H:%M:%S)"; mem start
export WFC_GATE_IGNORE_HOLD=1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/regress-8map.ps1 -Base work/ab/m9c_$B -Head work/ab/m9c_$H -OutDir $O-regress -Batching > $O-regress.out 2>&1
tail -11 $O-regress.out; echo "regress done $(date +%H:%M)"; mem after-regress
row() {  # row <map> <ov|rp> <rep> <seconds> [nolog]
  local d=$O-$1-$2-2160-t$4-r$3 M="" PL="-PerfLog '$LOG'"
  [ -n "$5" ] && PL=""
  if [ $2 = ov ]; then M="-FixedCam -CamByMap @{ '$1' = '$(cat work/permap/cams-$1/pick.txt)' }"; else M="-PlayerBot 1"; fi
  for try in 1 2; do
    waitclear
    powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$CS' -Root 'work/ab/m9c_$H' -OutDir '$d' -Maps $1 -Pops t64 -Resolutions 1920x1080 -TimeLimit $4 $M -NoSplit -RenderSize 3840x2160 -ExtraEnv 'WFC_SLOWFRAME=3.33' $PL -Note 'M1 re-verdict $H map $1 $2 4K ${4}s rep $3'" > $d.out 2>&1
    local c=$(ls $d/*/CONTAMINATED.txt 2>/dev/null)
    echo "$(date +%H:%M) $1 $2 2160 ${4}s r$3 try $try: p90 $(powershell -NoProfile -Command "(Import-Csv '$d/capacity.csv' | ? { \$_.match -eq '2' } | Select-Object -First 1).p90_ms") $c"
    [ -z "$c" ] && return 0
    [ $try = 2 ] && { echo "ABORT: $d contaminated twice"; return 1; }
    mv $d $d-contam1; mv $d.out $d-contam1.out
  done
}
abort() { mem abort; unset WFC_GATE_IGNORE_HOLD; rm -f work/PERF_HOLD; echo "window ABORTED $(date +%H:%M)"; exit 1; }
row 510 rp 0 150 nolog || abort    # warm-up, discarded (r0)
for r in 1 2 3; do row 510 rp $r 300 || abort; done
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/pooled-p90.ps1 -Dirs "$O-510-rp-2160-t300-r1,$O-510-rp-2160-t300-r2,$O-510-rp-2160-t300-r3" | tee $O-510-rp-pooled.txt
mem end
for p in $(powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe' } | % { \$_.ProcessId }"); do echo "leftover $p"; done
unset WFC_GATE_IGNORE_HOLD
rm -f work/PERF_HOLD; echo "window done $(date +%H:%M)"
