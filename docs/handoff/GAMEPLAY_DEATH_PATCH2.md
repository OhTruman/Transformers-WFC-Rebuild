# Death-body patch 2: the original's death presentation (package 7)

Gameplay, 2026-10-17. Base: integration af4d0731 (+ Rendering e5fc6d65 for the camera roll field). Edits in `docs/handoff/corpsep2/`,
applied in this order from a tree root (new files in `corpsep2/new/`: `src/game/GibRagdoll.{h,cpp}`, `src/game/GibRagdollTable.inc`,
`tools/gameplay/gen_gib_ragdoll.py`):

| # | Edit | What |
|---|---|---|
| 1 | `edit_scorch.js` | The DeathDecal_MAT scorch under gibbed robots |
| 2 | `edit_momentum_picker.js` | The killing hit's TearOffMomentum direction at the 8 real hit sites; the picker reads it |
| 3 | `edit_death_camera.js` | The Death / Death_Vehicle camera strategies |
| 4 | `edit_death_camera_roll.js` | The camera roll (needs e5fc6d65) |
| 5 | `edit_faithful_picker.js` | Four momentum types, ShouldInstaGib, per-entry custom gibs, the light cut with the pawn |
| 6 | `edit_skeletal_gibs.js` | The robot skeletal gib ragdoll |
| 7 | `edit_killer_cam.js` | The killer cam (RE KILLER_CAM_RE): PlayerSpectating after 3 s on SpectateOverTheShoulder (orbit 16 m, FOV 80, free look), hard cuts, target cycling LB / ']' and RB / '[' (new Buttons SpectateNext / Prev), the 1 s recheck, the respawn cut. [PROV: target order by match player index; bots are valid killer targets; no LevelCamera fallback] |

| 8 | `edit_death_fx.js` | Systems' per-entry death effects (DeathFx.inc, agents/systems 409b7f39 → `corpsep2/new/DeathFx.inc` → src/game/) |
| 9 | `edit_death_sockets_shake.js` | The effects at the authored sockets (kDeathSockets) + TnPawn.GibbedCameraShake (RE 51cca62) |

| 10 | `edit_deathfx_log.js` | DEATHFX diagnostics (WFC_CORPSETEST / WFC_DEATHFXLOG) for Systems' FX-count row |
| 11 | `edit_default_gibs_socket.js` | The default death's gibs effect at PositionSocket |

**Systems' FX-count row: PASS** on 8e4ecc6b (seeds 1 + 7): every entry matches its DeathFx row; 209 / 215 play effects at the authored
socket, and the 6 fallbacks are deaths mid-transform. The default-death placement was fixed after it, in a208d3b0.

**PROV item 2 (PokeGibs_DSYS) is CLOSED** by edit 8: TF_Death_MED_FX only, no chunks / skeletal gib / decal.

Headless after all nine: BOTTEST 48 / 48 × 12, seed-7 SIMHASH identical (agents/gameplay fec7ca8a).

Headless after all seven: BOTTEST 48 / 48 × 12, seed-7 SIMHASH identical to af4d0731 (agents/gameplay ebd359cf).
Systems owns the death audio / FX hooks (call sites sent 2026-10-17).

## Sources

All CONFIRMED RE unless marked, plus authored.db (read only):
- `GAMEPLAY_DEATH_PICKER_RE.md`;
- `GAMEPLAY_SKELETAL_GIB_INIT_RE.md`;
- `DEATH_CAMERA_RE` (RE lane);
- `corpsep2/SKELETAL_GIBS_AUTHORED.md`, `corpsep2/DEATH_CAMERA_AUTHORED.md`, `corpsep2/MOMENTUM_PICKER.md`.

## Behaviour

- **Picker:**
  - the first authored entry whose types are all active, with no scoring and no randomness;
  - momentum is Forward / Left / Right / Backward at ±45° in pawn space, with zero momentum → Forward;
  - the entry's modifiers start; then ShouldInstaGib: KillZ, BruteBackpack, DeathAnim always, EmpShotgun by another pawn within 10 m.
- **Gib:**
  - the default gibs (Exp_Death_FX + 4 RB_Gib chunks) + **one skeletal gib ragdoll** (GIB_Body / GIB_LowBody2, 50 / 50, at
    PositionSocket in the ref pose with the pawn's rotation, at rest, world collision only, life 5-15 s) + **the scorch**
    (8-9.5 m, 30 s);
  - **Melee:** none of these (the Defrag dissolve alone). **Poke:** no skeletal gib or decal.
- **Light:** the picked entry's light at the pawn origin, cut 1 s after the gib (the pawn's LifeSpan 1).
- **Local death camera:**
  - the live orbit frozen at death, plus the anchor offset, on the corpse bone (frozen at the gib);
  - vehicles: the wreck yaw, pitch −30;
  - FOV 85; the 1.25 / 1.0 s blend; the wiggle and roll; camera collision; no stick input; held until respawn.

## Provisional (PROV) and not modelled: carried to the package 7 doc

1. **Martyrdom:** the entry needs TnBuffExplodeOnDeath / Kamikaze, a buff the rebuild's multiplayer doesn't have. The entry is never
   active (unchanged from before).
2. **PokeGibs_DSYS:** the Poke entry's custom gibs aren't read yet. The default gibs stand in (the skeletal gib and decal are
   correctly absent).
3. **The WFC_NOCORPSE harness light:** with no body (the perf floor arm, or a pawn without a valid model), the light still uses the
   older damage-type guess instead of the picked entry. Harness only; real play takes the picked entry.

Also PROV, by design:
- **The ragdoll solver:** XPBD, 8 substeps, plane contacts per 60 Hz step; UE3 default friction 0.7 / restitution 0.3 / damping;
  isotropic inertia; the soft neck / head limits at a 0.3 correction; gravity 9.8 as the existing chunks. PhysX 2 itself is not
  reproduced.
- **Swing1 / swing2 axis assignment:** swing1 about the third axis, swing2 about the secondary (UE3 PhAT convention, HIGH).
- **The wiggle waveform:** a sine, as the existing vehicle wiggler. The 0.4 UU wiggle translation is omitted (below a pixel).
- **Killer cam:** after 3 s the original spectates the killer. That's its own item, next after patch 2. Until then the death camera
  holds to respawn.

## Gates

- **Headless:** SIMHASH identical (all presentation); BOTTEST 48 / 48.
- **Windowed crop slot:**
  - the scorch;
  - the ragdoll (both templates), plus a check that it settles and sleeps;
  - the death camera (robot / vehicle, the roll sign against the reference);
  - a melee death (no gibs);
  - a picker log (WFC_CORPSETEST entries: splash kills change, gun kills mostly don't);
  - insta-gib (KillZ).
- **Then the perf levers** (visually lossless): the corpse arm.
