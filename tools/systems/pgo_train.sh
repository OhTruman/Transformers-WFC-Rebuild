#!/bin/bash
# PGO profile regeneration (Release CPU, 300 fps lane). Reproducible: same seed, same scenarios.
#   1) configure + build an instrumented exe:  cmake -S . -B build/pgo-gen -DCMAKE_BUILD_TYPE=Release -DWFC_PGO=gen ...
#   2) run this script with BUILD=build/pgo-gen (from the repo root): it plays the training scenarios, then merges
#      the raw profiles into tools/pgo/wfc.profdata
#   3) build the optimised exe with -DWFC_PGO=${repo}/tools/pgo/wfc.profdata
# Scenarios (2026-10-09, Integration: 300+ on every map): every MP map with render data at 32 v 32 (64 participants), one
# 10 v 10 match (small-match paths), TDM live match only (high PointsToWin, WFC_MATCH_SECONDS), and the frontend flow
# (boot -> menus -> lobby -> match). MAPS / SECS override. Re-run whenever hot code changes a lot (stale profiles only lose the
# gain, never correctness). Recorded run: BUILD=build/pgo-gen SECS=60 bash tools/systems/pgo_train.sh (09c 5e51386).
set -u
BUILD=${BUILD:-build/pgo-gen}
EXE="$BUILD/bin/wfc_rebuild.exe"
PROFDATA=${LLVM_PROFDATA:-llvm-profdata}
RAW="$BUILD/pgo-raw"
SECS=${SECS:-60}
MAPS=${MAPS:-"MP_IAC_Streets MP_IAC_Rust MP_IAC_Seed MP_IAC_Berth MP_KON_Molten MP_ORB_Debris MP_UND_Complex MP_UND_Gorge MP_ESC_BrokenHope MP_ESC_Remnant"}
[ -x "$EXE" ] || { echo "no instrumented exe at $EXE"; exit 1; }
rm -rf "$RAW"; mkdir -p "$RAW"
export LLVM_PROFILE_FILE="$RAW/wfc-%p.profraw"
export WFC_SEED=7 WFC_FLOWSEED=7 WFC_FRAMELIMIT=0 WFC_NOMOUSE=1   # FLOWSEED: GameFlow RNG (team pick, rotation) for the frontend run
run_match() {   # map, bots url
    echo "PGO training: $1 $2"
    WFC_BOOT=match WFC_MATCH_SECONDS=$SECS WFC_SMOKE_FRAMES=2000000 \
        WFC_MATCH_URL="$1?GameModeTag=TDM?$2?ExtendedPlayers=1?PointsToWin=1000?BotDifficulty=1" \
        timeout 900 "$EXE" > "$RAW/$1_${2//[?=]/_}.log" 2>&1
}
run_match MP_IAC_Streets "BotsFriendly=9?BotsEnemy=10"
for map in $MAPS; do
    run_match "$map" "BotsFriendly=31?BotsEnemy=32"
done
echo "PGO training: frontend flow"
WFC_SKIPINTRO=1 WFC_FRONTEND_AUTOPLAY="TDM,508" WFC_SMOKE_FRAMES=20000 timeout 900 "$EXE" > "$RAW/frontend.log" 2>&1
mkdir -p tools/pgo
"$PROFDATA" merge -o tools/pgo/wfc.profdata "$RAW"/*.profraw && ls -la tools/pgo/wfc.profdata
