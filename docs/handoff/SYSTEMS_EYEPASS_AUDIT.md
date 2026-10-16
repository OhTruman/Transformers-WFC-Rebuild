# Eye-pass audit: effects and audio on the five user maps (Systems, 2026-10-16)

Build: integration bb5ac029 (Release + PGO, RT off). Runs: tools/systems/eyepass_run.ps1 - per map one lockstep TDM 4 v 5 +
playerbot, seed 7 / flow 7, 70 s, windowed, muted (the mix / cue / voice logic still runs), NOINTERACT, with WFC_LOGTIME,
WFC_CUELOG, WFC_AUDIOTIME, WFC_MUZZLELOG, WFC_LEVELAUDIOLOG, WFC_MATCHAUDIOLOG; captures every 600 steps. Analyser:
tools/systems/eyepass_audit.py (per-map json: work/eyepass/audit.json). "Step" = match step at 60 Hz from MATCH start
(Streets 35.24 s, Rust 26.14, Molten 21.16, Gorge 19.20, Debris 19.00 in each log).

Severity: HIGH = visibly / audibly wrong in normal play; MED = noticeable in some situations; LOW = rare or cosmetic;
PASS = checked and correct.

## Findings

| # | item | maps | evidence (seed 7, step) | severity | owner / fix |
|---|---|---|---|---|---|
| 1 | CORRECTED: the local player's idle / reload sounds were refused ("no channel") only in the lockstep harness, which runs the match 3-5x faster than real time while the mixer drains voices in real time (238 local SHOOT layers in 0.9 s of log time). Paced real-time runs (lockstep + WFC_FRAMELIMIT=60, identical matches): the local player is never refused, the pool reaches 96 in 1-3 of 160 samples | Streets, Gorge | work/voice_ab_paced | LOW (harness artefact) | Fix kept for fidelity: the original's FmodAudioDevice SilentChannelCullingThreshold (-64 dB) - voices that quiet at the listener are virtual (SYSTEMS_VOICE_CULL.patch): Streets refused 3 -> 0, stolen 116 -> 76; Gorge refused 7 -> 3, stolen 39 -> 27; SIMHASH identical. Audit runs must use paced lockstep for audio counts |
| 2 | Homing-rocket explosion embers do not fly: Squib_HomRocket_01_FX emitter "Embers_Dup_Dup" has PMI_VelocityOverLife with no decoded VelOverLife, so the runtime uses 0 ("map fx: PMI_VelocityOverLife has no decoded VelOverLife (0 used)") and the embers' velocity is scaled to 0 | all 5 | first rocket hit per map (Streets step ~44, 35.98 s) | MED | AssetTools (pstream decode of that distribution) / Rendering (fallback 1.0 instead of 0 for a missing VelOverLife, so they at least move) |
| 3 | (FIXED: SYSTEMS_IMPACT_NORMAL.patch) One impact squib per map not drawn: renderer.spawnParticleEffect returns -1 when the impact frame is degenerate (fxRows fails: a zero-length normal, shot.to == shot.from). The warning text ("Impact_AssaultRifle_FX is not reconstructed - not drawn") is misleading: the template is loaded and every other impact draws | Streets, Rust, Gorge, Debris (1 each) | Rust 30.02 s, Streets 36.81 s, Gorge 22.38 s, Debris 21.32 s | LOW | Systems / WeaponFx: fall back to the shot direction when the normal is zero; reword the warning |
| 4 | Vending-machine Ammo / EnergonCube materials fail the runtime GLSL compile (UBER line 94 'constructor' : too many arguments) and keep the glTF fallback | all 5 (load time) | map load | LOW (campaign-style props; already relayed) | Rendering (matc) |
| 5 | GPU frame spikes logged (1-7 per map, RT off, windowed) | all 5 | log "GPU frame spike" lines | perf, not eye-pass | Experimental / Rendering |
| 6 | Debris "glb: no triangles baked from decals.glb" | Debris | load | PASS (faithful) | the original Debris has no DecalActors (0, vs 24-25 on Streets / Molten); silence the warning |

## Checked and correct

