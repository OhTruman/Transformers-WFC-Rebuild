#!/bin/bash
# (B) candidate overlay: names = every entry that differs between the fresh channel-honouring build (matB/out) and the
# 07:40 snapshot (matA/src), plus new keys; merged by matfix_handoff (paths normalised to the integration render root,
# where tex/ exists) into matB/handoff, then copied into the full candidate tree matB/render.
set -e
cd "/f/Transformers Rebuild/Rebuild-Systems/work"
P="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
MAPS="MP_IAC_Streets MP_ESC_BrokenHope MP_ESC_Remnant MP_IAC_Berth MP_IAC_Rust MP_IAC_Seed MP_KON_Molten MP_ORB_Debris MP_UND_Complex MP_UND_Gorge"
NAMES=$("$P" -I -c "
import json, sys
W = 'F:/Transformers Rebuild/Rebuild-Systems/work/'
names = set()
for m in '$MAPS'.split():
    a = json.load(open(W + 'matA/src/%s/materials_glsl.json' % m, encoding='utf-8')); b = json.load(open(W + 'matB/out/%s/materials_glsl.json' % m, encoding='utf-8'))
    def n(o, r): return json.dumps(o, sort_keys=True).replace(r, 'ROOT/')
    for k in b:
        if k not in a or n(a[k], 'F:/Transformers Rebuild/Rebuild/work/render/%s/' % m) != n(b[k], W + 'matB/out/%s/' % m).replace('matB/out/%s/' % m, 'ROOT/'):
            names.add(k)
print(','.join(sorted(names)))
")
echo "names: $(echo "$NAMES" | tr ',' '\n' | wc -l)"
rm -rf matB/handoff
MATFIX_BASE="F:/Transformers Rebuild/Rebuild-Systems/work/matA/src" FRESH_PREFIX=matB/out "$P" -I ../tools/systems/matfix_handoff.py "F:/Transformers Rebuild/Rebuild-Systems/work/matB/out" matB/handoff "$NAMES" $MAPS
for m in $MAPS; do cp matB/handoff/$m/materials_glsl.json matB/render/$m/materials_glsl.json; done
echo "OVERLAY IN"
