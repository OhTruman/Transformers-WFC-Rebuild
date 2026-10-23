# Retest checklist for the 2026-10-23 playtest findings (DRAFT, updated as items close)

Build: the next internal test package (not cut yet; head on integration/milestone-09c). Play Private Match (bots) unless a row says otherwise.

| # | Finding | What changed | Retest |
|---|---|---|---|
| 2 | Respawn timer stuck | The killer-cam label opened a second copy of the respawn movie that nothing closed | Die, respawn, pause while dead, quit to menu: no timer anywhere |
| 11 | Shots not where you aim | Spread was a square twice the original's disc; sampling order fixed; shotgun patterns | AR / Ion Blaster at range and zoomed: shots land on the reticle |
| 17 | Can't switch class in a match | Iconic-mode pause menu dropped Choose Transformer; World side verified | Pause > Choose Transformer mid-match, respawn as the new class |
| 9 | Boost trails stuck in mid-air | Jet thrusters' particles were immortal; authored kill-on-stop flags restored | Scientist / jet boost: trails end when the boost ends |
| 3 / 16 | Vehicle headlight flicker + odd shadows | Lights kept swapping ray-traced shadow slots every frame; lamp self-shadowing | Drive a car at night in Realistic: steady headlight and shadow |
| 18 | Bots floating | Bot dodges flew flat off ledges (original move, now nav-limited as the original's AI); raised collision hulls fixed on all maps | Watch bots on Gorge / Debris / Berth: feet on the ground |
| 10 / 13 | Seed of Corruption collision | The 20 m invisible column over the hole removed (PC adaptation); 40 undersized hulls now per-poly | Fly / drive over the centre; walk into the cylinders |
| 1 | Slow screen transitions | AnimSets pre-parsed, scenes and textures kept resident, duplicate parses removed | Main menu > Multiplayer > lobby > back: no hitches after the first visit |
| 5 | Death smoke too long | Death effects now follow the body and stop with it (one looped forever) | Jet deaths: smoke ends within a few seconds of the body |
| 4 | Realistic texture pack missing | The renderer reported Enhanced unavailable regardless of the data | Settings > Graphics > HD row offers Off / Faithful / Enhanced (the pack is labelled Enhanced) |
| 6 | Video settings off-screen | Pages over 10 rows scroll with focus and the wheel | Settings > Graphics: scroll to Lens Flares |
| 7 | Scoreboard spacing | Versus lists used the Escalation portrait rows | Countdown roster and scoreboard: compact rows |
| 8 | Teammate health bars through walls | AUTHENTIC: only a Scientist sees ally bars, through walls, as the original | Play Scientist: bars show; play another class: no bars |
| 15 | Weapon switch animation | The authored put-down / equip anims now play (timing was already right) | Switch weapons: the gun lowers and raises |
| 14 | Water | Authored refraction restored on the waterfalls; an Enhanced water option follows | Remnant waterfall base and puddles shimmer |
| 12 | Time to kill | Damage / health equal the original; bot accuracy was too high and is now data-tuned | Medium bots: you survive longer; Hard noticeably sharper |
| new | Lens flares (opt-in) | Settings > Graphics > Lens Flares: Off / Subtle / Original | Look at the sun on Rust / Debris in each setting |
| new | Online private match | Host shows a code; a second PC joins by it | Two-PC test per PACKAGE_7_TWO_PC_TEST.md (menus now) |
