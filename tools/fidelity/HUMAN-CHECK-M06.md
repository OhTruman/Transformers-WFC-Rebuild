# Human check: Milestone 06 (`integration/milestone-06` 95edd7b)

Build: `work/ab/m6int/build-release/bin/wfc_rebuild.exe`, render data `work/ab/m6int/work/render`
(or Integration's own build of 95edd7b). Play with a real keyboard / mouse **and** an Xbox controller, at the
monitor's native refresh rate. Each item names what automation already measured, so only the judgement is left.

| # | check | what automation measured | what to judge |
|---|---|---|---|
| 1 | **Intro sync** | 4 / 4 movies with video + one audio stream each; audio clock trails video at the end by 0.42 / 0.10 / 0.10 / 0.15 s (Activision's first presented frame is 0.20 s into the movie); skip stops sound in the same frame; title music only after FMV_intro | lip / hit sync in FMV_intro; does Activision start abruptly? |
| 2 | **Menu / background look** | title, main menu, party lobby, host options and the frontend after a return are drawn (≤ 1% black, 0% flat grey) | does it look like the shipped UI_FrontEnd / lobby scenes? |
| 3 | **Keyboard / mouse and controller feel** | key / `ui:` paths through every screen pass; PC SKU paths pass; pad input cannot be injected | full route on the pad; Esc / Start to pause |
| 4 | **Character selection** | UI selection works; Gameplay receives it (`chassis=Jet4` for Scientist); the loaded body is still Optimus `robot.glb` / `vehicle.glb` with the IonBlaster | confirm the drawn body / weapon are always Optimus |
| 5 | **HUD look** | Hud_GFX drawn; team score 0/0 → 1/0 → 1/1 follows Gameplay; no round clock visible in TDM play | layout / clock vs the original |
| 6 | **Kill-feed stacking** | **reproduced**: two `GameMessage` rows drawn on top of each other (`hudprobe/crop_feed.png`) | how it reads in play |
| 7 | **Pause / resume** | **reproduced**: after Resume (Accept, Back or Enter) the pause menu stays drawn over live play until the match ends | does real Esc / Start behave the same? |
| 8 | **Animation smoothness at high refresh** | CAMSYNC per-frame camera 0.0003° at 60 / 144 / 240 Hz; product CAMLOG at 144 Hz: 25% of frames above the pacing threshold (mean 0.0034 vs 0.0095 broken reference) | robot idle / walk / strafe / turn / fire-while-moving / transform / vehicle / camera rotation at 144 and 240 Hz |
| 9 | **Robot movement feel** | lockstep: no displacement spikes, no frozen / double animation steps | weight, acceleration, turning |
| 10 | **Vehicle movement feel** | hover 15 m/s, boost 28.8 m/s, stable boost (0 oscillations) | handling, collisions |
| 11 | **Boost / nitro feel** | nitro 3 s / 8 s cooldown / ×1.5 speed / ×0.3 steering per RE | does it feel like WFC |
| 12 | **Transform feel** | transform every 5 s on 8 maps: no KillZ falls | snap, camera, sound |
| 13 | **Smoke / glass / translucency** | Streets 131 / 131 visual measurements unchanged vs M05 | veils and glass on every map |
| 14 | **Each map's visual completeness** | contact sheets `sheet_<map>.png` (28 views) and `tdm_<map>.png`. Look at: **Gorge** (11 / 28 views mostly black; large solid-black geometry at (−72.8, 12, −102.5)), **Rust** and **Remnant** (dark corridors), **Debris** (strong single-hue casts, one near-white view), **Berth / Seed / Gorge / BrokenHope** overviews (flat beige / cream sky) | which of these are the original's look |
| 15 | **Collision that feels wrong** | locations in `MAP-MATRIX.md` (Gorge start 19 stop, pass-throughs per map) | drive / walk there |
| 16 | **Audio field / announcer** | DM_START, final stretch at 5 kills left, DM_END by winner, 5 Optimus announcer lines per match on every TDM map; 0 doubled cues over 17 matches | mix, positioning, announcer timing |
| 17 | **Second match / returning to menus** | 17-match chain over 8 maps in one process completes; lobby after a match shows index 0 (Seed), as BLK C7 predicts | anything stale after several matches |
| 18 | **Anything that does not look like shipped WFC** | — | free play on every TDM map |
