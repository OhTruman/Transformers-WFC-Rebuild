# Retest checklist for the 2026-10-23 playtest findings

Build: the internal test package Systems is building from be5b7cdc (pt9 PASS head). Later Frontend commits (player card, Escalation
no-XP) arrive in the next package. Play Private Match (bots) unless a row says otherwise.

## Your 18 findings

| # | Finding | What changed | Retest |
|---|---|---|---|
| 2 | Respawn timer stuck | The killer-cam label opened a second copy of the respawn movie that nothing closed | Die, respawn, pause while dead, quit to menu: no timer anywhere |
| 11 | Shots not where you aim | Spread was a square twice the original's disc; sampling order fixed; shotgun patterns | AR / Ion Blaster at range and zoomed: shots land on the reticle |
| 17 | Can't switch class in a match | Iconic-mode pause menu dropped Choose Transformer; World side verified | Pause > Choose Transformer mid-match, respawn as the new class |
| 9 | Boost trails stuck in mid-air | Jet thrusters' particles were immortal; authored kill-on-stop flags restored | Scientist / jet boost: trails end when the boost ends |
| 3 / 16 | Vehicle headlight flicker + odd shadows | Lights kept swapping ray-traced shadow slots every frame; lamp self-shadowing | Drive a car in Realistic: steady headlight and shadow |
| 18 | Bots floating | Bot dodges flew flat off ledges (now nav-limited as the original's AI); 266 raised collision hulls fixed on all 10 maps | Watch bots on Gorge / Debris / Berth: feet on the ground |
| 10 / 13 | Seed of Corruption collision | The 20 m invisible column over the hole removed (PC adaptation); 40 undersized hulls now per-poly | Fly / drive over the centre; walk into the cylinders |
| 1 | Slow screen transitions | AnimSets pre-parsed on worker threads, the last scenes and their textures kept resident | Main menu > Multiplayer > lobby > back: no hitches after the first visit |
| 5 | Death smoke too long | Death effects follow the body and stop with it (jet smoke looped forever: 101 s -> ~7 s) | Jet deaths: smoke fades within a few seconds |
| 4 | Realistic texture pack missing | The renderer reported Enhanced unavailable regardless of the data | Settings > Graphics > HD row offers Off / Faithful / Enhanced (your pack is Enhanced) |
| 6 | Video settings off-screen | Pages over 10 rows scroll with focus and the wheel | Settings > Graphics: scroll to the last row |
| 7 | Scoreboard spacing | Versus lists used the Escalation portrait rows (104 px -> 25 px) | Countdown roster and scoreboard: compact rows |
| 8 | Teammate health bars through walls | AUTHENTIC: only a Scientist sees ally bars, through walls, as the original. Open decision: keep or limit | Play Scientist: bars show; another class: none |
| 15 | Weapon switch animation | The authored put-down / equip anims now play (timing was already right) | Switch weapons: the gun lowers and raises |
| 14 | Water | Authored refraction restored on the waterfalls; the Enhanced water option stays hidden (did not pass review) | Remnant waterfall base and coolant shimmer |
| 12 | Time to kill | Damage / health equal the original; bot accuracy was ~2x too high, now data-tuned per difficulty | Medium bots: you survive longer; Hard noticeably sharper |

## New since the playtest

| Area | What to try |
|---|---|
| Smoke (opt-in) | Settings > Graphics > Smoke: Original / Enhanced. Enhanced = lit, drifting smoke; compare explosions and death smoke |
| Lens flares (opt-in) | Settings > Graphics > Lens Flares: Off / Subtle / Original; look at the sun on Rust / Debris |
| Online private match | Host shows a code; a second PC joins by it (PACKAGE_7_TWO_PC_TEST.md) |
| Join in progress | Host a match; once it runs, join from the second PC by code: catch-up screen with %, then Choose Character, spawn on the smaller team. Leaving mid-match hands the slot to a bot |
| Public lobby (Quick Match) | Map vote with preview, autostart once enough players are in; teams re-drafted just before the start (by last match's score, else level); XP only in public games |
| Friends List / invites | Lobby > Friends List shows players you met online ("Online" when they are reachable). Send Invite copies your game code and delivers it if the invite server is up; the other player gets "`p wants to play" with Join |
| Voice chat | Push to talk (V) by default, team-only in team matches (as the original); Settings > Voice |
| Escalation | Starts from the menus (Escalation > Remnant / Broken Hope): waves, horde, weapon factories, generators. No XP (as the original); quitting mid-match warns "Your leaderboard progress will be lost"; end screen shows the wave reached |
| Campaign | Solo Campaign > A1 (Autobot chapter VI, "Defend Iacon": opening shot, lift ride, then control) and D5 (Decepticon chapter V, "The Final Guardian") play from the menu on Normal, Off and Realistic. Pick any of the chapter's three characters in the map; the other two follow as your squad. Checkpoints save: Resume Autobot / Resume Decepticon then appear in the Solo menu. Death shows MISSION FAILED with Restart From Last Checkpoint / Restart Chapter. Loads: A1 ~45 s (Realistic ~60 s), D5 ~40 s (Realistic ~55 s). Other chapters stay locked until their data is exported |
| Co-op campaign | Cooperative > host a CP game, a second PC joins by code; characters another player took are greyed in the pick (untested with two real players yet) |
| Mute / Block | In a lobby or the pause player list, A on a player opens Mute / Block; a blocked player stays muted when they come back |
| Lobby Game Stats | After a public match, Y in the game lobby re-opens the XP you earned |
