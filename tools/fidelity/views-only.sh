#!/bin/bash
# Views-only eye pass (no match frames): the authored views (tools/fidelity/views/<map>.txt) and the sun-aimed lens-flare views
# (tools/fidelity/views_flare/<map>.txt, warmed, report-only) for a few maps; sheets + contrast rows into work/eyepass/<name>/EYEPASS.md.
#   bash tools/fidelity/views-only.sh <name> <exe> "<maps>" ["off real realhd"]
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
N=${1:?name}; exe=${2:?exe}; MAPS=${3:?maps}; MODES=${4:-"off real realhd"}
O=work/eyepass/$N; mkdir -p $O; PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
export WFC_MUTE=1 WFC_NOINTERACT=1 PYTHONDONTWRITEBYTECODE=1
mine() { powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='wfc_rebuild.exe'\" | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*' } | % { \$_.ProcessId }"; }
csv() { echo $1 | tr ' ' ','; }
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/eyepass-views.ps1 -Exe "$exe" -OutDir $O/views -Maps $(csv "$MAPS") -Modes $(csv "$MODES") -Size 2560x1440
for p in $(mine); do powershell -NoProfile -Command "Stop-Process -Id $p -Force"; echo "reaped $p"; done
"$PY" tools/fidelity/eyepass_views_sheet.py $O/views "$MAPS" "$MODES" > /dev/null
echo "# Views-only eye pass - $N ($exe)" > $O/EYEPASS.md; sed -n '/## Authored views/,$p' $O/views/EYEPASS.md >> $O/EYEPASS.md
FM=""; for m in $MAPS; do [ -f tools/fidelity/views_flare/$m.txt ] && FM="$FM $m"; done
if [ -n "$FM" ]; then
  mkdir -p $O/flare_list; for m in $FM; do awk 'NF>=2 { print $1"_w1 "$2; print $1"_w2 "$2; print }' tools/fidelity/views_flare/$m.txt > $O/flare_list/$m.txt; done
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/eyepass-views.ps1 -Exe "$exe" -OutDir $O/flareviews -ViewsDir "$(cygpath -w $O/flare_list)" -Maps $(csv "$FM") -Modes $(csv "$MODES") -Size 2560x1440
  for p in $(mine); do powershell -NoProfile -Command "Stop-Process -Id $p -Force"; echo "reaped $p"; done
  "$PY" tools/fidelity/eyepass_views_sheet.py $O/flareviews "$FM" "$MODES" "$(cygpath -w $O/flare_list)" "Lens flare views (sun-aimed; report-only)" > /dev/null
  sed -n '/## Lens flare views/,$p' $O/flareviews/EYEPASS.md >> $O/EYEPASS.md
fi
echo "views-only done $(date +%T): $O"
