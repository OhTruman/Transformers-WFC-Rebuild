#!/usr/bin/env bash
# Renderer performance suite (deterministic WFC_LOCKSTEP; WFC_RENDERSTATS averages over 120 frames).
# Usage: bash tools/render/perf_suite.sh <exe> [frames]
# Per scenario (last 120-frame window): frame time, scene submit, gpu wait, draws, DirectLightEnv
# updates, LightsVisibilitiesVolume queries + their time, character shadow rays, dynamic vertex build.
set -u
exe="${1:?exe}"
frames="${2:-370}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
export WFC_RENDER_DATA="${WFC_RENDER_DATA:-$root/work/render}"
scen=(
"idle|"
"moving|WFC_AUTOWALK=1 WFC_AUTOTURN=0.6"
"firing|WFC_AUTOFIRE=1"
"transform|WFC_AUTOTRANSFORM=30"
"vehicle|WFC_STARTVEHICLE=1"
"veh_move|WFC_STARTVEHICLE=1 WFC_AUTOWALK=1 WFC_AUTOTURN=0.4"
"hover_fx|WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1"
)
num() { grep -oE "$1" <<< "$2" | grep -oE '[0-9]+(\.[0-9]+)?' | head -1; }
printf "%-10s %8s %7s %6s %7s %8s %7s %9s %6s %8s\n" scenario frame_ms submit gpu draws dle_upd lvv_q lvv_ms rays vbuild_ms
for s in "${scen[@]}"; do
  IFS='|' read -r name envs <<< "$s"
  log="$(env WFC_LOCKSTEP=1 WFC_RENDERSTATS=1 WFC_SMOKE_FRAMES="$frames" $envs timeout 900 "$exe" 2>&1)"
  f="$(grep 'avg frame' <<< "$log" | tail -1)"
  dy="$(grep 'dynamic meshes' <<< "$log" | tail -1)"
  dl="$(grep 'DirectLightEnv per frame' <<< "$log" | tail -1)"
  printf "%-10s %8s %7s %6s %7s %8s %7s %9s %6s %8s\n" "$name" \
    "$(num 'avg frame [0-9.]+' "$f")" "$(num 'scene submit [0-9.]+' "$f")" "$(num 'gpu wait [0-9.]+' "$f")" \
    "$(num '[0-9.]+ draws' "$f")" "$(num '[0-9.]+ updates' "$dl")" "$(num '[0-9.]+ volume queries' "$dl")" \
    "$(num '\([0-9.]+ ms\)' "$dl")" "$(num '[0-9.]+ shadow rays' "$dl")" "$(num 'vertex build [0-9.]+' "$dy")"
done
