#!/bin/bash
# V_bake: trace + fit per MP map, Gorge first, 8 threads; one line per step in work/vbake/run_all.log
# (writes only under work/vbake/<map>; ExtractedAssets / render data read-only)
W="${VBAKE_OUT:-F:/Transformers Rebuild/Rebuild-Systems/work/vbake}"
PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
FIT="F:/Transformers Rebuild/Rebuild-Systems/tools/systems/vbake/vbake_fit.py"
LOG="$W/run_all.log"
MAPS="MP_UND_Gorge MP_ESC_BrokenHope MP_ESC_Remnant MP_IAC_Berth MP_IAC_Rust MP_IAC_Seed MP_IAC_Streets MP_KON_Molten MP_ORB_Debris MP_UND_Complex"
[ -n "$1" ] && MAPS="$*"
for m in $MAPS; do
  R="F:/Transformers Rebuild/Rebuild/work/render/$m"
  WG="F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/$m/world.glb"
  O="$W/$m"
  mkdir -p "$O"
  t0=$(date +%s)
  echo "$(date +%H:%M:%S) $m trace start" >> "$LOG"
  "F:/Transformers Rebuild/Rebuild-Systems/work/vbake/vbake_trace.exe" --world "$WG" --bsp "$R/bsp.glb" --render "$R" --out "$O" --threads 8 > "$O/trace.log" 2>&1
  rc=$?
  t1=$(date +%s)
  echo "$(date +%H:%M:%S) $m trace rc=$rc wall=$((t1-t0))s" >> "$LOG"
  if [ $rc -eq 0 ]; then
    "$PY" "$FIT" --trace "$O" --render "$R" > "$O/fit.log" 2>&1
    rc2=$?
    t2=$(date +%s)
    echo "$(date +%H:%M:%S) $m fit rc=$rc2 wall=$((t2-t1))s total=$((t2-t0))s" >> "$LOG"
  fi
done
echo "$(date +%H:%M:%S) ALL DONE" >> "$LOG"
