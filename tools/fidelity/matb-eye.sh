#!/bin/bash
# MATERIAL EYE PASS (Systems' material translator fix (B), Integration 2026-10-17): the same exe (af4d0731) on the same seeded
# lockstep match frames with two render-data roots - before = integration Rebuild/work/render, after = Systems' candidate
# Rebuild-Systems/work/matB/render - in Off and Faithful (RT 1) [+ Realistic with MODES], 8 versus maps (BrokenHope / Remnant are
# Escalation-only), plus the authored views on the 5 maps that have them. Then matb_sheet.py: before / after / difference crops
# per map, with the expected-change materials per map from Systems' matscan. One windowed exe at a time; untimed (card slot).
#   bash tools/fidelity/matb-eye.sh [name matB]      (env: MAPS, MODES "off faithful", AFTER_ROOT, BEFORE_ROOT, EXE)
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
N=${1:-matB}; MAPS=${MAPS:-"501 502 503 504 507 508 509 510"}; MODES=${MODES:-"off faithful real"}   # (Realistic included: Integration 2026-10-17, the user plays it)
EXE=${EXE:-"$PWD/work/ab/fast_af4d073/build-release/bin/wfc_rebuild.exe"}
BEFORE_ROOT=${BEFORE_ROOT:-"F:/Transformers Rebuild/Rebuild/work/render"}; [ -n "$AFTER_ROOT" ] && AFTER_ROOT_SET=1
MATS="F:/Transformers Rebuild/Rebuild-Systems/docs/handoff/matscan/scan_classes.json"; PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
O=work/matb/$N; mkdir -p $O; L=$O/matb.log
st() { echo "$(date +%T) $*" | tee -a $L; }
# the after arm = integration's CURRENT render data + only the (B) entries (matb_root.ps1), built at the start so both arms share every
# other change of the day ((A), the particle / map_fx regen); AFTER_ROOT=<dir> overrides
# the before arm is the same snapshot without (B) (frozen together: integration data changing mid-pass cannot split the arms)
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/matb_root.ps1 -Name ${N}_base -NoCandidate ${MAPFX_FROM:+-MapFxFrom "$(cygpath -w "$MAPFX_FROM")"} -Src "$(cygpath -w "$BEFORE_ROOT")" >> $L 2>&1 || { st "matb_root (before) failed"; exit 1; }
BEFORE_SRC=$BEFORE_ROOT; BEFORE_ROOT="$PWD/work/matb/${N}_base_root"
if [ -z "$AFTER_ROOT_SET" ]; then powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/matb_root.ps1 -Name $N ${MAPFX_FROM:+-MapFxFrom "$(cygpath -w "$MAPFX_FROM")"} -Src "$(cygpath -w "$BEFORE_SRC")" >> $L 2>&1 || { st "matb_root failed"; exit 1; }; AFTER_ROOT="$PWD/work/matb/${N}_root"; fi
for r in "$BEFORE_ROOT" "$AFTER_ROOT"; do [ -d "$r" ] || { st "no render root $r"; exit 1; }; done
st "MATERIAL EYE PASS $N: exe $EXE; before $BEFORE_ROOT; after $AFTER_ROOT; maps $MAPS; modes $MODES"
# (views: eyepass.sh runs them only for maps with tools/fidelity/views/<map>.txt)
EYE_EXE="$EXE" EYE_RENDER_DATA="$BEFORE_ROOT" bash tools/fidelity/eyepass.sh ${N}_before "$MAPS" "$MODES" > $O/before.out 2>&1; st "before arm done ($(grep -c 'shots,' $O/before.out) runs)"
EYE_EXE="$EXE" EYE_RENDER_DATA="$AFTER_ROOT" bash tools/fidelity/eyepass.sh ${N}_after "$MAPS" "$MODES" > $O/after.out 2>&1; st "after arm done ($(grep -c 'shots,' $O/after.out) runs)"
grep -hE "INVALID|UNKNOWN" $O/before.out $O/after.out | tee -a $L
PYTHONDONTWRITEBYTECODE=1 "$PY" -I tools/fidelity/matb_sheet.py work/eyepass/${N}_before work/eyepass/${N}_after $O "$MAPS" "$MODES" --materials "$MATS" --top 4 >> $L 2>&1
# the authored views, before vs after (same cameras)
if [ -d work/eyepass/${N}_before/views ]; then
  PYTHONDONTWRITEBYTECODE=1 "$PY" -I tools/fidelity/matb_sheet.py work/eyepass/${N}_before/views work/eyepass/${N}_after/views $O/views "$MAPS" "$MODES" --sub . --materials "$MATS" --top 5 >> $L 2>&1 || st "views sheet failed"
fi
st "MATERIAL EYE PASS $N DONE: $O/MATB.md, $O/sheet_<map>.png"
