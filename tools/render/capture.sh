#!/usr/bin/env bash
# Standardized rendering captures (headless smoke runs) for before/after comparison.
# Usage: bash tools/render/capture.sh <outdir> [extra env assignments...]
# Produces <outdir>/<scenario>.png + <scenario>.log and contact sheets, plus frame-time samples.
set -u
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:?outdir}"; shift
extra=("$@")
exe="$root/build/bin/wfc_rebuild.exe"
py="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
mkdir -p "$out"

run() {   # name frames env...
  local name=$1 frames=$2; shift 2
  env "${extra[@]}" "$@" WFC_SMOKE_FRAMES="$frames" WFC_SHOT="$out/$name.bmp" timeout 600 "$exe" > "$out/$name.log" 2>&1
  echo "$name exit=$? errors=$(grep -c '\[error\]' "$out/$name.log")"
}

run robot          90
run fineaim        90  WFC_FINEAIM_ON=20
run vehicle_idle   90  WFC_STARTVEHICLE=1
run vehicle_boost 120  WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1
run vehicle_dash   68  WFC_STARTVEHICLE=1 WFC_AUTODASH=60
run vehicle_nitro 100  WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 WFC_AUTODASH=60
run transform_mid  90  WFC_AUTOTRANSFORM=30
for s in 2 6 10 14 18 22; do run streets_$s 60 WFC_SPAWN_INDEX=$s; done
run fire_perf     360  WFC_AUTOFIRE=1 WFC_RENDERSTATS=1
run idle_perf     360  WFC_RENDERSTATS=1

"$py" - "$out" <<'PYEOF'
import sys, os
from PIL import Image
out = sys.argv[1]
def sheet(names, fn, cols=2):
    ims = [Image.open(os.path.join(out, n + '.bmp')).convert('RGB').resize((640, 360)) for n in names
           if os.path.exists(os.path.join(out, n + '.bmp'))]
    if not ims: return
    rows = (len(ims) + cols - 1) // cols
    W = Image.new('RGB', (640 * cols, 360 * rows))
    for k, im in enumerate(ims): W.paste(im, ((k % cols) * 640, (k // cols) * 360))
    W.save(os.path.join(out, fn))
for f in os.listdir(out):
    if f.endswith('.bmp'): Image.open(os.path.join(out, f)).save(os.path.join(out, f[:-4] + '.png'))
sheet(['robot', 'fineaim', 'vehicle_idle', 'vehicle_boost', 'vehicle_dash', 'vehicle_nitro', 'transform_mid'], 'sheet_character.png')
sheet(['streets_%d' % s for s in (2, 6, 10, 14, 18, 22)], 'sheet_streets.png')
PYEOF
for n in fire_perf idle_perf; do echo "$n: $(grep -h 'avg frame' "$out/$n.log" | tail -2 | tr '\n' ' ')"; done
