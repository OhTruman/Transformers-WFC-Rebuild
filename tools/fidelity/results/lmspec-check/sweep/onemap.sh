#!/bin/bash
# One map of the after-fix sweep: named run -> sharemap -> shared run (single process at a time); refuses while a PERF_HOLD exists.
cd "F:/Transformers Rebuild/Rebuild-Experimental/work/lmspec/geom" || exit 1
PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"; export PYTHONDONTWRITEBYTECODE=1; unset LMS_SUB LMS_DUMP LMS_OWNER
M=$1
ls "/f/Transformers Rebuild"/*/work/PERF_HOLD >/dev/null 2>&1 && { echo "$M HOLD - not run"; exit 2; }
[ -f scmap_$M.json ] || "$PY" scmap.py $M > scmap_$M.out 2>&1
unset LMS_SHARE; "$PY" lmspec_geom.py $M > named_$M.out 2>&1; cp lmspecg_$M.json lmspecg_${M}_named.json
"$PY" sharemap.py $M > share_$M.out 2>&1
LMS_SHARE=1 "$PY" lmspec_geom.py $M > shared_$M.out 2>&1; cp lmspecg_$M.json lmspecg_${M}_sharedfinal.json
echo "$M $(date +%H:%M:%S) $(tail -1 shared_$M.out | cut -c1-160)"
