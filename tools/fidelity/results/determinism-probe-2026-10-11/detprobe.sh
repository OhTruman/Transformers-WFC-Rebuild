#!/bin/bash
# UNTIMED probe (Integration 2026-10-11: run-to-run spread of Gorge rp 4K): is a free-running frontend match on fixed seeds
# (sim RNG fixed per match, WFC_FLOWSEED=1) the same match at two frame rates? Per-step WFC_SIMHASH, 60 s matches, 64p Gorge.
#   A = capacity-stress overview inputs (local player auto-walk / strafe / jump every 150 RENDERED frames)
#   B = the same with the local player's auto input removed (idle human)
#   P = real play (WFC_PLAYERBOT=1, per-frame player bot)
# each at RenderSize 1280x720 and 3840x2160 (different frame rates). Not timing: rows are never used as perf numbers.
# Stops before each run if any lane's PERF_HOLD exists.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
H=5aa2d22; O=work/detprobe; CS=tools/fidelity/capacity-stress.ps1; mkdir -p $O
held() { ls "F:/Transformers Rebuild"/*/work/PERF_HOLD >/dev/null 2>&1; }
run() {  # run <tag> <rendersize> <extra env> [player bot]
  local d=$O/$1-$2; [ -d $d ] && return 0
  if held; then echo "$(date +%H:%M:%S) hold present - stop before $1-$2"; return 1; fi
  local pb="-PlayerBot -1"; [ -n "$4" ] && pb="-PlayerBot 1"
  powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$CS' -Root 'work/ab/m9c_$H' -OutDir '$d' -Maps 510 -Pops t64 -Resolutions 1280x720 -TimeLimit 60 $pb -NoSplit -RenderSize $2 -ExtraEnv 'WFC_SIMHASH=0;$3'" > $d.out 2>&1
  echo "$(date +%H:%M:%S) $1-$2 done: $(grep -c 'SIMHASH' $d/*/wfc.log 2>/dev/null) hash lines $(ls $d/*/CONTAMINATED.txt 2>/dev/null)"
}
ok=1
for t in "A|" "B|WFC_AUTOWALK=;WFC_AUTOSTRAFE=;WFC_AUTOJUMP_EVERY=" "P||pb"; do
  IFS='|' read -r tag env pb <<< "$t"
  for rs in 1280x720 3840x2160; do run $tag $rs "$env" $pb || { ok=0; break 2; }; done
done
for p in $(powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe' } | % { \$_.ProcessId }"); do echo "leftover $p"; done
echo "probe done $(date +%H:%M) ok=$ok"
