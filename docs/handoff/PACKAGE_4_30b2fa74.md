# Package 4: integration 30b2fa74 (2026-10-14)

Launch with Play.bat (maps the package to W: on the dev PC). Package: work/playtest_slim (16.60 GB, Release + PGO). Fallback: work/playtest_slim_c949ab88 (package 3).

## New since package 3 (c949ab88)

- **Whole-map ray-traced lighting (Realistic).** Sun, sky, every static light and bounced light are traced at runtime and replace the baked lightmaps on every surface, vertex-lit props included; calibrated to the bake per map (Streets 1.07, Gorge 0.96, Debris 0.95, Rust 0.98, Molten 0.93 of bake brightness). Converges in under a second after load, then costs 0 ms per frame. Soft lamp shadows (0.15 m sources). On by default in Realistic; Faithful and Off are byte-identical to package 3 (identity 5/5 + SIMHASH).
- **Visible RT reflections** (Fresnel layer on glossy surfaces, Realistic / Faithful; WFC_RT_REFLSTRENGTH / WFC_RT_NOREFL).
- **Molten shadow report closed:** actor-placed (mover / toggled) props stay out of the static RT scene; the hidden repair node that blacked out the central platform is gone.
- **Smart bots:** pathing set 2 (level with Classic within noise on all four user maps, fewer stucks); squad tactics on by default (travel wedge, firing line in fights, leashed errands). Classic byte-identical (24/24).
- **Per-page sun calibration** for the five user maps (vbake fit.json pages[]).
- **HD textures:** faithful HD pass snapshot b0-15 (1,875 rows, 11.0 GB). The generative overhaul was dropped (user verdict 2026-10-14).
- Harness-only: WFC_ASYNCJITTER, lockstep window-input discard, WFC_RT_GI / WFC_RT_GIPROBES / WFC_RT_GISWEEPS / WFC_RT_GIBOUNCE / WFC_RT_CENSUS.

## Known issues

- **Realistic VRAM per map change:** the runtime lightmap path leaves ~0.2-0.4 GB of GPU dedicated memory per map change until restart (the game's own DXGI view is flat; residency / driver caching suspected). ~10 map changes fill 3 GB of headroom on a 24 GB card. Fix targeted at package 5.
- **Reflection artefacts (Realistic / Faithful):** a hard vertical seam across Molten's central platform floor and thin black lines along bright rims at grazing angles (reflection-target sampling; plan v2 item 3).
- **Squad tactics:** members adjust position around a walking or waiting leader (reversals up, no stucks). Fallback: `squadTactics=1` (movement-only) or `0` (off) in the Smart tune keys.
- **Robots under RT lighting:** characters still use the original light environment next to RT-lit walls (plan v2 item 1, probe grid, next).
- **Driver nonpaged pool** grows with GPU load (AMD); a reboot before a long session clears it.

## Gates

- regress-8map c949ab88 vs 30b2fa74: 8/8 PASS, worst +0.01 ms.
- Off identity vs c949ab88 PASS; Faithful identity vs 0e7329cf PASS; Realistic lighting-only (sim identical).
- verify_slim -SubstDrive: all rows PASS (R4-aware RT gate, SETTINGSTEST 7/7, -ForeignCwd).
- FAST gate: 55 PASS; 4 flagged rows = stale harness constructions (the range-damage check builds a bare Weapon with no range points and reads 15 hp; the game deals the authored 7.5 hp at 300 m; 64 decal nodes is current since 480e3149; the rig reads a CPU pose empty since GPU skinning) and one GPU-contention frame spike; 2 voices after the pause -> Quit unload = the Quit popup BUTTON_ACCEPT click (frontend UI; map instances 0 / level cues 0).
- Lockstep divergence hunt: closed as monitored (RT-off and RT-on 31v32 jitter pairs identical).
