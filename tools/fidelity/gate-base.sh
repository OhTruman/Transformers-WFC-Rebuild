#!/bin/bash
# GATE BASE (package 6, Integration 2026-10-16): the package-gate regress base = package 5 398b27ef + ONLY the integration commits
# that change the Classic simulation (yaw 022e5e34, drive probe, ride height, ...), so base / final / lights-off play the same match.
#   bash tools/fidelity/gate-base.sh <name> <sim commit | patch file> [...]      e.g. gate-base.sh yaw_probe 022e5e34 <probe sha>
#   (a patch FILE = a minimal sim-only diff against 398b27ef, e.g. tools/fidelity/gatebase/steer_3de18d73_vs_398b27ef.patch)
# Steps: m05 build-target 398b27ef (Release only, third_party) into work/ab/fast_398b27e_<name>; each commit's src/ diff applied
# in order with patch -p1 (git apply is a no-op in archive trees); every patched file byte-checked against the LAST listed commit
# that touched it (git show <sha>:<path>); Release rebuilt; link-render (render data, NOINTERACT / RT shaders). Then verify with
# VERIFY=<final head tree> (default: none): a 2-arm lockstep on Streets, steps 600-9600, RT off, shared GPU (untimed) - the base
# must read "same match as 'base' True" for the head. Use the result as GATE_BASE=work/ab/fast_398b27e_<name>,
# GATE_BASE_NAME="398b27ef + <commits>".
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
N=${1:?name}; shift; [ $# -ge 1 ] || { echo "list the sim commits"; exit 1; }
T=work/ab/fast_398b27e_$N; L=work/gatebase_$N.log; : > $L
TC="$PWD/.toolchain"; CM="$TC/cmake-4.4.3-windows-x86_64/bin/cmake.exe"
st() { echo "$(date +%T) $*" | tee -a $L; }
[ -e $T ] && { st "$T exists - remove it first (rmdir /s /q)"; exit 1; }
st "build 398b27ef -> $T"
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/m05/build-target.ps1 -Ref 398b27ef -Name fast_398b27e_$N -Jobs 4 -NoDebug -ThirdParty work/ab/_third_party >> $L 2>&1 || { st "build-target failed"; exit 1; }
declare -A last
for c in "$@"; do
  if [ -f "$c" ]; then   # a minimal sim-only diff against 398b27ef (when a commit's other hunks need non-sim history): applied, not byte-checked;
    (cd $T && patch -p1 --forward < "$OLDPWD/$c") >> $L 2>&1 || { st "patch $c failed (see $L)"; exit 1; }   # the VERIFY same-match row proves it
    pf=$(grep '^+++ ' "$c" | sed -E 's#^[+]{3} b/([^[:space:]]+).*#\1#' | tr '\n' ' ')
    for f in $pf; do unset "last[$f]"; done   # (those files are no longer a commit's copy)
    st "applied file $c: $pf(verified by VERIFY, not byte-checked)"; continue
  fi
  git cat-file -e $c^{commit} || { st "unknown commit $c"; exit 1; }
  git show $c -- src > work/gatebase_$N.$c.diff
  (cd $T && patch -p1 --forward < ../../gatebase_$N.$c.diff) >> $L 2>&1 || { st "patch $c failed (see $L)"; exit 1; }
  for f in $(git show --name-only --format= $c -- src); do last[$f]=$c; done
  st "applied $c: $(git show --name-only --format= $c -- src | tr '\n' ' ')"
done
bad=0; for f in "${!last[@]}"; do git show ${last[$f]}:$f | cmp -s - $T/$f && st "  same as ${last[$f]}: $f" || { st "  DIFFERS from ${last[$f]}: $f"; bad=1; }; done
[ $bad = 0 ] || { st "byte-check failed: a listed commit depends on others - ask Integration for a diff against 398b27ef"; exit 1; }
PATH="$TC/llvm-mingw-20260922-ucrt-x86_64/bin:$PATH" "$CM" --build $T/build-release --parallel 4 >> $L 2>&1 || { st "rebuild failed"; exit 1; }
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target $T | tee -a $L
if [ -n "$VERIFY" ]; then
  st "verify vs $VERIFY on Streets (steps 600-9600, RT off, shared GPU, untimed)"
  WFC_GATE_GPU_POLICY=shared WFC_GATE_GPU_MAX_OTHERS=1 WFC_GATE_GPU_GRACE_MIN=0 powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir work/gatebase_${N}_verify -Map 508 -Arms "base|$T|;;head|$VERIFY|" -Steps 9600 -From 600 -RenderSize 1280x720 -PlayerBot 1 -Reps 1 -Seed 1234 > work/gatebase_${N}_verify.out 2>&1
  grep -o "head: steps [0-9]* ([0-9-]*), same match as 'base' [A-Za-z]*" work/gatebase_${N}_verify.out | tee -a $L
fi
st "done: GATE_BASE=$T GATE_BASE_NAME=\"398b27ef + $*\""
