#!/bin/bash
# D3D12 PRESENTER FIX VERIFICATION (package 7, Integration 2026-10-17): the 1-in-32 exit hang under WFC_D3D12PRESENT (af4d0731,
# arm B run 32: a kernel-stuck exe after 'smoke test complete') must be gone on Rendering's fixed exe.
#   1 loop     N (64) same-match lockstep runs, Streets 2560x1440, RT off, WFC_D3D12PRESENT=1: 0 hangs; the presenter's
#              'latency wait' max per run vs the af4d0731 arm B runs (work/leak/armB_d3d12present)
#   2 FG       one run with WFC_FRAMEGEN=1 (frame generation presents through D3D12)
#   3 resize   one run whose window is resized 3 times while it plays (SetWindowPos, SWP_NOACTIVATE - no focus taken)
#   4 mapswitch map-loss-gate (Streets -> lobby -> Berth -> lobby -> Streets) with WFC_D3D12PRESENT=1
# A run whose exe outlives HANG_S (240 s) is a HANG: the series stops there (a kernel-stuck exe cannot be killed - reboot), and
# the exe's minidump path is printed. One windowed exe at a time; WFC_MUTE=1 WFC_NOINTERACT=1.
#   bash tools/fidelity/presenter-verify.sh <name> <fixed exe> [N 64]       (env: SKIP_LOOP / SKIP_FG / SKIP_RESIZE / SKIP_MAPSWITCH)
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
N=${1:?name}; E=${2:?fixed exe}; K=${3:-64}; HANG_S=${HANG_S:-240}
O=work/presenter/$N; mkdir -p $O; L=$O/verify.log
export WFC_MUTE=1 WFC_NOINTERACT=1 WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_GRACE_MIN=0 PYTHONDONTWRITEBYTECODE=1
# (the start gate: other lanes' headless exes (null render, no window) may run in untimed slots - share with at most one)
export WFC_GATE_GPU_POLICY=shared WFC_GATE_GPU_MAX_OTHERS=1
RD="F:/Transformers Rebuild/Rebuild/work/render"
st() { echo "$(date +%T) $*" | tee -a $L; }
[ -f "$E" ] || { st "no exe $E"; exit 1; }
for s in WFC_D3D12PRESENT WFC_NOINTERACT; do grep -q "$s" "$E" || { st "exe lacks $s"; exit 1; }; done
T=work/ab/pv_$N; B=$(dirname "$(dirname "$E")")
if [ ! -e $T/build-release ]; then mkdir -p $T; cmd //c mklink //J "$(cygpath -w $T/build-release)" "$(cygpath -w "$B")" >/dev/null; powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target $T -Alias m9c_pv_$N >> $L 2>&1; fi
EW=$(cygpath -w "$PWD/$T/build-release/bin/wfc_rebuild.exe")
# one lockstep run with a hang watch: <dir> <env> [resize]
one() {
  local d=$1 env=$2 rs=$3
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $d -Map 508 -Arms "arm|$T|$env" -Steps 9600 -From 600 -Width 2560 -Height 1440 -RenderSize 2560x1440 -PlayerBot 1 -Reps 1 -Seed 1234 > $d.out 2>&1 &
  local lp=$! t0=$(date +%s) pid="" resized=0 ts=""   # (the hang clock starts when the exe appears, not at launch)
  while kill -0 $lp 2>/dev/null; do
    sleep 5
    if [ -z "$pid" ]; then
      pid=$(powershell -NoProfile -Command "@(Get-CimInstance Win32_Process -Filter \"Name='wfc_rebuild.exe'\" | ? { \$_.ExecutablePath -eq '$EW' } | % { \$_.ProcessId })[0]" | tr -d '\r')
      [ -n "$pid" ] && ts=$(date +%s)
      # (no exe yet: lockstep-ab's start gate is waiting for the card - not a hang; give up after 15 min)
      [ -z "$pid" ] && [ $(( $(date +%s) - t0 )) -ge 900 ] && { st "GATE: $(basename $d) never started an exe in 15 min (card busy?)"; return 3; }
      continue
    fi
    if [ -n "$rs" ] && [ $resized = 0 ] && [ $(( $(date +%s) - ts )) -ge 40 ]; then
      powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lib/resize-window.ps1 -ProcessId $pid >> $L 2>&1; resized=1
    fi
    if [ $(( $(date +%s) - ts )) -ge $HANG_S ] && powershell -NoProfile -Command "exit [int](-not (Get-Process -Id $pid -ErrorAction SilentlyContinue))"; then
      st "HANG: $(basename $d) exe pid $pid alive ${HANG_S} s after it started; dump: $(ls $d/*/wfc_hang_*.dmp 2>/dev/null | head -1)"; return 2
    fi
  done
  local lw; lw=$(grep -ohE "latency wait [0-9.]+ / [0-9.]+" $d/*/wfc.log 2>/dev/null | awk '{ if ($5 > m) m = $5 } END { printf "%.2f", m }')
  echo "$(basename $d) $(grep -oE '(PASS|UNKNOWN|FAIL) lockstep\.arm\.r1' $d.out | head -1) present=$(grep -m1 -oE 'PRESENT: [A-Za-z0-9]+' $d/*/wfc.log 2>/dev/null | head -1 | cut -d' ' -f2) latencywait_max=$lw smoke=$(grep -c 'smoke test complete' $d/*/wfc.log 2>/dev/null) watchdog=$(grep -c 'watchdog:' $d/*/wfc.log 2>/dev/null) escape=$(grep -c 'raised from the CPU' $d/*/wfc.log 2>/dev/null) nodrain=$(grep -c 'did not drain in 2 s' $d/*/wfc.log 2>/dev/null)" | tee -a $L
  return 0
}
st "PRESENTER VERIFY $N: $E ($K runs, hang at ${HANG_S} s)"
if [ -z "$SKIP_LOOP" ]; then
  for i in $(seq 1 $K); do one $O/loop-$i "WFC_D3D12PRESENT=1"; rc=$?; [ $rc = 2 ] && { st "STOPPED at run $i (hang) - reboot needed"; exit 2; }; [ $rc = 3 ] && { st "STOPPED at run $i (no card)"; exit 3; }; done
  # before / after: latency-wait max per run (af4d0731 arm B runs vs this loop)
  for d in work/leak/armB_d3d12present $O; do
    printf "%s: " "$d" | tee -a $L; grep -ohE "latency wait [0-9.]+ / [0-9.]+" $d/*/*/wfc.log $d/*/*/*/wfc.log 2>/dev/null | awk '{ n++; s += $3; if ($5 > m) m = $5 } END { printf "latency wait avg %.3f ms, max %.2f ms over %d lines\n", s / n, m, n }' | tee -a $L
  done
  st "1 loop: $(grep -c ' present=D3D12 ' $L) D3D12 runs, $(grep -c ' HANG: ' $L) hangs, $(grep -E 'watchdog=[1-9]' $L | wc -l) runs with watchdog lines; shutdown escape fired in $(grep -E 'escape=[1-9]' $L | wc -l) runs; queue not drained in $(grep -E 'nodrain=[1-9]' $L | wc -l) runs (must be 0)"
