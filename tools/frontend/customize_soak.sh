#!/bin/bash
# Create a Character soak: repeated customization cycles (all four classes, both factions' chassis menus, Change Form,
# a colour edit) and private matches with in-match character selection, return to the party lobby and reopening
# customization. Every step logs a navcheck (UI owner, AS heap / timers / graveyard, preview pawn state, scene
# matinees, camera, process memory) for tools/frontend/customize_soak.js.
# Usage: tools/frontend/customize_soak.sh <run dir> [cycles=4] [matches=2] [exe=build/bin/wfc_rebuild.exe]
set -u
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
RUN="${1:?run dir}"; CYC="${2:-4}"; MATCH="${3:-2}"; EXE="${4:-$HERE/build/bin/wfc_rebuild.exe}"
mkdir -p "$RUN"; cd "$RUN"; rm -f wfc.log wfc_profile.ini wfc_characters.ini *.bmp
[ -f "$HERE/work/camx/wfc_profile.ini" ] && cp "$HERE/work/camx/wfc_profile.ini" .
L="lobby_mc.menuAnchor_mc.menu_mc.customCharacters_mc"
C="customTransMenuLoader_mc.customCharMenu_mc"
O="$C.characterOverview_mc.charOverviewMenu_mc"
CLS=(button01_mc button04_mc button07_mc button10_mc)
NAMES=(scout scientist leader soldier)
S="wait:ui=FrontEnd;wait:t=2;navcheck:main.start"
for ((c = 1; c <= CYC; ++c)); do
  S="$S;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:t=4;navcheck:c$c.partylobby"
  S="$S;clickclip:$L;wait:t=3;navcheck:c$c.cac"
  for k in 0 1 2 3; do
    n="c$c.${NAMES[$k]}"
    # The class list keeps focus on the class just left: Accept the first, then Down + Accept (a click on the
    # buttonNN_mc clips is not reliable: the list scrolls under the pointer).
    if [ $k -eq 0 ]; then S="$S;ui:Accept"; else S="$S;ui:Down;wait:t=0.7;ui:Accept"; fi
    S="$S;wait:t=5;navcheck:$n.overview"
    S="$S;clickclip:$O.chassisButtonA_mc;wait:t=2.5;navcheck:$n.autobot;ui:LThumb;wait:t=1.2;navcheck:$n.vehicle;ui:LThumb;wait:t=1.2"
    S="$S;ui:Back;wait:t=3;clickclip:$O.chassisButtonD_mc;wait:t=2.5;navcheck:$n.decepticon;ui:Back;wait:t=3;navcheck:$n.overview2"
    if [ $k -eq $(( (c - 1) % 4 )) ]; then   # one colour edit per cycle: Color 1 picker, move, next palette, accept
      S="$S;clickclip:$O.chassisButtonA_mc;wait:t=2.5;ui:Down;wait:t=0.7;ui:Accept;wait:t=1.5;navcheck:$n.picker"
      for i in 1 2 3 4 5; do S="$S;ui:Right;wait:t=0.05;ui:Up;wait:t=0.05"; done
      S="$S;ui:RT;wait:t=0.8;ui:Accept;wait:t=2;navcheck:$n.picked;ui:Back;wait:t=3"
    fi
    S="$S;ui:Back;wait:t=3;navcheck:$n.list"
  done
  S="$S;ui:Back;wait:t=2;navcheck:c$c.partylobby.back;ui:Back;wait:t=1;ui:Accept;wait:level=FrontEnd;wait:t=2;navcheck:c$c.main"
done
for ((m = 1; m <= MATCH; ++m)); do
  k=$(( (m - 1) % 4 ))
  S="$S;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:t=2;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:t=2"
  S="$S;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:t=3;navcheck:m$m.charselect"
  for ((i = 0; i < k; ++i)); do S="$S;ui:Down;wait:t=0.7"; done
  S="$S;ui:Accept;wait:ui=InGame;wait:t=3;navcheck:m$m.ingame;shot:m$m.bmp"
  S="$S;showmenu;wait:t=1;call:Game.QuitToMainMenu;wait:t=1;ui:Accept;wait:level=PartyLobby;wait:t=4;navcheck:m$m.partylobby"
  S="$S;clickclip:$L;wait:t=3"
  for ((i = 0; i < k; ++i)); do S="$S;ui:Down;wait:t=0.7"; done
  S="$S;ui:Accept;wait:t=5;navcheck:m$m.reopen.${NAMES[$k]};shot:m${m}r.bmp"
  S="$S;ui:Back;wait:t=3;ui:Back;wait:t=2;ui:Back;wait:t=1;ui:Accept;wait:level=FrontEnd;wait:t=2;navcheck:m$m.main"
done
S="$S;wait:t=2;navcheck:main.end;shot:final.bmp;quit"
WFC_GFX_GCCHECK="${WFC_GFX_GCCHECK:-}" WFC_NOPAD=1 WFC_NOMOUSE=1 WFC_CHARSELECT=1 WFC_BOOT=frontend WFC_SKIPINTRO=1 \
  WFC_FRONTEND_SCRIPT="$S" WFC_FLOW_TIMEOUT=3600 timeout 5400 "$EXE" > out.txt 2>&1
echo "exit=$?"
node "$HERE/tools/frontend/customize_soak.js" wfc.log
