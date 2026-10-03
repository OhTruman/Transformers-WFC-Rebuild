#!/usr/bin/env bash
# Standardized render-audit captures (deterministic: WFC_LOCKSTEP) with a per-capture frame report
# (materials drawn, blend/lighting/lightmap state, distortion, dynamic light environments).
# Usage: bash tools/render/capture_audit.sh <outdir>
set -u
root="$(cd "$(dirname "$0")/../.." && pwd)"
exe="$root/build/bin/wfc_rebuild.exe"
out="${1:-$root/work/render_audit}"
py="$root/../AssetTools/bin/py/python.exe"
mkdir -p "$out"
side="$(cat "$root/work/m3/sidecam.txt" 2>/dev/null || echo "")"

# name | frames | env | effect templates | provisional notes
scen=(
"robot|90||-|Robot light environment: 6 authored visibility samples (bounds sampling semantics HIGH); character shadows not implemented"
"vehicle|90|WFC_STARTVEHICLE=1|FX_Navigation_p.CarHover_A_01_FX x6 (HoverBooster_* sockets)|base_glow/rays_Dup hover emitters not spawned (Systems); character shadows not implemented"
"hover_idle|90|WFC_STARTVEHICLE=1 WFC_RENDERCAM=$side|FX_Navigation_p.CarHover_A_01_FX|base_glow (Glow_Mod_MAT) and rays_Dup (Trail_Distort_MAT) not spawned (Systems); distortion RT format UNORM8 (HIGH)"
"boost|120|WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1|FX_Navigation_p.bumble_boost_small1_FX (BoostSocket_L/R)|particle colour encoding clamped by Systems (hdr())"
"hover_dash|68|WFC_STARTVEHICLE=1 WFC_AUTODASH=60|CarHover_A_01_FX + jump/dash FX (Systems)|-"
"ram_nitro|100|WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 WFC_AUTODASH=60|FX_Navigation_p.FX.Truck_ram_FX (RamSocket)|Ram_model_MAT rim term literal (Normal node semantics PROV); dust/rays_Dup not spawned (Systems)"
"transform_mid|80|WFC_AUTOTRANSFORM=30|-|-"
"streets_dark|60|WFC_SPAWN_INDEX=10|-|360 BSP elements without static lighting use the dynamic env (PROV)"
"streets_bright|60|WFC_SPAWN_INDEX=14|-|-"
"firing|90|WFC_AUTOFIRE=1|Ion Blaster weapon FX (Systems, GL1 textured path: no material passed yet)|weapon sprite FX not material-shaded until Systems passes ParticleBatch::material"
"fineaim|90|WFC_FINEAIM_ON=20|-|HUD scale mode / easeout curve PROV; no scope for the Ion Blaster (HUD script CONF)"
"streets_floor|60|WFC_SPAWN_INDEX=2|-|-"
"transform_r2v_end|420|WFC_AUTOTRANSFORM=30|-|robot -> vehicle complete (separate vehicle LightEnvironment)"
"transform_v2r_end|420|WFC_STARTVEHICLE=1 WFC_AUTOTRANSFORM=30|-|vehicle -> robot complete"
"walk|240|WFC_AUTOWALK=1 WFC_AUTOTURN=0.6|-|moving through light-volume cells (full updates every 90 UU)"
)
idx="$out/INDEX.md"
{
  echo "# Render audit captures"
  echo
  echo "Deterministic (WFC_LOCKSTEP). Marks per FIDELITY.md: CONFIRMED / HIGH / PROV / UNKNOWN."
  echo
} > "$idx"
for s in "${scen[@]}"; do
  IFS='|' read -r name frames envs fx notes <<< "$s"
  rm -f "$out/$name.bmp" "$out/$name.txt"
  env WFC_LOCKSTEP=1 ${AUDIT_ENV:-} WFC_SMOKE_FRAMES="$frames" WFC_SHOT="$out/$name.bmp" WFC_FRAMEREPORT="$out/$name.txt" $envs \
      timeout 600 "$exe" > "$out/$name.log" 2>&1
  echo "$name exit=$? errors=$(grep -c '\[error\]' "$out/$name.log")"
  "$py" -c "from PIL import Image; Image.open(r'$out/$name.bmp').save(r'$out/$name.png')" 2>/dev/null && rm -f "$out/$name.bmp"
  {
    echo "## $name"
    echo
    echo "![]($name.png)"
    echo
    echo "- env: \`${envs:-default spawn}\`, frames $frames"
    echo "- effect templates: $fx"
    echo "- provisional / open: $notes"
    echo
    echo '```'
    cat "$out/$name.txt" 2>/dev/null || echo "(no frame report)"
    echo '```'
    echo
  } >> "$idx"
done
echo "index -> $idx"
