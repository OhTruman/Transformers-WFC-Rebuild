# Package 7 playtest guide - Menus (Frontend; the "Menus" section of PACKAGE_7_PLAYTEST_GUIDE.md)

What changed in the menus and HUD since package 6, where to find it, what should happen, and what to report if not.
"Report" = a screenshot plus one line on what you did; the game log (wfc.log next to Play.bat) helps.

| # | where (click path) | what should happen | report if |
|---|---|---|---|
| 1 | Settings > Graphics > **FPS Counter** (Off / On) | On: top-right "144 FPS  6.9 ms", updating about 4 times a second, in menus and in matches; Off: nothing | it does not appear, freezes, or overlaps other text |
| 2 | Settings > Graphics > **Volumetric Lighting** | with Ray Tracing **Realistic**: Off / On (On = light shafts and haze); with any other Ray Tracing setting the row shows only "Realistic only" | the row offers On without Realistic, or On changes nothing in a match |
| 3 | Multiplayer > Private Match > a team mode (e.g. Team Deathmatch) > the options page: **Player Limit**, **Autobot Bots**, **Decepticon Bots**, **Bot Difficulty** (Deathmatch: **Bots**) | Player Limit Original (5 v 5) / Extended (32 v 32); bot counts never go above what the limit allows; Bot Difficulty Easy / Medium / Hard / Expert; Create Game -> the match has those bots | a match has a different number of bots than set, or a value resets after a map change |
| 4 | the same options page > **Iconic Characters** (Off / On) - a PC addition; the original only had iconic characters in Escalation / co-op | On: the lobby menu shows **Select Character**; it opens a wheel of named characters (Bumblebee, Optimus Prime, ...: 18 on a new profile) with portraits and "SPECIALTY   UNCLAIMED"; pick one, Start Game -> you spawn as that character. Off (default): everything as before | the wheel is empty or blank, a picked character spawns as someone else, or "Escalation Info" appears in the versus lobby |
| 5 | Multiplayer > Private Match > Create a Character > a character > **Rename** | the prompt is titled "Name Character" / "Enter a name for this custom character build." | the title or description is blank |
| 6 | a match, die | about 3 s on your own body, then the camera follows the player who killed you, with "currently spectating <name>" fading in; it fades out when you respawn | the camera stays on your body, follows the wrong player, or the label never shows |
| 7 | a match, an enemy jams you (Scientist / killstreak jammers) | your ability slots turn red while jammed and come back when it ends; a renewed jam redraws it | abilities look usable while jammed, or stay red after it ends |
| 8 | Private Match with bots (no menu row) | bots use the **Smart** AI by default (they take cover, flank, use abilities); there is no Classic choice any more | bots stand still, walk into walls, or never use abilities |

**Known limits (expected, not bugs):**
- **Online play is not in this package.** Everything online (Join Game in the party lobby, the join code panel, the voice rows in
  Settings > Audio, Mute in the player list, the one-time "Online Play" message) appears only when an online session exists; the
  network itself is still being built. In package 7 none of it is visible: Settings > Audio shows the original four rows.
- Iconic characters that need unlocking (Arcee, Jazz, Shockwave, ...) are hidden on a new profile; they unlock with specialty
  levels like the locked bodies.
- The FPS counter measures the game's own frame time; it can differ slightly from an overlay such as the AMD one.
