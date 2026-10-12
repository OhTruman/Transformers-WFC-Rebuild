#!/bin/bash
# Untimed lockstep exact identity, integration 0502051 (base) vs 4ef2208 (head, M5 review fixes), Integration request
# 2026-10-12. WINDOWED only (profile 1920x1080 Fullscreen=0; display degraded - no fullscreen / mode changes), default
# settings, frontend flow -> 64p TDM on <map>, PLAYERBOT, seed 7 / flowseed 1, LOCKSTEP + LOCKSTEP_EXACT, 4K render size
# in the window, 25 captures (steps 60..1500 every 60) + per-step SIMHASH. One exe at a time; stops under 8 GB free commit
# or on any PERF_HOLD. Exes reaped by path after every run.
# usage: bash work/m5id/ident.sh "510 504 508"
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
MAPS=${1:-"510 504 508"}; O=work/m5id/runs; mkdir -p $O
PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"; SP="C:/Users/coxes/AppData/Local/Temp/claude/F--Transformers-Rebuild-Rebuild-Experimental/80e2e0b2-04f6-4df6-991a-088cc81d6a34/scratchpad"
freegb() { powershell -NoProfile -Command "[int]((Get-CimInstance Win32_OperatingSystem).FreeVirtualMemory/1MB)"; }
mine() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental\work\ab\m9c_0502051*' -or \$_.ExecutablePath -like '*Rebuild-Experimental\work\ab\m9c_4ef2208*' } | % { \$_.ProcessId }"; }
gate() {
  ls "F:/Transformers Rebuild/"*/work/PERF_HOLD 2>/dev/null && { echo "PERF_HOLD: stopping"; exit 3; }
  local f=$(freegb | tr -d '\r'); [ "$f" -lt 8 ] && { echo "free commit ${f} GB < 8: stopping"; exit 4; }
  echo "free commit ${f} GB"
}
run() {   # run <sha> <map> <tag>
  local sha=$1 map=$2 d=$O/$2-$3; local exe="$PWD/work/ab/m9c_$1/build-release/bin/wfc_rebuild.exe"
  [ -f $d/wfc.log ] && [ $(ls $d/shots 2>/dev/null | wc -l) -ge 25 ] && { echo "$d done"; return; }
  gate; rm -rf $d; mkdir -p $d/shots
  cp "F:/Transformers Rebuild/Rebuild-Rendering/work/lockh/seed_profile.ini" $d/wfc_profile.ini
  ( cd $d && env WFC_RENDER_DATA="F:/Transformers Rebuild/Rebuild/work/render" WFC_BOOT=frontend WFC_SKIPINTRO=1 WFC_NOMOUSE=1 \
      WFC_FLOWSEED=1 WFC_SEED=7 WFC_AUTODETECT=0 WFC_LOGEVERY=0 WFC_LOGTIME=1 WFC_LOCKSTEP=1 WFC_LOCKSTEP_EXACT=1 WFC_SIMHASH=0 \
      WFC_RENDERSIZE=3840x2160 WFC_PLAYERBOT=1 WFC_SHOTMATCH="$(cygpath -w "$PWD")\\shots,60,1500,60" \
      WFC_LOBBY_OPTIONS="ExtendedPlayers=1;BotsAutobot=31;BotsDecepticon=32;PointsToWin=9999;TimeLimit=300" \
      WFC_FRONTEND_SCRIPT="wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;call:Online.SetSelectedMapID,$map;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=40;quit" \
      timeout 180 "$exe" > run.out 2>&1 )
  for p in $(mine); do powershell -NoProfile -Command "Stop-Process -Id $p -Force"; echo "reaped $p"; done
  grep 'SIMHASH' $d/wfc.log | sed -E 's/.*(SIMHASH [0-9]+ [0-9a-f]+).*/\1/' > $d/simhash.txt
  echo "$(date +%H:%M:%S) $(basename $d): $(ls $d/shots | wc -l) shots, $(wc -l < $d/simhash.txt) simhash, size $(grep -o 'window [0-9]*x[0-9]*' $d/wfc.log | head -1)"
}
# A/A on Gorge: a second run of each build (is the 1-shot 6 px / max 1 difference same-build noise?)
run 0502051 510 base2
run 4ef2208 510 head2
for p in "base base2" "head head2" "base2 head2" "base head2" "head base2"; do set -- $p
  awk 'NR==FNR { a[$2]=$3; next } ($2 in a) { n++; if (a[$2] != $3) d++ } END { printf "simhash %d common, %d differ  ", n, d+0 }' $O/510-$1/simhash.txt $O/510-$2/simhash.txt
  PYTHONDONTWRITEBYTECODE=1 "$PY" "$SP/shotsdiff.py" $O/510-$1 $O/510-$2
done
for p in $(mine); do echo "leftover $p"; done
echo "aa done $(date +%H:%M:%S)"
