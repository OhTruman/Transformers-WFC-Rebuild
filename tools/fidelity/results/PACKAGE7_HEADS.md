# Package 7 - per merged head identity ledger (Experimental; design: tools/fidelity/PACKAGE7-GATE.md)

Checkpoint: af4d0731 (package 6). Per head: identity + eye sheet; one combined timed gate on the package 7 head.

| head | merge | src diff vs previous head | identity | eye sheet | note |
|---|---|---|---|---|---|
| a2ee795b | Systems: local death sound (TnPlayerController.DeathSound) | src/game/AbilityAudio.{h,cpp}, World.{h,cpp}: +14 lines - a cue start and a diagnostics counter / WFC_DEATHAUDIOLOG line on the local player's death; no renderer, no sim state | frame-identical by construction (Integration 2026-10-17; diff read by Experimental); headless SIMHASH identical; paced Rust run PASS (Systems) | not needed (audio only) | no run |
| 3a19b47a | Frontend session boundary | headers + tests; nothing reads it | frame-identical by construction (Integration) | not needed | no run |
| 72eec90c | Settings > Graphics: Volumetric Lighting row + pc_strings.int | UI only; the row stays hidden until Rendering's setter merges | frame-identical in game by construction (Integration) | not needed | SETTINGSTEST count change arrives with the setter (f2465a80) |
| be0cac77 | Camera.h roll | roll 0 = byte-identical view matrix | frame-identical by construction (Integration) | not needed | no run |
| 1e11edc2 | Gameplay death presentation patch 2 | presentation only; seed-7 SIMHASH identical (Gameplay) | **PENDING (Experimental, card):** one Off + one Faithful lockstep pair, base vs head, Berth 63 bots; diff frames classed against the death-frame list (picker, gib, camera, killer cam, effects) | Gameplay's crop sheets, docs/handoff/corpsep2/*.png | after the presenter loop unless a gap opens first |
| 0960cd35 | WFC_FRONTEND_AUTOPLAY waits 2 s in the game lobby before SetSelectedMapID (FrontendRuntime.cpp) + regression test | harness-only (the autoplay script) | frame-identical in game by construction (Integration) | not needed | fixes the selector race Experimental found (af4d0731 autoplay landed on 501); package 7 gate frontend boots run on a head with it, so load-stall.ps1's own script is only needed on older exes |
| eebe15b2 | campaign flag + <Strings:> data store | answers "" in versus as before | versus unchanged by construction (behind GameFlow::campaign()); headless tests 165 / 0 (Integration) | not needed | no run |
| b79692b6 | campaign objective path | behind GameFlow::campaign() | versus unchanged by construction | not needed | no run |
| fedf83ee | campaign tutorial messages (once per profile) | behind GameFlow::campaign() | versus unchanged by construction | not needed | no run |
| f2465a80 | Volumetric Lighting setter | Realistic-only gate | Off / Faithful unaffected by construction (Integration); Realistic: X3 on by default as af4d0731 | - | SETTINGSTEST count update and release() keep-list check due with the setter (memory settings-survive-map-change) |