fi
[ -n "$SKIP_FG" ] || { st "2 FG run"; one $O/fg "WFC_D3D12PRESENT=1;WFC_FRAMEGEN=1" || exit 2; grep -m2 -hE "FG|frame generation" $O/fg/*/wfc.log | cut -c1-200 | tee -a $L; }
[ -n "$SKIP_RESIZE" ] || { st "3 resize run"; one $O/resize "WFC_D3D12PRESENT=1" resize || exit 2; grep -hiE "resize|swapchain" $O/resize/*/wfc.log | head -6 | cut -c1-200 | tee -a $L; }
if [ -z "$SKIP_MAPSWITCH" ]; then
  st "4 mapswitch with the presenter"
  WFC_D3D12PRESENT=1 WFC_RENDER_DATA="$RD" powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/map-loss-gate.ps1 -Exe "$EW" -OutDir $O/mapswitch -Configs normal > $O/mapswitch.out 2>&1
  st "4 mapswitch: $(grep -m1 -oE 'MAP-LOSS[^|]{0,120}|VERDICT[^|]{0,120}' $O/mapswitch.out) present lines: $(grep -rh 'PRESENT: D3D12' $O/mapswitch --include=wfc.log 2>/dev/null | wc -l)"
fi
st "PRESENTER VERIFY $N DONE"
