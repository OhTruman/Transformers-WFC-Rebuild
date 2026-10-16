#!/bin/bash
# EYE PASS (package 6, user 2026-10-16: "Realistic must look night-and-day vs the original"; Integration). The SAME frames of one
# seeded lockstep match per map in each mode, side by side: Off | Realistic | Realistic + Enhanced materials (HD 2). Frontend flow
# -> private TDM, 31 v 32 (BotsExtended) + PLAYERBOT, seed 7 / flowseed 1, LOCKSTEP + LOCKSTEP_EXACT, NOINTERACT + muted,
# window and render size WxH (default 2560x1440, the display's native size: no mode change), one exe at a time. Captures every
# 60 steps from 300 to 1800 (WFC_SHOTMATCH: the same match moment in every mode; SIMHASH per step proves the arms played the same
# match - RT can move the sim, then frames after the split are labelled). eyepass_sheets.py then composes per map:
#   sheet_<map>.png      every captured step, a row per step: Off | Realistic | Realistic+HD, labelled
#   pick_<map>.png       4 auto-picked frames (most muzzle flash / most lit detail / two evenly spaced) at full size, labelled
# Authored views (a street under buildings, a reflective surface) can be added later per map as WFC_SHOTLIST camera files.
# RT-on runs need a slot-A window from Integration. Output: work/eyepass/<build>/.
#   bash tools/fidelity/eyepass.sh <build, e.g. 398b27e> [maps "508 510 507 504 509"] [modes "off real realhd"]
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
B=${1:?build name (work/ab/m9c_<name>)}; MAPS=${2:-"508 510 507 504 509"}; MODES=${3:-"off real realhd"}
W=${EYE_W:-2560}; H=${EYE_H:-1440}; HDROOT=${EYE_HDROOT:-"F:/Transformers Rebuild/AssetTools/out/hd/push_full_snap_20261016_b0-31/pack"}
O=work/eyepass/$B; mkdir -p $O; PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
exe="$PWD/work/ab/m9c_$B/build-release/bin/wfc_rebuild.exe"; [ -f "$exe" ] || { echo "no exe $exe"; exit 1; }
grep -q WFC_NOINTERACT "$exe" || { echo "exe lacks WFC_NOINTERACT: patch it first"; exit 1; }
mine() { powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='wfc_rebuild.exe'\" | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*' } | % { \$_.ProcessId }"; }
for map in $MAPS; do for mode in $MODES; do
  d=$O/$map-$mode; [ -f $d/wfc.log ] && [ $(ls $d/shots 2>/dev/null | wc -l) -ge 26 ] && { echo "$d done"; continue; }
  rm -rf $d; mkdir -p $d/shots; cp work/sweep/ident_profile.ini $d/wfc_profile.ini
  case $mode in off) RT=0; HD=0;; real) RT=2; HD=0;; realhd) RT=2; HD=2;; *) echo "unknown mode $mode"; continue;; esac
  sed -i "s/^Width=.*/Width=$W/; s/^Height=.*/Height=$H/; s/^RayTracing=.*/RayTracing=$RT/; s/^HDTextures=.*/HDTextures=$HD/" $d/wfc_profile.ini
  HDENV=("WFC_HDTEXTURES=$HD"); [ "$HD" != 0 ] && HDENV+=("WFC_HD_ROOT=$(cygpath -w "$HDROOT")")   # (an array: the root has a space)
  echo "$(date +%T) $map $mode (RT $RT, HD $HD)"
  ( cd $d && env WFC_BOOT=frontend WFC_BOTAI=Classic "${HDENV[@]}" WFC_GPULOST_DIALOG_S=0 WFC_HANG_EXIT_S=60 WFC_MUTE=1 WFC_NOINTERACT=1 \
      WFC_SKIPINTRO=1 WFC_NOMOUSE=1 WFC_FLOWSEED=1 WFC_SEED=7 WFC_AUTODETECT=0 WFC_LOGEVERY=0 WFC_LOGTIME=1 WFC_LOCKSTEP=1 \
      WFC_LOCKSTEP_EXACT=1 WFC_SIMHASH=0 WFC_RENDERSIZE=${W}x${H} WFC_PLAYERBOT=1 WFC_SHOTMATCH="$(cygpath -w "$PWD")\\shots,300,1800,60" \
      WFC_LOBBY_OPTIONS="BotsExtended=1;BotsAutobot=31;BotsDecepticon=32;PointsToWin=9999;TimeLimit=600" \
      WFC_FRONTEND_SCRIPT="wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;call:Online.SetSelectedMapID,$map;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=90;quit" \
      timeout 900 "$exe" > run.out 2>&1 )
  for p in $(mine); do powershell -NoProfile -Command "Stop-Process -Id $p -Force"; echo "reaped $p"; done
  grep -E 'SIMHASH [0-9]+ [0-9a-f]{16}' $d/wfc.log | sed -E 's/.*(SIMHASH [0-9]+ [0-9a-f]+).*/\1/' > $d/simhash.txt
  # (2026-10-16) the window must be the requested client size, else the pixels of this run are not comparable
  if grep -q "NOINTERACT: client .* - DIFFERS from the request" $d/wfc.log; then echo "INVALID: $(basename $d) client rect differs from the request: $(grep -m1 -o 'NOINTERACT: client [^-]*' $d/wfc.log)"; touch $d/INVALID_CLIENT_RECT; fi
  echo "  $(ls $d/shots | wc -l) shots, $(wc -l < $d/simhash.txt) simhash; $(grep -o 'renderer: Ray Tracing [A-Za-z]*' $d/wfc.log | tail -1); $(grep -m1 -o 'renderer: HD textures [a-z]*' $d/wfc.log); $(grep -m1 -oE 'HD texture pack available: [0-9]+ textures' $d/wfc.log); $(grep -oE 'NOINTERACT: client [0-9]+x[0-9]+[^)]*\)?( - DIFFERS from the request)?' $d/wfc.log | tail -1) (last = after the mode change)"
done; done
PYTHONDONTWRITEBYTECODE=1 "$PY" tools/fidelity/eyepass_sheets.py $O "$MAPS" "$MODES"
echo "eyepass done $(date +%T): $O"
