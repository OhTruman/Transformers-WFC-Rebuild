# Package 5: integration 398b27ef (2026-10-16)

Launch with Play.bat (maps the package to W: on the dev PC). Package: work/playtest_slim (size filled at build). Fallback: work/playtest_slim_30b2fa74 (package 4). Tag: playtest/09c-package5-398b27ef.

Reboot before a long session: the AMD driver's nonpaged pool grows with GPU load (9.4 GB after two days of lane work) and only a reboot clears it.

## New since package 4 (30b2fa74)

- **Ray-tracing plan v2, items 1-5 (Realistic).** Robots and movers are lit by the ray-traced world through sparse RT probes (traced under the loading screen, 0.75-1.8 s per map; WFC_RT_GIPROBES=0 off). Up to 4 lamps per frame cast ray-traced shadows on robots and robots cast lamp shadows onto the world (WFC_RT_LIGHTSHADOWS=0 off). Reflections on every physically reflective surface with a roughness-aware Fresnel: the Molten platform seam and the grazing dark rim lines are gone, reflection hits read the RT lightmap, and robots appear in reflections (previous-frame image at the hit). Dynamic lights light the world with RT shadows on the 4 strongest. Soft lamp shadows in the RT lightmap (0.15 m sources). Realistic cost at 1440p with 63 bots: p50 7.5-9.8 ms, p90 9.5-12.0 ms (see docs/handoff/RT_PERF_BASELINE_398b27ef.md, the new reference series under WFC_NOINTERACT).
- **Realistic GPU memory per map change fixed.** The runtime lightmap's shared pages are pooled across maps (the driver kept charging freed pages); the counter is flat after the first cycle. Package 4 known issue closed.
- **Effects and characters, all three cases from the 2026-10-14 report.** Weapon smoke, flashes, sparks and dust (material-less particles) are sorted with the other translucency after the characters and get a 0.5 m soft depth fade, so smoke veils a robot standing in it and no longer cuts hard lines into bodies or floors. The Leader's barrier renders as the original's see-through additive force field (its export had no material link; the package overlays the corrected glTF) and anything behind the grid is veiled by it (translucent ability meshes draw after the characters).
- **Muzzle flashes and deaths light the scene (all modes, as the original).** Every gun that lit up when fired in the original casts its authored muzzle light on the world and the characters, for you, bots and vehicle weapons; car / truck / tank deaths glow purple for 8 s; robot deaths from guns or explosions ramp a white light over 2 s (melee, energy and headshot deaths stay dark: an approximation of the original's death-type choice). Realistic shadows the 4 strongest. A view-cone cull drops lights that cannot reach the view. WFC_NOTRANSIENTLIGHTS=1 off; WFC_TLIGHTSCALE=<f> scales brightness (default 1).
- **Victim death sounds** for participants killed through the match damage path (the victim was despawned before its death presentation fired).
- **Smart bot vehicles right themselves when tipped** (the original's AI hover righting at the authored limits, with a contact-relative gate so ramps do not trigger it). Smart tipped time over seeds 1-3: 230 -> 76.5 s. Classic unchanged and byte-exact.
- **HD textures: the complete faithful world.** D snapshot b0-31: 3,505 textures, 21.27 GB (the rest of the 3,771 selection stays original by eye: sky / glow / FX gradients, faction logos, flares). Replaces b0-15 (1,875).
- **Package overlay:** 85 weapon / prop skeletal glTFs ship with their material links (extras.wfc_material), verified against FROZEN.json at build time; ExtractedAssets untouched.
- Harness-only: WFC_NOINTERACT (automated runs never take the mouse or focus), WFC_ASYNCJITTER, lockstep window-input discard, WFC_RT_GI* switches, WFC_MUZZLELOG, WFC_BOTTIP / WFC_TIPREL.

## Known issues

- **Transient lights cost at RT off:** +0.03 to +0.23 ms p90 per map (63-bot lockstep), the per-pixel light loop in the lit world pass; nothing else regressed. The real fix (tiled light lists or per-vertex world evaluation) is Rendering's next item.
- **Vehicle muzzle lights saturate nearby walls** (brightness 25, 5 m radius are the original's authored values, verified; UE3 clipped the same way). If it reads too bright in play, WFC_TLIGHTSCALE or a tone-down is a value change.
- **Smart tip-over cost:** Gorge stucks 1.26 -> 1.44 and Streets off-mesh 3.85 -> 4.91 per bot-match beyond a weak noise yardstick; the next Smart round's target, with the squad follow reversals.
- **Squad tactics:** members adjust position around a walking leader (follow reversals); squadTactics=1 (movement-only) or 0 (off) in the Smart tune keys.
- **Sentry turret material:** WEP_DeployedTurret_MATINST is compiled in no map, so the turret draws with a plain fallback surface (58 such weapon / prop materials listed for Rendering's matc pass).
- **Death lights are an approximation** of the original's per-death type choice; the real picker is a campaign-phase item (docs/handoff/CAMPAIGN_BACKLOG.md).
- **Enhanced materials row:** the M6 roughness maps (498, reviewed) exist but the Modern calibration (plan v2 item 6) is not in this package; Enhanced = package 6.
- **Lightmap super-sample** (plan v2 item 7) deferred: needs a per-sub GI array mapping to stay within the VRAM budget.
- **Driver nonpaged pool** grows with GPU load (AMD); reboot before a long session.

## Gates (final head 398b27ef)

- regress-8map 30b2fa74 vs 398b27ef, 3 arms x 8 maps (RT off, Classic, lockstep): the lights-off arm within -0.10..+0.04 ms p90 of base on every map (nothing else regressed); lights cost +0.03..+0.23 (limit +0.35). PASS under the re-based rule.
- Off / Faithful identity vs 30b2fa74 (Rust, Classic): SIMHASH identical; differences only where effects overlap characters (sort + soft fade), the barrier, and the transient lights (baseline changes in all modes); NOINTERACT on-arm determinism 25/25 identical; the cull floor changes only faint light edges.
- R4 leak rows: GPU dedicated plateau ~2.7 GB over 5 map changes (pool fix).
- verify_slim -SubstDrive (R4-aware RT gate, SETTINGSTEST, -ForeignCwd, the overlay row) and the FAST gate: filled at build.
