# Identity row: 1e11edc2 (Gameplay death presentation patch 2) vs af4d0731 - Experimental, 2026-10-17 14:3x

Berth (502) TDM 31 v 32, seed 7, eye-pass lockstep frames (shot every 60 steps 300-1800), Off and Faithful (RT 1), 2560x1440; both
exes on one frozen render-data snapshot (work/matb/id1e11_root). Same match: SIMHASH identical (6,001 steps) in both modes.

| step | changed px (Off) | where | class (death-frame list) |
|---|---|---|---|
| 300-660 | <= 0.1 % | - | identical (floor) |
| 720-1080 | 5.8-34.7 % | around robots dying in front of the player: the explosion / smoke / debris differ | death effects / gib / picker |
| 1140-1380 | 74-80 % | the player is destroyed ("Detent destroyed Player"): the death camera frames a different view (the killer / wreck) | death camera / killer cam |
| 1440 | 0.0 % | respawned, back to the same view | identical |
| 1500-1740 | 0.8-8.7 % | local regions at bot deaths (blast / debris) | death effects / gib |
| 1800 | 0.1 % | - | identical |

Off and Faithful agree frame for frame (21 / 26 frames changed in each, 26.9 % mean in the changed frames). Every changed frame
sits on a death moment in view (a bot's death effect, or the player's own death camera); frames without a death in view stay at
the floor. Verdict: **PASS** - changes confined to the expected-change list (picker, gib, camera, killer cam, effects).
Evidence: work/id1e11/sheet_502.png, review.png; frames in work/eyepass/id1e11_{before,after}/502-{off,faithful}/shots/.
