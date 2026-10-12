#!/bin/bash
# UNTIMED, indicative (Integration 2026-10-11): free-running 8-map overview pair 39185f5 (b749a83 merged) vs 9be437d (+ 5eeb3f7 pawnRayHit
# sim change). A sim change rules out lockstep, so: capacity-stress FixedCam overview (per-map pick cam, no autojump since cf6d68b), 64p TDM,
# 4K RenderSize in a 1080 window, 120 s, interleaved 39185f5 -> 9be437d per map. Yields to any lane's PERF_HOLD before each row
# (no WFC_GATE_IGNORE_HOLD: Wait-WfcGpu also waits). Rows are not timing verdicts.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
O=work/permap/ovpair-39185f5; CS=tools/fidelity/capacity-stress.ps1; mkdir -p $O
held() { ls "F:/Transformers Rebuild"/*/work/PERF_HOLD >/dev/null 2>&1; }
foreign() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.Name -like 'wfc_rebuild*' -or \$_.Name -like 'hd*' -or (\$_.Name -eq 'python.exe' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') } | % { '{0} {1}' -f \$_.ProcessId, \$_.ExecutablePath }"; }
row() {  # row <sha> <map>
  local d=$O/$1-$2
  if [ -f $d/capacity.csv ]; then ls $d/*/CONTAMINATED.txt >/dev/null 2>&1 || return 0; mv $d $O/_discarded/$1-$2-contam-$(date +%H%M); mv $d.out $O/_discarded/$1-$2-contam-$(date +%H%M).out; fi
  # work/PAUSE = stop before the next row (a GPU window announced by message, not by PERF_HOLD; 2026-10-11 17:37 a killed row let the loop start the next)
  [ -f work/PAUSE ] && { echo "$(date +%H:%M:%S) PAUSE - stop before $1-$2"; exit 0; }
  # untimed, but a row next to another lane's exe / hd job is CONTAMINATED and useless for the pair: wait for no hold AND zero foreign jobs
  while true; do
    [ -f work/PAUSE ] && { echo "$(date +%H:%M:%S) PAUSE - stop before $1-$2"; exit 0; }
    if held; then echo "$(date +%H:%M:%S) hold present - waiting before $1-$2"; sleep 60; continue; fi
    f=$(foreign); [ -z "$f" ] && break
    echo "$(date +%H:%M:%S) waiting on: $(echo $f | tr '\n' ' ')"; sleep 30
  done
  powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$CS' -Root 'work/ab/m9c_$1' -OutDir '$d' -Maps $2 -Pops t64 -Resolutions 1920x1080 -TimeLimit 120 -FixedCam -CamByMap @{ '$2' = '$(cat work/permap/cams-$2/pick.txt)' } -NoSplit -RenderSize 3840x2160 -Note 'ovpair untimed $1 map $2'" > $d.out 2>&1
  echo "$(date +%H:%M) $1 $2: p90 $(powershell -NoProfile -Command "(Import-Csv '$d/capacity.csv' | ? { \$_.match -eq '2' } | Select-Object -First 1).p90_ms") $(ls $d/*/CONTAMINATED.txt 2>/dev/null)"
}
for m in 501 502 503 504 507 508 509 510; do row 39185f5 $m; row 9be437d $m; done
for p in $(powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe' } | % { \$_.ProcessId }"); do echo "leftover $p"; done
echo "ovpair done $(date +%H:%M)"
