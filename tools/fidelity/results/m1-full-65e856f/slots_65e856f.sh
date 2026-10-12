#!/bin/bash
# M1 FULL VERDICT 65e856f (Integration 2026-10-11, approved split; run only after Gorge rp 4K 300 s x3 passed 3/3).
# Per slot: 2 maps x {4K = RenderSize 3840x2160 in a 1080 window, 1080} x {ov = FixedCam pick cam, rp = player bot} x 2 reps, 150 s
# (16 rows), rep 1 of every cell before rep 2; then per map 1 real 2560x1440 FULLSCREEN real-play row (borderless at the desktop mode,
# RenderSize 2560x1440). One discarded warm-up row opens each slot. A contaminated row is rerun once; twice = abort.
# After each fullscreen row the desktop mode is logged; if it is not back to 2560x1440 @ 144 everything STOPS (no display fixes).
#   bash work/slots_65e856f.sh A B C D
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
H=65e856f; O=work/permap/m1f-$H; CS=tools/fidelity/capacity-stress.ps1; LOG=tools/fidelity/results/PERFORMANCE_LOG.md
EXE=work/ab/m9c_$H/build-release/bin/wfc_rebuild.exe; DESK="current 2560x1440 @ 144 Hz"
eval "$(sed -n '/^mem()/p' work/batch_regress.sh)"
foreign() { powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.Name -like 'wfc_rebuild*' -or \$_.Name -like 'hd*' -or (\$_.Name -eq 'python.exe' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') } | % { '{0} {1} {2}' -f \$_.ProcessId, \$_.Name, \$_.ExecutablePath }"; }
waitclear() { while true; do f=$(foreign); [ -z "$f" ] && break; echo "$(date +%H:%M:%S) waiting on: $(echo $f | tr '\n' ' ')"; sleep 15; done; }
p90() { powershell -NoProfile -Command "(Import-Csv '$1/capacity.csv' | ? { \$_.match -eq '2' } | Select-Object -First 1).p90_ms"; }
row() {  # row <map> <ov|rp|fs> <2160|1080|1440> <rep> [nolog]
  local d=$O-$1-$2-$3-t150-r$4 M="" W="-Resolutions 1920x1080" RS="" F="" PL="-PerfLog '$LOG'"
  [ -n "$5" ] && PL=""
  case $2 in ov) M="-FixedCam -CamByMap @{ '$1' = '$(cat work/permap/cams-$1/pick.txt)' }";; *) M="-PlayerBot 1";; esac
  [ $3 = 2160 ] && RS="-RenderSize 3840x2160"
  [ $2 = fs ] && { W="-Resolutions 2560x1440"; RS="-RenderSize 2560x1440"; F="-Fullscreen"; }
  for try in 1 2; do
    waitclear
    powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$CS' -Root 'work/ab/m9c_$H' -OutDir '$d' -Maps $1 -Pops t64 $W $F -TimeLimit 150 $M -NoSplit $RS -ExtraEnv 'WFC_SLOWFRAME=3.33' $PL -Note 'M1 full verdict $H map $1 $2 $3 150s rep $4'" > $d.out 2>&1 &
    local bg=$!
    if [ $2 = fs ]; then
      for i in $(seq 1 30); do sleep 3; [ -n "$(powershell -NoProfile -ExecutionPolicy Bypass -File work/clientrect.ps1 -Exe $EXE)" ] && break; done
      sleep 60; powershell -NoProfile -ExecutionPolicy Bypass -File work/clientrect.ps1 -Exe $EXE > $d-clientrect.txt 2>&1
    fi
    wait $bg
    local c=$(ls $d/*/CONTAMINATED.txt 2>/dev/null)
    echo "$(date +%H:%M) $1 $2 $3 r$4 try $try: p90 $(p90 $d) $(cat $d-clientrect.txt 2>/dev/null) $c"
    if [ $2 = fs ]; then
      local m=$(work/tools/dispmodes.exe | grep -m1 DISPLAY1); echo "$(date +%H:%M:%S) after $1 fs r$4: $m" | tee -a $O-desktop.txt
      case "$m" in *"$DESK"*) ;; *) echo "DESKTOP MODE NOT RESTORED - stopped"; return 2;; esac
    fi
    [ -z "$c" ] && return 0
    [ $try = 2 ] && { echo "ABORT: $d contaminated twice"; return 1; }
    mv $d $d-contam1; mv $d.out $d-contam1.out
  done
}
finish() { mem "$1"; for p in $(powershell -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*wfc_rebuild.exe' } | % { \$_.ProcessId }"); do echo "leftover $p"; done
           unset WFC_GATE_IGNORE_HOLD; rm -f work/PERF_HOLD; echo "$2 $(date +%H:%M)"; }
declare -A MAPS=( [A]="510 504" [B]="509 507" [C]="508 503" [D]="501 502" )
echo "desktop before: $(work/tools/dispmodes.exe | grep -m1 DISPLAY1)" | tee -a $O-desktop.txt
for s in "$@"; do
  maps=${MAPS[$s]}; [ -z "$maps" ] && { echo "unknown slot $s"; continue; }
  echo "experimental PERF RUN M1 full verdict $H slot $s ($maps) $(date +%H:%M)" > work/PERF_HOLD
  waitclear; echo "slot $s open $(date +%H:%M:%S) maps $maps"; mem "slot-$s-start"
  export WFC_GATE_IGNORE_HOLD=1
  row ${maps%% *} rp 2160 0 nolog || { finish "slot-$s-abort" "ABORTED in slot $s warm-up"; exit 1; }
  for r in 1 2; do for m in $maps; do for res in 2160 1080; do for mode in ov rp; do
    row $m $mode $res $r || { finish "slot-$s-abort" "ABORTED in slot $s"; exit 1; }
  done; done; done; done
  for m in $maps; do row $m fs 1440 1; rc=$?; [ $rc != 0 ] && { finish "slot-$s-abort" "STOPPED in slot $s (fs rc $rc)"; exit 1; }; done
  mem "slot-$s-end"; echo "slot $s done $(date +%H:%M)"
done
finish end "all slots done"
