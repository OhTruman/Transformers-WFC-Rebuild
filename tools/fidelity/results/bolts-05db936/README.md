# Scatter Blaster bolt check: aa5a74c (old FX) vs 05db936 (new FX)

Rendering's run reproduced (frontend TDM Streets 5+5, lockstep, autofire, turn 0.4, shot at 12 s). INCONCLUSIVE: both frames show
near-identical grey bolt streaks; the player view looks down a corridor with no wall a few metres ahead, so wall
termination is not visible. Asked Rendering whether the spawn / view differs from their run.

## Outcome (Rendering, 2026-10-07)
Confirmed by Rendering's own A/B: the collision data only shortens some thin bright bolt streaks; the thick grey streaks
are a separate smoke-trail-like ribbon emitter and are UNCHANGED, so the "grey rods fixed" claim was an overclaim. OPEN with
Rendering: identify that emitter and compare its width / lifetime / opacity against the original authored values.
