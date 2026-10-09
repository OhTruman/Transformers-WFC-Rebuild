#!/bin/bash
# Frontend-launched map chain (Integration's persistent-renderer bar): Streets, Seed, Berth, Gorge, Complex, Rust,
# Debris, Molten, Streets through party lobby -> private TDM -> map -> match -> quit -> party lobby, [rounds] times.
# Reports match.loaded / match.unloaded privateMB and the GL census after every unload (match.glCensus).
# Usage: tools/frontend/map_chain.sh <run dir> [rounds=1] [exe=build/bin/wfc_rebuild.exe]   (env passes through, e.g.
# WFC_PERSISTENT_RENDERER=1 for one renderer across matches)
set -u
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
RUN="${1:?run dir}"; ROUNDS="${2:-1}"; EXE="${3:-$HERE/build/bin/wfc_rebuild.exe}"
mkdir -p "$RUN"; cd "$RUN"; rm -f wfc.log wfc_profile.ini wfc_characters.ini *.bmp
[ -f "$HERE/work/camx/wfc_profile.ini" ] && cp "$HERE/work/camx/wfc_profile.ini" .
MAPS=(${CHAIN_MAPS:-508 501 502 510 503 504 507 509 508})   # default: Streets Seed Berth Gorge Complex Rust Debris Molten Streets
S="wait:ui=FrontEnd;wait:t=2"
for ((r = 1; r <= ROUNDS; ++r)); do
  for i in "${!MAPS[@]}"; do
    id=${MAPS[$i]}
    S="$S;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:t=2;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:t=2"
    S="$S;call:Online.SetSelectedMapID,$id;wait:t=0.5;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:t=2;ui:Accept;wait:ui=InGame;wait:t=4"
    S="$S;shot:r${r}_${i}_$id.bmp;navcheck:r$r.$i.$id.ingame;showmenu;wait:t=1;call:Game.QuitToMainMenu;wait:t=1;ui:Accept;wait:level=PartyLobby;wait:t=3"
    S="$S;navcheck:r$r.$i.$id.lobby;ui:Back;wait:t=1;ui:Accept;wait:level=FrontEnd;wait:t=2"
  done
done
S="$S;navcheck:end;quit"
WFC_GLCENSUS=1 WFC_NOPAD=1 WFC_NOMOUSE=1 WFC_CHARSELECT=1 WFC_BOOT=frontend WFC_SKIPINTRO=1 WFC_FRONTEND_SCRIPT="$S" WFC_FLOW_TIMEOUT=5400 \
  timeout 7200 "$EXE" > out.txt 2>&1
echo "exit=$?"
node -e '
const l = require("fs").readFileSync("wfc.log", "utf8").split(/\r?\n/);
let map = "";
for (const x of l) {
  let m;
  if ((m = x.match(/FLOW match\.loaded .*?map=([^ ]+).*?privateMB=([0-9.]+)/)) || (m = x.match(/FLOW match\.loaded .*?privateMB=([0-9.]+)/))) {
    if (m.length === 3) { map = m[1]; console.log(`loaded   ${map.padEnd(26)} ${m[2]} MB`); } else console.log(`loaded   ${m[1]} MB`);
  }
  if ((m = x.match(/FLOW match\.unloaded privateMB=([0-9.]+)/))) console.log(`unloaded ${"".padEnd(26)} ${m[1]} MB`);
  if ((m = x.match(/FLOW match\.glCensus live=(.*?) renderer=(\w+)/))) console.log(`census   ${m[2]} ${m[1]}`);
}
const vc = l.filter(x => /VISUALCHECK.*FAIL|noDepth=[1-9]|glErr=[1-9]/.test(x)).length;
console.log(`visual/GL failures logged: ${vc}`);'
