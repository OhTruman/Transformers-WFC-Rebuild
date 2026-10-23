# Two-PC online test (about 20 minutes)

**What this is.** The "everything" build plays online from the game's own menus: host a match, join it with a code, play it
together on two PCs, leave, and join again while the match is running (join in progress). This test checks all of that between
two PCs on your home network. Over the internet it needs the lobby server (the Hetzner VPS), which comes later: for now both PCs
must be on the **same home network**.

## 1. Set up the second PC (PC B)

1. Copy the **whole package folder** (the one with Play.bat) to PC B, to a short path such as `D:\WFC` (deep folders break some
   file paths).
2. Both PCs on the **same home network** (wired or Wi-Fi), with the network set to **Private** in Windows.
3. Start the game on each PC with **Play.bat** (not the exe directly).
4. The first time a PC hosts or joins, Windows may ask whether to allow **wfc_rebuild.exe** on the network: choose **Allow**
   (Private networks).

## 2. A private match by code (PC A hosts)

1. **Both PCs:** Multiplayer.
2. **PC A:** **Private Match**. You are in the game lobby at once. The right column shows **Join code: 3Z000-02DNG** (two groups
   of five letters and digits on a home network) and **Address: 192.168.x.x:14000** under it; press **C** to copy the code, or
   read it out.
3. **PC B:** **Join Game**, type the code (upper or lower case) or PC A's address, press Enter. "Joining game..." then PC B is in
   PC A's lobby. Both lobbies list both players.
4. **PC A:** pick a map and start. Both PCs load, then the match starts together (Choose Character on each).
5. Play a few minutes: move, shoot, transform, die, respawn. Both screens should agree: the same scores, the same kills, each
   other's robot where it should be. Try the voice key (push to talk) if you have headsets.

## 3. Join in progress (a public match)

1. **PC A:** Multiplayer, **Find Match**, **Team Deathmatch**. With no lobby server and no game on the network it hosts a public
   lobby; it shows "Finding 1 more player" and the same Join code / Address as a private lobby.
2. **PC B:** Multiplayer, **Find Match**, **Team Deathmatch** (it finds PC A's lobby on the network), or **Join Game** with the code.
   With two players in, the 45 s countdown starts by itself (Start Game is off in public lobbies, as in the original); the map vote
   closes at 10 s and the match loads.
3. After a minute of play, **PC B:** pause, **Quit Game**, answer **Yes** to "Leave Lobby". PC B is back in the party lobby; PC A
   plays on (PC B's robot leaves the match).
4. **PC B:** **Join Game** with PC A's code. It goes straight into the running match: the loading screen shows "Joining the match
   in progress... n%", then Choose Character, and PC B spawns on the team with fewer players. It should take a few seconds.
5. Play on for a couple of minutes: both screens should agree again.

## 4. What counts as a pass

- Section 2: both PCs in the same match, scores and kills agree, the match ends together.
- Section 3: PC B leaves without ending PC A's match, joins again mid-match, and both agree afterwards.
- Neither PC shows "the matches went out of sync" (a desync message) or drops back to the menu by itself.

If something goes wrong: "That is not a join code or an address." - check it was typed in full; "Join Game Failed" - PC A's
lobby closed or the firewall blocked it (allow wfc_rebuild.exe, Private network); "Match in Progress: The match is too far along
to join" - joining a running public match is limited to its first 5 minutes (or "The match is full"): PC B stays in the lobby and
joins the next match when it starts, as in the original.

## 5. Send back from **each** PC

**wfc.log** from the package folder (beside Play.bat), labelled `PC-A` / `PC-B`. It has the session lines (`session: ...`), the
match network lines (`NET ...`: the players, the input delay, a leave, the late join and its catch-up) and any error. Nothing
in it is audio or personal beyond the player names you chose.
