# Package 7 - per merged head identity ledger (Experimental; design: tools/fidelity/PACKAGE7-GATE.md)

Checkpoint: af4d0731 (package 6). Per head: identity + eye sheet; one combined timed gate on the package 7 head.

| head | merge | src diff vs previous head | identity | eye sheet | note |
|---|---|---|---|---|---|
| a2ee795b | Systems: local death sound (TnPlayerController.DeathSound) | src/game/AbilityAudio.{h,cpp}, World.{h,cpp}: +14 lines - a cue start and a diagnostics counter / WFC_DEATHAUDIOLOG line on the local player's death; no renderer, no sim state | frame-identical by construction (Integration 2026-10-17; diff read by Experimental); headless SIMHASH identical; paced Rust run PASS (Systems) | not needed (audio only) | no run |