| check | result |
|---|---|
| Muzzle flash vs fire sound (local player) | PASS: 574 shots on 5 maps, every MUZZLE paired with its SHOOT / SHOOT_LOW_AMMO cue; median 0 ms, max 16 ms (< 1 frame). The 4-6 "late" pairs per map in the first pass were low-ammo shots firing SHOOT_LOW_AMMO (the original's WP_LowAmmoFire cue), correct |
| Late sounds (streamed decode) | PASS: 1 streamed cue per map, no main-thread decode wait > 5 ms |
| Missing sounds | PASS: no "sound cue ... not in table", no localized wave skipped |
| Level audio / Kismet audio ops | PASS: none skipped or unapplied |
| Reverb zones | switching observed (6-15 reverb switches per map as the playerbot moves; e.g. Streets Default -> REVERB_TRANS_MP_STREETS_AUTO_ROOM_01 over 0.25 s); a missed zone is not detectable from the log (needs pawn position vs volume) - not verified beyond that |
| Effect guard drops (pop-outs from the instance / particle budget) | PASS: no "fx guard" line on any map (4v5) |
| Effects missing from the map data | PASS: no "template ... not in this map's render data" line |
| Particle LOD | faithful: map FX switch LOD by camera distance exactly as UE3's Automatic LOD (no blend), so an LOD step can pop as in the original. A Realistic-only crossfade would be a deviation (not proposed by default) |
| Effects vs characters ordering (option B / 3a / 3b) | not re-checked here (logs cannot show ordering); the staged cases (smoke on / in front of a bot, muzzle flash over a bot, the barrier grid) were verified on 56290b7d / 3a; a fresh crop pass needs the eye pass itself |

## Not covered (needs eyes or a slot)

- Pop-in / pop-out timing of individual particles (no log); the per-600-step captures (work/eyepass/<map>/shots) are for spot checks.
- Realistic-only effects (the RT-era effect lights / headlights) - Rendering is calibrating from the A/B crops.
- 31 v 32: voice starvation (item 1) and effect guards would be worse; a 63-bot run of the same tools is the next check if wanted.

## Follow-ups (2026-10-16)

- Mine detonation (Exp_2500cb_FX) renders: a staged spawn through fxStepSpawn(explosion) in front of the camera shows the flash / fireball / sparks / smoke over ~0.4 s (docs/handoff/eyepass/exp_2500cb_staged_rtoff.png); Gameplay's empty frames were a timing / position question (frames 808-860 vs the spawn at step 1625; a mine at or below the floor is depth-cut).
- Audio counts from lockstep runs are inflated (the sim outruns the real-time mixer); tools/systems/voice_ab.ps1 -Paced (lockstep + 60 fps cap) gives deterministic real-time numbers.

## 31 v 32 rerun (paced, 2026-10-16)

Build: 06110ef4 + SYSTEMS_IMPACT_NORMAL + SYSTEMS_VOICE_CULL (= what integration merged), RT off, seed 7, 70 s,
`eyepass_run.ps1 -Paced -Bots 'BotsFriendly=31?BotsEnemy=32?ExtendedPlayers=1'` (without ExtendedPlayers=1 the URL is clamped to
the original 5 v 5 - 9 bots). 63 bots confirmed on all 5 maps. Deltas vs the 4 v 5 audit only:

- Effect guards: still 0 on every map (13-18 k cue lines per map, no "fx guard").
- Muzzle / fire-sound sync: 509 local shots, all paired, max 1 ms (unchanged).
- Late / missing sounds, Kismet audio, map FX data: unchanged (PASS).
- Impact squib (item 3): no "not spawned" line on any map (fixed).
- Voice pool (voice_ab.ps1 -Paced, Streets / Gorge, base 06110ef4 vs patched): the 96-channel pool IS saturated at 63 bots, but
  only for distant bot / world sounds (lowest priority, quietest first); the local player is never refused.
  Streets: at-96 in 61 -> 33 of 160 samples, refused 1700 -> 1289, stolen 3550 -> 3065. Gorge: 81 -> 56, 2726 -> 2049,
  5299 -> 4935. The original never ran more than 10 players on 96 channels; whether 31 v 32 wants a bigger pool (e.g. 128) is
  an extension decision, not a fidelity one - not changed.
- Unchanged LOW items: vending GLSL (2 materials), Debris decals.glb warning, 1 frame-1 GPU spike per map (load).
