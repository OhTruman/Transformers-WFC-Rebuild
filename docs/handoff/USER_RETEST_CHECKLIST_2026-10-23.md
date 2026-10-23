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
| Public lobby (Quick Match) | Map vote with preview, autostart once enough players are in; XP only in public games |
| Voice chat | Push to talk (V) by default; Settings > Voice. Gamer Card button on a player opens Mute / Block (next package) |
| Escalation | Remnant and the other Escalation maps: waves, horde, weapon factories, generators. No XP (as the original), end screen shows the wave reached |
| Campaign | Chapter select and lobby only; chapters do not load in this package yet |
