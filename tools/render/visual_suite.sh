#!/bin/bash
# Visual regression suite (M10): fixed-camera Streets positions, the frontend title scene and, when the build has the
# frontend runtime, the human path (title -> lobbies -> Streets -> match end -> lobby -> Streets again).
#
#   bash tools/render/visual_suite.sh <wfc_rebuild.exe> <out dir> [--ref <dir>] [--flow]
#
# Every capture is taken with WFC_VISUALCHECK=1 and checked by visual_check.py; the suite exits non-zero if any
# capture fails. --ref compares against a known-good run of this suite (same positions); make one by running the suite
# on a known-good build into a reference directory. WFC_RENDER_DATA is passed through when set; otherwise the
# executable finds work/render itself (the M10 root-cause fix) - which is part of what this suite tests.
set -u
EXE="$1"; OUT="$2"; shift 2
REF=""; FLOW=0
while [ $# -gt 0 ]; do
  case "$1" in --ref) REF="$2"; shift 2;; --flow) FLOW=1; shift;; *) shift;; esac
done
PY="$(dirname "$0")/../../../AssetTools/bin/py/python.exe"
mkdir -p "$OUT"
W="$(cygpath -m "$(cd "$OUT" && pwd)")"
fail=0
# Streets: five fixed cameras across the map (WFC_RENDERCAM pins the renderer's camera, independent of Gameplay's
# camera code; the spawn index only places the player). Values: the M10 standalone cameras at those spawns.
CAMS=("0:371.727,-719.284,-339.968,1.01247,-0.15000" "18:345.411,-701.921,-458.812,1.66854,-0.15000"
      "40:344.823,-701.365,-429.420,1.45563,-0.15000" "63:26.169,-716.505,-512.406,-1.96253,-0.15000"
      "83:24.267,-716.505,-602.320,-2.31535,-0.15000")
for c in "${CAMS[@]}"; do
  sp="${c%%:*}"; cam="${c#*:}"
  WFC_VISUALCHECK=1 WFC_RENDERCAM="$cam" WFC_SPAWN_INDEX=$sp WFC_LOCKSTEP=1 WFC_SMOKE_FRAMES=150 WFC_SHOT="$W/streets_cam$sp.bmp" \
    timeout 600 "$EXE" > "$OUT/streets_cam$sp.log" 2>&1 || { echo "streets camera $sp: exit $?"; fail=1; }
done
# Frontend title scene from its authored camera (CameraActor_6585)
WFC_VISUALCHECK=1 WFC_FRONTENDSCENE=UI_FrontEnd_m,UI_FrontEnd_capture_VIG_m WFC_SMOKE_FRAMES=150 WFC_SHOT="$W/title_scene.bmp" \
  timeout 600 "$EXE" > "$OUT/title_scene.log" 2>&1 || { echo "title scene: exit $?"; fail=1; }
if [ $FLOW -eq 1 ]; then
  P="$W/flow_"
  WFC_VISUALCHECK=1 WFC_LIFECYCLE=2 WFC_FLOW_TIMEOUT=420 WFC_FRONTEND_SCRIPT="wait:frontend;wait:ui=FrontEnd;wait:t=6;shot:${P}1_title.bmp;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=4;shot:${P}2_partylobby.bmp;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=3;call:Online.SetSelectedMapID,508;wait:t=2;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=6;shot:${P}3_match1.bmp;wait:level=GameLobby;wait:ui=InLobby;wait:t=4;shot:${P}4_backlobby.bmp;call:Online.SetSelectedMapID,508;wait:t=2;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=6;shot:${P}5_match2.bmp;wait:t=1;quit" \
    timeout 600 "$EXE" > "$OUT/flow.log" 2>&1 || { echo "flow: exit $?"; fail=1; }
fi
ARGS=("$OUT")
[ -n "$REF" ] && ARGS+=(--ref "$REF")
PYTHONDONTWRITEBYTECODE=1 "$PY" "$(dirname "$0")/visual_check.py" "${ARGS[@]}" || fail=1
MAPS=()
for f in "$OUT"/streets_*.bmp.json "$OUT"/flow_*match*.bmp.json; do [ -f "$f" ] && MAPS+=("$f"); done
PYTHONDONTWRITEBYTECODE=1 "$PY" "$(dirname "$0")/visual_check.py" "${MAPS[@]}" --expect-map > /dev/null 2>&1 || {
  echo "map captures: not on the original path / no map geometry"; fail=1; }
exit $fail
