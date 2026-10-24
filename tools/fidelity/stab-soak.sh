#!/bin/bash
# Stability soak (tools/fidelity/broad-pass.ps1 uses it): headless, every map x mode x 300 s, 4 v 5 Smart; per run exit / end / ERROR / WARN -> <out>/runs.csv
# Stability soak (Integration 2026-10-23): headless, every versus map x TDM / CTF / KOTH (Power Struggle) / DOM (Conquest) x 300 s,
# 4 v 5 Smart bots, seeds 7 + 1234; per run: exit code, end, wall, ERROR / WARN counts, assert / crash lines -> <out>/runs.csv
#   bash work/stabsoak/run.sh <exe> <outdir> "<seeds>" "<modes>"
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
EXE=${1:?exe}; O=${2:?out}; SEEDS=${3:-"7 1234"}; MODES=${4:-"TDM CTF KOTH DOM"}; mkdir -p $O
# SOAK_ASSETS: WFC_ASSETS for every run (default the DLC dev root: the DLC maps world.glb live only there; 2026-10-24)
MAPS=${SOAK_MAPS:-"MP_IAC_Rust MP_IAC_Streets MP_IAC_Berth MP_IAC_Seed MP_KON_Molten MP_ORB_Debris MP_UND_Complex MP_UND_Gorge MP_ESC_Remnant MP_ESC_BrokenHope"}
# DLC maps (docs/DLC_PLAN.md) join the default list once their render data exists (SOAK_MAPS set = exactly those maps)
[ -z "$SOAK_MAPS" ] && for nm in MP_ORB_Havoc MP_KON_Fortress MP_ESC_Tranquillity MP_ESC_District MP_IAC_Horizon MP_IAC_Metropolis MP_KON_Sector MP_ESC_Pulse MP_ESC_Static; do
  [ -d "${SOAK_RENDER_ROOT:-F:/Transformers Rebuild/Rebuild/work/render}/$nm" ] && MAPS="$MAPS $nm"; done
W=$(cygpath -w "$EXE")
echo "seed,map,mode,rc,wall_s,end,errors,warns,asserts,last_step" > $O/runs.csv
for s in $SEEDS; do for md in $MODES; do for m in $MAPS; do
  d=$O/s$s-$md-$m; mkdir -p $d; t0=$(date +%s)
  ( cd $d && exec env WFC_ASSETS="${SOAK_ASSETS:-F:/Transformers Rebuild/Rebuild/work/assets_dlc/VerticalSlice}" WFC_MUTE=1 WFC_NOINTERACT=1 WFC_AUTODETECT=0 WFC_BOOT=match WFC_NULLRENDER=1 WFC_LOCKSTEP=1 WFC_SEED=$s WFC_FLOWSEED=$s \
      WFC_MATCH_SECONDS=300 WFC_MAP=$m WFC_MATCH_URL="$m?GameModeTag=$md?BotsFriendly=4?BotsEnemy=5" "$EXE" > run.log 2>&1 ) &
  pid=$!; t=0
  while kill -0 $pid 2>/dev/null; do sleep 1; t=$((t+1))
    if grep -qa "only for direct-boot" $d/run.log 2>/dev/null || [ $t -ge 240 ]; then
      powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='wfc_rebuild.exe'\" | ? { \$_.ExecutablePath -eq '$W' } | % { Stop-Process -Id \$_.ProcessId -Force }"
      echo "KILLED $s $md $m at ${t}s" >> $O/killed.txt; break; fi
  done
  wait $pid; rc=$?; L=$d/run.log
  end=other; grep -qa "of match time reached" $L && end=time; grep -qaE "match: (ended|over)|MatchOver|match over|state -> (WaitingPostMatch|MatchOver)" $L && [ $end = other ] && end=match_end
  grep -qa "only for direct-boot" $L && end=refused; [ -f $O/killed.txt ] && grep -q "$s $md $m " $O/killed.txt && end=KILLED_${t}s
  ls $d/*.dmp >/dev/null 2>&1 && end="$end+dump"
  echo "$s,$m,$md,$rc,$(( $(date +%s) - t0 )),$end,$(grep -ac '^\[error' $L),$(grep -ac '^\[warn' $L),$(grep -aciE 'assert|access violation|exception|crash' $L),$(grep -aoE 'HEADLESS done: [0-9]+' $L | grep -oE '[0-9]+$')" >> $O/runs.csv
done; done; done
echo "stability soak done $(date +%T): $(($(wc -l < $O/runs.csv) - 1)) runs"
