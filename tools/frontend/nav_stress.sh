#!/usr/bin/env bash
# Frontend navigation stress harness: enters and backs out of Settings, Accounts (create-name prompt), Extras (Concept
# Art viewer, Movies + a movie, Credits), Multiplayer / Private Match, and - in the match part - Choose Character,
# Pause / Resume and Quit, repeatedly, from a cold frontend. A navcheck:<label> step after every transition records the
# UI state, open movies, focus owner, modal state and resource counters (nav.check lines in wfc.log);
# tools/frontend/nav_check.js asserts the invariants.
#
# usage: tools/frontend/nav_stress.sh <run dir> [menu cycles=5] [match cycles=2]
set -u
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
RUN="${1:?run dir}"; MENU="${2:-5}"; MATCH="${3:-2}"
mkdir -p "$RUN"; cd "$RUN"; rm -f wfc.log wfc_profile.ini wfc_characters.ini *.bmp
D="wait:t=0.6"
M="menuAnchor_mc.menuMain_mc"
X="extrasMenuLoader_mc.extrasMenu_mc.rootMenu_mc"
S="wait:ui=FrontEnd;wait:t=2;mouse:5,5;navcheck:main.start"
for ((c = 1; c <= MENU; ++c)); do
  # Settings: Graphics page and back.
  S="$S;clickclip:$M.settingsBtn_mc;wait:t=2;navcheck:c$c.settings;ui:Accept;wait:t=1.5;navcheck:c$c.settings.graphics"
  S="$S;ui:Back;wait:t=1;ui:Back;wait:t=1.5;mouse:5,5;navcheck:c$c.main"
  # Accounts: the New Account prompt (typed text), Escape cancels it, Back leaves.
  S="$S;clickclip:$M.accountsBtn_mc;wait:t=2;navcheck:c$c.accounts;ui:Accept;wait:t=1.5;type:Name$c;$D;navcheck:c$c.accounts.prompt"
  S="$S;ui:Back;wait:t=1;navcheck:c$c.accounts.after;ui:Back;wait:t=1.5;mouse:5,5;navcheck:c$c.main"
  # Extras: Concept Art viewer, Movies (+ the intro movie, skipped), Credits (skipped).
  S="$S;clickclip:$M.extrasBtn_mc;wait:t=2;navcheck:c$c.extras;clickclip:$X.conceptArtBtn_mc;wait:t=1.5;navcheck:c$c.extras.conceptart"
  S="$S;ui:Accept;wait:t=1.5;navcheck:c$c.extras.conceptart.viewer;ui:Back;wait:t=1;ui:Back;wait:t=1;mouse:5,5;navcheck:c$c.extras"
  S="$S;clickclip:$X.moviesBtn_mc;wait:t=1.5;navcheck:c$c.extras.movies;mouse:5,5;ui:Accept;wait:t=2;navcheck:c$c.extras.movie.playing"
  S="$S;ui:Back;wait:t=1.5;navcheck:c$c.extras.movies.after;ui:Back;wait:t=1;mouse:5,5;navcheck:c$c.extras"
  S="$S;clickclip:$X.creditsBtn_mc;wait:t=2;navcheck:c$c.extras.credits.playing;ui:Back;wait:t=1.5;mouse:5,5;navcheck:c$c.extras.after.credits"
  S="$S;ui:Back;wait:t=1.5;navcheck:c$c.main"
  # Multiplayer: party lobby, Private Match mode list, back, Quit box -> front end.
  S="$S;clickclip:$M.multiplayerBtn_mc;wait:level=PartyLobby;wait:t=2;mouse:5,5;navcheck:c$c.partylobby;ui:Down;$D;ui:Accept;wait:t=1.5;navcheck:c$c.privatematch.modes"
  S="$S;ui:Back;wait:t=1;navcheck:c$c.partylobby.back;ui:Back;wait:t=1;navcheck:c$c.partylobby.quitbox;ui:Accept;wait:level=FrontEnd;wait:t=2;mouse:5,5;navcheck:c$c.main"
done
for ((m = 1; m <= MATCH; ++m)); do
  # Private match: Choose Character, InGame, Pause / Resume, Quit (box) -> party lobby -> front end.
  S="$S;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:t=1;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:t=2"
  S="$S;navcheck:m$m.gamelobby;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:t=2;navcheck:m$m.charselect;ui:Accept;wait:ui=InGame;wait:t=1;navcheck:m$m.ingame"
  S="$S;showmenu;wait:t=1;navcheck:m$m.pause;ui:Back;wait:t=1;navcheck:m$m.resumed;showmenu;wait:t=1;call:Game.QuitToMainMenu;wait:t=1;navcheck:m$m.quitbox"
  S="$S;ui:Accept;wait:level=PartyLobby;wait:t=2;navcheck:m$m.partylobby.afterquit;ui:Back;wait:t=1;ui:Accept;wait:level=FrontEnd;wait:t=2;mouse:5,5;navcheck:m$m.main"
done
S="$S;wait:t=2;navcheck:main.end;shot:final.bmp;quit"
WFC_CHARSELECT=1 WFC_BOOT=frontend WFC_SKIPINTRO=1 WFC_FRONTEND_SCRIPT="$S" WFC_FLOW_TIMEOUT=900 \
  timeout 2400 "$HERE/build/bin/wfc_rebuild.exe" > out.txt 2>&1
echo "exit=$?"
node "$HERE/tools/frontend/nav_check.js" wfc.log
