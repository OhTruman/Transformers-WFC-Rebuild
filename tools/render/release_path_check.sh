#!/bin/bash
# Release-path map presentation check (M11). Runs the given executable exactly as a player launches it: no
# WFC_RENDER_DATA override (the executable finds work/render itself), the frontend boot, Multiplayer -> Private Match
# -> Team Deathmatch -> Streets, then match end -> lobby -> Berth -> match end -> lobby -> Streets. Only the menu
# navigation is scripted (WFC_FRONTEND_SCRIPT) and WFC_VISUALCHECK records the verdicts; nothing else is overridden.
#
#   bash tools/render/release_path_check.sh <wfc_rebuild.exe> <scratch dir> [profile.ini]
#
# The scratch dir is the working directory (wfc.log / wfc_profile.ini land there, never in the build tree).
# Fails (exit 1) when any capture or periodic in-match check FAILs - in particular the M11 regression: map surfaces
# drawn without depth testing after the menus ("opaque draws without depth testing"), legacy fallback, no map geometry,
# GL errors - or when a match does not draw the map's structure (world + BSP draws below the floors below).
set -u
EXE="$1"; OUT="$2"; PROFILE="${3:-}"
PY="$(dirname "$0")/../../../AssetTools/bin/py/python.exe"
mkdir -p "$OUT"
[ -n "$PROFILE" ] && cp "$PROFILE" "$OUT/wfc_profile.ini"
S="$(cygpath -m "$(cd "$OUT" && pwd)")"
M="wait:level=Match;wait:ui=InGame;wait:t=6"
SCRIPT="wait:frontend;wait:ui=FrontEnd;wait:t=5;shot:$S/0_title.bmp;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=3;shot:$S/0_partylobby.bmp;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=3;call:Online.SetSelectedMapID,508;wait:t=2;call:Online.BeginLobbyExitCountdown;$M;shot:$S/1_streets_a.bmp;wait:t=4;shot:$S/1_streets_b.bmp;wait:level=GameLobby;wait:ui=InLobby;wait:t=3;call:Online.SetSelectedMapID,502;wait:t=2;call:Online.BeginLobbyExitCountdown;$M;shot:$S/2_berth_a.bmp;wait:t=4;shot:$S/2_berth_b.bmp;wait:level=GameLobby;wait:ui=InLobby;wait:t=3;call:Online.SetSelectedMapID,508;wait:t=2;call:Online.BeginLobbyExitCountdown;$M;shot:$S/3_streets_a.bmp;wait:t=4;shot:$S/3_streets_b.bmp;wait:t=1;quit"
( cd "$OUT" && env -u WFC_RENDER_DATA WFC_VISUALCHECK=1 WFC_LIFECYCLE=3 WFC_FLOW_TIMEOUT=900 WFC_FRONTEND_SCRIPT="$SCRIPT" \
    timeout 1200 "$EXE" > run.out 2>&1 ); rc=$?
fail=0
[ $rc -ne 0 ] && { echo "process exit $rc"; fail=1; }
grep -q "wfc: render data root" "$OUT/wfc.log" || { echo "no render data root found"; fail=1; }
n=$(grep -cE "VISUALCHECK .*FAIL" "$OUT/wfc.log"); [ "$n" -gt 0 ] && { echo "$n VISUALCHECK failures:"; grep -E "VISUALCHECK .*FAIL" "$OUT/wfc.log" | head -5; fail=1; }
# structural floors per map, view-independent: submitted world + BSP submeshes (drawn + frustum-culled) is the whole
# map's structure (M11: Streets 2801, Berth 2059 in every run and camera); a partially loaded map falls below. Drawn
# world + BSP must also be non-trivial (a broken scene that culls / drops everything).
PYTHONDONTWRITEBYTECODE=1 "$PY" - "$OUT" <<'EOF' || fail=1
import glob, json, os, sys
floors = {'MP_IAC_Streets': 2700, 'MP_IAC_Berth': 2000}
bad = 0
shots = sorted(glob.glob(os.path.join(sys.argv[1], '[123]_*.bmp.json')))
if len(shots) < 6:
    print('expected 6 match captures, got %d' % len(shots)); bad = 1
for f in shots:
    d = json.load(open(f, encoding='utf-8'))
    m = os.path.basename(d.get('map_data_dir', ''))
    drawn = d.get('world_draws', 0) + d.get('bsp_draws', 0)
    sub = drawn + d.get('culled_subs', 0)
    ok = d.get('verdict') == 'PASS' and sub >= floors.get(m, 1) and drawn >= 200
    print('%-22s %-16s %s world=%d bsp=%d submitted=%d noDepth=%d glErr=%d' % (os.path.basename(f)[:-9], m, 'PASS' if ok else 'FAIL',
          d.get('world_draws', 0), d.get('bsp_draws', 0), sub, d.get('opaque_no_depth_test', 0), d.get('gl_errors', 0)))
    bad += not ok
sys.exit(1 if bad else 0)
EOF
echo "release_path_check: $([ $fail -eq 0 ] && echo PASS || echo FAIL)"
exit $fail
