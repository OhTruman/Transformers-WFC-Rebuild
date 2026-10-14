#!/bin/bash
# Untimed lockstep exact identity, integration 0502051 (base) vs 4ef2208 (head, M5 review fixes), Integration request
# 2026-10-12. WINDOWED only (profile 1920x1080 Fullscreen=0; display degraded - no fullscreen / mode changes), default
# settings, frontend flow -> TDM on <map> (the lobby string asks 31 v 32 with ExtendedPlayers, which the lobby ignores - its key is
# BotsExtended - so the roster clamps to 5 a side: 2026-10-14 note; every base / head pair ran the same), PLAYERBOT, seed 7 / flowseed 1, LOCKSTEP + LOCKSTEP_EXACT, 4K render size
# in the window, 25 captures (steps 60..1500 every 60) + per-step SIMHASH. One exe at a time; stops under 8 GB free commit
# or on any PERF_HOLD. Exes reaped by path after every run.
# usage: [RTMODE=1] bash tools/fidelity/identity-capture.sh "504" 0531934 <headname>   (identity of a head vs a base, HD off, windowed;
#        RTMODE 0 Off (default) / 1 Faithful / 2 Realistic - RT-on pairs: compare SIMHASH first, shotsdiff labels SIM-DIVERGED)
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
BASE=${2:-0531934}; HEAD=${3:?head build name (work/ab/m9c_<name>)}; MAPS=${1:-"504"}; RT=${RTMODE:-0}   # RTMODE=0|1|2: the profile's Ray Tracing row (Off / Faithful / Realistic; frontend boots apply the profile)
O=work/sweep/ident_${BASE}_vs_${HEAD}$([ "$RT" != 0 ] && echo "_rt$RT"); mkdir -p $O
PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"; SP="C:/Users/coxes/AppData/Local/Temp/claude/F--Transformers-Rebuild-Rebuild-Experimental/80e2e0b2-04f6-4df6-991a-088cc81d6a34/scratchpad"
freegb() { powershell -NoProfile -Command "[int]((Get-CimInstance Win32_OperatingSystem).FreeVirtualMemory/1MB)"; }
mine() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental\work\ab\m9c_*' } | % { \$_.ProcessId }"; }
gate() {
  for h in "F:/Transformers Rebuild/"*/work/PERF_HOLD; do [ -e "$h" ] || continue; case "$h" in *Rebuild-Experimental*) [ -n "$WFC_GATE_IGNORE_HOLD" ] && continue;; esac; echo "PERF_HOLD $h: stopping"; exit 3; done
  local f=$(freegb | tr -d '\r'); [ "$f" -lt 8 ] && { echo "free commit ${f} GB < 8: stopping"; exit 4; }
  echo "free commit ${f} GB"
}
run() {   # run <sha> <map> <tag>
  local sha=$1 map=$2 d=$O/$2-$3; local exe="$PWD/work/ab/m9c_$1/build-release/bin/wfc_rebuild.exe"
  [ -f $d/wfc.log ] && [ $(ls $d/shots 2>/dev/null | wc -l) -ge 25 ] && { echo "$d done"; return; }
  gate; rm -rf $d; mkdir -p $d/shots
  cp work/sweep/ident_profile.ini $d/wfc_profile.ini
  if grep -q "^RayTracing=" $d/wfc_profile.ini; then sed -i "s/^RayTracing=.*/RayTracing=$RT/" $d/wfc_profile.ini; else echo "RayTracing=$RT" >> $d/wfc_profile.ini; fi
  ( cd $d && env WFC_BOOT=frontend WFC_BOTAI=Classic WFC_HDTEXTURES=0 WFC_GPULOST_DIALOG_S=0 WFC_HANG_EXIT_S=60 WFC_MUTE=1 WFC_SKIPINTRO=1 WFC_NOMOUSE=1 \
      WFC_FLOWSEED=1 WFC_SEED=7 WFC_AUTODETECT=0 WFC_LOGEVERY=0 WFC_LOGTIME=1 WFC_LOCKSTEP=1 WFC_LOCKSTEP_EXACT=1 WFC_SIMHASH=0 \
      WFC_RENDERSIZE=3840x2160 WFC_PLAYERBOT=1 WFC_SHOTMATCH="$(cygpath -w "$PWD")\\shots,60,1500,60" \
      WFC_LOBBY_OPTIONS="ExtendedPlayers=1;BotsAutobot=31;BotsDecepticon=32;PointsToWin=9999;TimeLimit=300" \
      WFC_FRONTEND_SCRIPT="wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;call:Online.SetSelectedMapID,$map;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=40;quit" \
      timeout 180 "$exe" > run.out 2>&1 )
  for p in $(mine); do powershell -NoProfile -Command "Stop-Process -Id $p -Force"; echo "reaped $p"; done
  grep 'SIMHASH' $d/wfc.log | sed -E 's/.*(SIMHASH [0-9]+ [0-9a-f]+).*/\1/' > $d/simhash.txt
  echo "$(date +%H:%M:%S) $(basename $d): $(ls $d/shots | wc -l) shots, $(wc -l < $d/simhash.txt) simhash, size $(grep -o 'window [0-9]*x[0-9]*' $d/wfc.log | head -1)"
}
for m in $MAPS; do
  run $BASE $m base
  run $HEAD $m head
  b=$O/$m-base; h=$O/$m-head
  # compare over the common step range (runs quit on wall time, so the last step differs)
  sh=$(awk 'NR==FNR { a[$2]=$3; next } ($2 in a) { n++; if (a[$2] != $3) { d++; if (!f) f=$2 } } END { printf "map simhash: %d common steps, %d differ, first differing step %s\n", n, d+0, f ? f : "none" }' $b/simhash.txt $h/simhash.txt)
  echo "$sh"
  # (Rendering 2026-10-13: renderer output feeds the sim - per-owner render times are read by gameplay / bots - so an RT-on pair can
  #  diverge in SIMULATION; captures from the first differing step on are labelled SIM-DIVERGED by shotsdiff, not counted as render diffs)
  fd=$(echo "$sh" | sed -nE 's/.*first differing step ([0-9]+).*/\1/p')
  echo "map $m: simhash lines base $(wc -l < $b/simhash.txt) head $(wc -l < $h/simhash.txt)"
  PYTHONDONTWRITEBYTECODE=1 "$PY" tools/fidelity/shotsdiff.py $b $h $fd
done
for p in $(mine); do echo "leftover $p"; done
echo "ident done $(date +%H:%M:%S)"
