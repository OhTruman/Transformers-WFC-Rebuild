#!/bin/bash
# Shared-map lightmap PS sweep over all maps (untimed; waits while any lane's PERF_HOLD exists).
cd "F:/Transformers Rebuild/Rebuild-Experimental/work/lmspec/geom" || exit 1
PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"; export PYTHONDONTWRITEBYTECODE=1
hold() { while ls "/f/Transformers Rebuild"/*/work/PERF_HOLD >/dev/null 2>&1; do sleep 20; done; }
for M in MP_IAC_Rust MP_IAC_Streets MP_UND_Gorge MP_ORB_Debris MP_IAC_Berth MP_IAC_Seed MP_UND_Complex MP_ESC_BrokenHope MP_ESC_Remnant MP_KON_Molten; do
  [ -f "lmspecg_${M}_sharedfinal.json" ] && continue
  hold; "$PY" scmap.py $M > scmap_$M.out 2>&1 || { echo "$M scmap failed"; continue; }
  hold; unset LMS_SHARE; "$PY" lmspec_geom.py $M > named_$M.out 2>&1; cp lmspecg_$M.json lmspecg_${M}_named.json
  hold; "$PY" sharemap.py $M > share_$M.out 2>&1
  hold; LMS_SHARE=1 "$PY" lmspec_geom.py $M > shared_$M.out 2>&1; cp lmspecg_$M.json lmspecg_${M}_sharedfinal.json
  echo "$M $(date +%H:%M:%S) $(tail -1 shared_$M.out | cut -c1-200)"
done
echo "sweep done $(date +%H:%M:%S)"
