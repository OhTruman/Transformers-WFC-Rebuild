#!/bin/bash
# Cull-distance sidecars for every campaign unit (300 fps 0a; RENDERING_CULL_DIST, tools/render/build_cull_distance.py).
# Per chapter <CH> under the campaign render root, each chapter.json sublevel {dir, level}:
#   --world <root>/<CH>/<dir>/world.glb  --prefix <level> (the unit's package, e.g. D1_ORB_Assmebly1_ART_m)  --out <root>/<CH>/<dir>
# The renderer reads <unit dir>/cull_distance.json at each unit load (Pipeline::loadSublevel); build_slim ships the chapters whole.
# Base authored.db (the campaign is base game). 398 units, ~5-30 s each (one sqlite lookup per node).
# usage: cull_sidecar_campaign.sh [chapter...]   (default: all 10)
set -u
PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
TOOL="F:/Transformers Rebuild/Rebuild/tools/render/build_cull_distance.py"
ROOT="${WFC_CAMPAIGN_ROOT:-F:/Transformers Rebuild/AssetTools/out/campaign_render}"
DB="F:/Transformers Rebuild/AssetTools/manifests/authored.db"
CHS="${*:-A1_IAC A2_KON A3_COR A4_ORB A5_TRY D1_ORB D2_COR D3A_IAC D3B_IAC D5_OME}"
fail=0
for CH in $CHS; do
    "$PY" -I -c "
import json, sys
for s in json.load(open(sys.argv[1], encoding='utf-8'))['sublevels']: print(s['dir'] + ' ' + s['level'])
" "$ROOT/$CH/chapter.json" | tr -d '\r' | while read -r DIR LEVEL; do
        W="$ROOT/$CH/$DIR/world.glb"
        if [ ! -f "$W" ]; then echo "SKIP $CH/$DIR (no world.glb)"; continue; fi
        "$PY" -I "$TOOL" --world "$W" --prefix "$LEVEL" --out "$ROOT/$CH/$DIR" --db "$DB" || { echo "FAIL $CH/$DIR"; fail=1; }
    done
done
exit $fail
