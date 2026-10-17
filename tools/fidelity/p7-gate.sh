#!/bin/bash
# PACKAGE 7 COMBINED GATE (tools/fidelity/PACKAGE7-GATE.md): one start once Integration names the head.
#   phase pre (CPU + untimed card, no hold):
#     1 build the head (build-target -j 4, link-render) and the regress base (gate-base.sh BASE_REF=af4d0731 + P7_BASE_ITEMS)
#     2 headless same-match: base vs head (Streets lockstep 600-9600, seed 1234, PlayerBot, 31 v 32, WFC_NULLRENDER) + the negative
#       control (the hover-only base vs head must differ) - stop if the base does not match
#     3 the head's eye pass (eyepass.sh: Off / Realistic / Realistic + HD 2, 5 user maps + views) - card, untimed
#   phase gate (the timed holds, after Integration's "GO" and the acks): package-respin.sh <head> af4d0731 with GATE_BASE, RERUN_1B=1,
#     EYE_FROM=p7_<head>; the render snapshot, CONTAMINATED reruns and the pool guard are inside package-respin.sh
#   bash tools/fidelity/p7-gate.sh <head sha> pre|gate     (env: P7_BASE_ITEMS = the base patch list, GATE_REGRESS_NOTE)
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
H=${1:?head sha}; PH=${2:?pre or gate}; h=${H:0:7}
O=work/p7gate/$h; mkdir -p $O; L=$O/p7gate.log
st() { echo "$(date +%T) $*" | tee -a $L; }
export WFC_MUTE=1 WFC_NOINTERACT=1 PYTHONDONTWRITEBYTECODE=1 WFC_NORECOIL=1   # (NORECOIL: see package-respin.sh; the head must carry the switch)
ITEMS=${P7_BASE_ITEMS:-"tools/fidelity/gatebase/hover_cfca1717_srcgame.patch tools/fidelity/gatebase/headless_runner_be54be6d_af4d.patch tools/fidelity/gatebase/TRANSACT_sim_only_af4d.patch"}
BN=p7_$h; BASE=work/ab/fast_af4d073_$BN
if [ "$PH" = pre ]; then
  st "P7 GATE pre: head $H; base items: $ITEMS"
  [ -d work/ab/fast_$h/build-release/bin ] || { powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/m05/build-target.ps1 -Ref $H -Name fast_$h -Jobs 4 -NoDebug -ThirdParty work/ab/_third_party > $O/build_head.log 2>&1; powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target work/ab/fast_$h -Alias m9c_$h >> $O/build_head.log 2>&1; }
  [ -f work/ab/fast_$h/build-release/bin/wfc_rebuild.exe ] || { st "head build failed (see $O/build_head.log)"; exit 1; }
  grep -q WFC_NORECOIL work/ab/fast_$h/build-release/bin/wfc_rebuild.exe || { st "STOP: the head exe lacks WFC_NORECOIL"; exit 2; }
  [ -d $BASE/build-release/bin ] || BASE_REF=af4d0731 bash tools/fidelity/gate-base.sh $BN $ITEMS > $O/build_base.out 2>&1
  [ -f $BASE/build-release/bin/wfc_rebuild.exe ] || { st "base build failed (see work/gatebase_$BN.log)"; exit 1; }
  st "builds done; headless same-match + negative control"
  E="WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_POLICY=shared WFC_GATE_GPU_MAX_OTHERS=9 WFC_GATE_GPU_GRACE_MIN=0 WFC_NO_WATCHDOG=1"
  env $E powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/samematch -Map 508 -Arms "base|$BASE|WFC_NULLRENDER=1;;head|work/ab/fast_$h|WFC_NULLRENDER=1;;neg|work/ab/fast_af4d073_hover_hl2|WFC_NULLRENDER=1" -Steps 9600 -From 600 -RenderSize 1280x720 -Width 1280 -Height 720 -PlayerBot 1 -Reps 1 -Seed 1234 > $O/samematch.out 2>&1
  sm=$(grep -oE "lockstep\.head\.r1: head: steps [0-9]+ \([0-9-]+\), same match as 'base' [A-Za-z]+" $O/samematch.out); ng=$(grep -oE "lockstep\.neg\.r1: neg: steps [0-9]+ \([0-9-]+\), same match as 'base' [A-Za-z]+" $O/samematch.out)
  st "same match: $sm"; st "negative control: $ng"
  case "$sm" in *True) ;; *) st "STOP: the base does not play the head's match - not gating"; exit 2;; esac
  case "$ng" in *False) ;; *) st "WARNING: the negative control matches too (no sim change since the hover base?)";; esac
  st "eye pass on the head (card, untimed)"
  EYE_EXE="$PWD/work/ab/fast_$h/build-release/bin/wfc_rebuild.exe" bash tools/fidelity/eyepass.sh p7_$h > $O/eyepass.out 2>&1
  grep -E "contrast floor|INVALID|UNKNOWN" work/eyepass/p7_$h/EYEPASS.md $O/eyepass.out | tee -a $L
  st "P7 GATE pre DONE: base $BASE, eye pass work/eyepass/p7_$h - ready for the hold"
elif [ "$PH" = gate ]; then
  [ -f $BASE/build-release/bin/wfc_rebuild.exe ] || { st "run 'pre' first"; exit 1; }
  st "P7 GATE timed phase: head $H vs base $BASE"
  GATE_BASE=$BASE GATE_BASE_NAME="af4d0731 + $(echo $ITEMS | sed 's#tools/fidelity/gatebase/##g')" RERUN_1B=1 EYE_FROM=p7_$h SKIP_BUILD=1 \
    bash tools/fidelity/package-respin.sh $H af4d0731 > $O/respin.out 2>&1
  st "P7 GATE DONE: work/package-gate/$h/PACKAGE-GATE.md"
fi
