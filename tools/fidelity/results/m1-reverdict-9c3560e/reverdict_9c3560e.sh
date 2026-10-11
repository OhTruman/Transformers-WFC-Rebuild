#!/bin/bash
# M1 RE-VERDICT 9c3560e (Integration 2026-10-11, TIMED slot granted; Gorge 48/48): head 9c3560e = base 9be437d + Rendering d8c6720
# (skin palette batching + near-face occlusion boxes, byte-identical).
#  (1) regress-8map 9be437d -> 9c3560e (lockstep, BLOCK > +0.15)
#  (2) Gorge real play 4K, 300 s timed matches, 3 reps, each <= 3.33, + pooled p90 (tools/fidelity/pooled-p90.ps1)
#  (3) check: Gorge overview 4K + Rust real play 4K, 2 reps, 150 s (overview: no autojump since cf6d68b - not comparable with older ov rows)
# 4K = RenderSize 3840x2160 in a 1920x1080 window (as the previous verdicts). Clears the hold at the end.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
B=9be437d; H=9c3560e; O=work/permap/m1r-$H; CS=tools/fidelity/capacity-stress.ps1; LOG=tools/fidelity/results/PERFORMANCE_LOG.md
eval "$(sed -n '/^mem()/p' work/batch_regress.sh)"
[ -f work/PERF_HOLD ] || echo "experimental PERF RUN M1 re-verdict $H $(date +%H:%M)" > work/PERF_HOLD
foreign() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.Name -like 'wfc_rebuild*' -or \$_.Name -like 'hd*' -or (\$_.Name -eq 'python.exe' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') } | % { '{0} {1} {2}' -f \$_.ProcessId, \$_.Name, \$_.ExecutablePath }"; }
while true; do f=$(foreign); [ -z "$f" ] && break; echo "$(date +%H:%M:%S) waiting on: $f"; sleep 15; done
echo "window open $(date +%H:%M:%S)"; mem start
export WFC_GATE_IGNORE_HOLD=1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/regress-8map.ps1 -Base work/ab/m9c_$B -Head work/ab/m9c_$H -OutDir $O-regress > $O-regress.out 2>&1
tail -11 $O-regress.out; echo "regress done $(date +%H:%M)"; mem after-regress
row() {  # row <map> <ov|rp> <rep> <seconds>
  local d=$O-$1-$2-2160-t$4-r$3 M=""
  if [ $2 = ov ]; then M="-FixedCam -CamByMap @{ '$1' = '$(cat work/permap/cams-$1/pick.txt)' }"; else M="-PlayerBot 1"; fi
  powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$CS' -Root 'work/ab/m9c_$H' -OutDir '$d' -Maps $1 -Pops t64 -Resolutions 1920x1080 -TimeLimit $4 $M -NoSplit -RenderSize 3840x2160 -ExtraEnv 'WFC_SLOWFRAME=3.33' -PerfLog '$LOG' -Note 'M1 re-verdict $H map $1 $2 4K ${4}s rep $3'" > $d.out 2>&1
  echo "$(date +%H:%M) $1 $2 2160 ${4}s r$3: p90 $(powershell -NoProfile -Command "(Import-Csv '$d/capacity.csv' | ? { \$_.match -eq '2' } | Select-Object -First 1).p90_ms") $(ls $d/*/CONTAMINATED.txt 2>/dev/null)"
}
for r in 1 2 3; do row 510 rp $r 300; done
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/pooled-p90.ps1 -Dirs "$O-510-rp-2160-t300-r1,$O-510-rp-2160-t300-r2,$O-510-rp-2160-t300-r3" | tee $O-510-rp-pooled.txt
mem after-gorge-rp
for r in 1 2; do row 510 ov $r 150; row 504 rp $r 150; done
mem end
for p in $(powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe' } | % { \$_.ProcessId }"); do echo "leftover $p"; done
unset WFC_GATE_IGNORE_HOLD
rm -f work/PERF_HOLD; echo "window done $(date +%H:%M)"
