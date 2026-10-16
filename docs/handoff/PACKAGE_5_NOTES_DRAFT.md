# Package 5: DRAFT notes (build target a5f53923, pending the D freeze; Systems draft 2026-10-15 from the milestone entries after 30b2fa74)

Items marked *(pending)* are not merged or not measured yet; Integration fills the head, size, HD snapshot and gates
when package 5 is built. Launch as before: Play.bat (maps the package to F:, or W: on the dev PC).

## New since package 4 (30b2fa74)

- **Characters lit by the ray-traced world (Realistic).** Robots and movers now take their light from the RT
  lighting through sparse RT probes, so a character next to an RT-lit wall matches it (package 4 known issue
  closed). Traced under the loading screen (0.75-1.8 s per map). On by default in Realistic; WFC_RT_GIPROBES=0 off.
  (Rendering 12bda047, merged df132b5a.)
- **Lamp shadows on characters (Realistic).** Up to 4 lamps per frame cast ray-traced shadows: a lamp behind a wall
  no longer lights a robot, robots shadow themselves, and robots cast lamp shadows onto the world.
  WFC_RT_LIGHTSHADOWS=0 off. (12bda047.)
- **Realistic GPU memory per map change fixed.** The runtime lightmap's GPU pages are pooled and reused across maps
  (the driver kept charging freed pages, ~275 MB per change); the counter is flat after the first cycle
  (Molten -> Rust -> Streets x 2: 1632-2468 MB, was 1818 -> 3184). Package 4 known issue closed. (Rendering 77d9e02c,
  merged 00aa2ca7.)
- **Smoke and weapon effects no longer drawn behind characters.** Tracer smoke, impact / explosion / grenade clouds,
  sparks and dust (and vehicle emitters without a material) now depth-test against every character, local or bot:
  smoke in front of a character veils it, smoke behind stays hidden (user report 2026-10-14). (Systems, 56290b7d.)
- **Barrier grid** *(pending: merges after Rendering's verification crop)*: the Leader's barrier renders as the
  original's see-through additive force field (it drew a solid fallback surface: the export had no material link),
  and characters behind it are no longer drawn over it (user report 2026-10-14). (Systems SYSTEMS_FX_ORDER_3A.patch,
  4adf8e9c.)
- **Muzzle flashes and deaths light the scene.** Every gun that lit up when fired in the original (46 weapon classes) now casts its original muzzle light (radius, colour, falloff, 0.1-1 s) on the world and the characters, for you, bots and vehicle weapons; car / truck / tank deaths glow purple for 8 s and robot deaths from guns or explosions ramp up a white light over 2 s (an approximation of the original's death-type choice; melee, energy and headshot deaths stay dark). Realistic shadows the 4 strongest lights; Faithful / Off light unshadowed. WFC_NOTRANSIENTLIGHTS=1 off. (Systems SYSTEMS_MUZZLE_LIGHTS / SYSTEMS_DEATH_LIGHTS + Rendering item 4, merged 76b077e8; check docs/handoff/transient_lights.)
- **Smart bot vehicles right themselves when tipped** (the original AI hover righting at the authored limits; ramps
  do not trigger it). Smart tipped time over seeds 1-3: 230 -> 76.5 s. Classic unchanged and byte-exact (NAVQ 24/24).
  (Gameplay, 069a0a8f.)
- **HD textures** *(pending)*: D snapshot b0-31 (AssetTools' overnight freeze), replacing b0-15.
- Harness-only: WFC_RT_GIPROBES, WFC_RT_LIGHTSHADOWS, WFC_NOTRANSIENTLIGHTS, WFC_MUZZLELOG, WFC_BOTTIP / WFC_TIPREL (measurement overrides).

## Known issues

- **Effects cut hard into characters and walls:** the game-side particles (weapon smoke, sparks, dust) have no soft
  depth fade where they meet a surface, unlike the original's particle materials (Rendering option B: the fallback
  batches join the sorted translucency with a generic material and depth-biased alpha).
- **Sentry turret material:** WEP_SentryDeploy_SKEL's WEP_DeployedTurret_MATINST is not in any map's compiled
  materials, so the turret draws with a plain fallback surface (AssetTools export item; the barrier's equivalent
  gap is covered by the game-side name fallback until the export carries extras.wfc_material).
- **Smart tip-over cost:** Gorge stucks 1.26 -> 1.44 per bot-match and Streets off-mesh 3.85 -> 4.91, beyond a weak
  noise yardstick; target of the next Smart round.
- **Realistic frame cost** (1440p, 63 bots, p50 vs package 4): Rust 7.77 -> 8.04, Debris 6.99 -> 7.10, Streets
  8.28 -> 8.66, Molten 8.68 -> 9.09, Gorge 7.96 -> 8.48 ms (p90 max 10.55). Off and Faithful unchanged.
- **Reflection artefacts (Realistic / Faithful)** *(carried over unless plan v2 item 3 lands)*: a seam on Molten's
  central platform floor and thin dark lines along bright rims at grazing angles.
- **Death lights are an approximation:** the original picks a death type per death (also from the victim's motion), so a few deaths light differently (e.g. jet / truck rocket kills of robots stay dark); the real picker is a campaign-phase item.
- **Driver nonpaged pool** grows with GPU load (AMD); a reboot before a long session clears it.

## Gates *(pending: filled at build time)*

- regress-8map 30b2fa74 vs package 5 head; Off / Faithful identity (smoke-order and barrier changes are a baseline
  change in all modes wherever effects overlap a character; SIMHASH unaffected).
- verify_slim -SubstDrive -SettingsExpected 7 -ForeignCwd with the R4-aware RT gate.
- FAST gate (audio: instances / level cues after an unload; raw voices informational).
