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
| 1 | The local player's own idle loop (BL_FOLY_IDLES.OPTIMUS_IDLE / IDLE_N) is dropped: "no channel ... all 96 busy with more important sounds". At the first drop ~47 of the 96 voices are held by the map's level emitter loops (BL_LVL_MP_IAC_STREETS: every event of every emitter, distant ones included) | Streets 14x, Gorge 1x | Streets step ~134 (37.47 s) first; Gorge 1 | MED | Systems / audio: voice priority by audible level at the listener (UE3 picks voices by volume x priority after distance attenuation, so inaudible distant ambient loops lose first); or virtualise level loops outside their radius. Needs a fidelity read of the 360 voice budget before changing |
| 2 | Homing-rocket explosion embers do not fly: Squib_HomRocket_01_FX emitter "Embers_Dup_Dup" has PMI_VelocityOverLife with no decoded VelOverLife, so the runtime uses 0 ("map fx: PMI_VelocityOverLife has no decoded VelOverLife (0 used)") and the embers' velocity is scaled to 0 | all 5 | first rocket hit per map (Streets step ~44, 35.98 s) | MED | AssetTools (pstream decode of that distribution) / Rendering (fallback 1.0 instead of 0 for a missing VelOverLife, so they at least move) |
| 3 | One impact squib per map not drawn: renderer.spawnParticleEffect returns -1 when the impact frame is degenerate (fxRows fails: a zero-length normal, shot.to == shot.from). The warning text ("Impact_AssaultRifle_FX is not reconstructed - not drawn") is misleading: the template is loaded and every other impact draws | Streets, Rust, Gorge, Debris (1 each) | Rust 30.02 s, Streets 36.81 s, Gorge 22.38 s, Debris 21.32 s | LOW | Systems / WeaponFx: fall back to the shot direction when the normal is zero; reword the warning |
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
