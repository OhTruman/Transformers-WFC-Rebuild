# Which Trail2 template can draw a blue electric-arc ribbon on a robot on Molten? (RE, 2026-10-18)

For Rendering's x14 Trail2 observation (Molten, Off mode: a large blue electric-arc ribbon on a robot that the base never drew).
Data: AssetTools `manifests/particles/trail2_templates.json` (Trail2 templates, compiled modules, references) plus authored.db
references. Labels: C = data; H = inference.

## 1. Look of every MP Trail2 template that is blue or electric (LOD0, C)

Colours: `ColorByParameter` default (the instance parameter can override it), or StartColor / ColorScaleOverLife (HDR scale).
Size: StartSize X = ribbon width (UU).

| Template | Material | Colour | Width | Lifetime | What spawns it in MP | On a robot? |
|---|---|---|---|---|---|---|
| **`Trail_FusionCannon_FX`** (FX_FusionCannon_p) | **Lightning_Anim_03_MAT** (animated lightning) | ColorParam default **(0, 50, 215)** blue, ColorScaleOverLife ×1.25–2.5 | **300** (× 25 × 25) | **0.35 s**, ConnectToSource, SpawnRate 10, EmitOnlyWhenMoving false | **weapon: Bazooka**: the **projectile trail** of the fusion-cannon round | **No:** it belongs on the projectile. If a runtime attaches a projectile trail to the firing pawn or muzzle, it appears on the robot. |
| `TF_Death_MED_FX` (FX_Explosives_p) | Streak_Mat | StartColor **(0.3, 0.625, 1.0) × 5**: bright light blue | 50 | 0.25 s, **SpawnRate 876** (dense streaks), 3 emitters | **Car / jet deaths:** `TR_Deaths_p.SharedCarDeaths` / `SharedJetDeaths` TnDeathModifierPlayEffect at **TargetingSocket**; the Martyrdom / Poke / SoldierCar gib destruction factories | **Yes**, at the moment of a car / jet-form death. A burst of blue streaks, not a lingering ribbon. |
| `TF_Death_Energy_Med_FX` | Streak_Mat | ColorParam (150, 200, 255) × 3–4 | 50 | 0.25 s | (map FX set only) | death effect family |
| `Exp_Energy_MED_FX` | Streak_Mat | StartColor (1, 0.5, 8) × 10: blue-violet | 10 | 0.075 s | FlashBang projectile explosion; Brute backpack destruction; rocket-socket notifies | brief, at the socket |
| `Drain_Blast_FX` | Streak_Mat | StartColor (1, 3, 10): blue | 10 | 0.25 s | `TnAbilityDrain` CDO | when that ability fires |
| `Code_of_Power_FX` (FX_Multiplayer_p, has "ElectricArcs" + "Trail" emitters) | Spark_01_MAT_INST | ColorParam (77, 77, 207) blue | 20–30 | **4 s** | **Campaign-placed Emitters only** (A3_COR_CoreInt, A4_ORB_CogRoom). On Molten it is only in the base-map ObjectReferencer (always-loaded list) | **No.** Nothing in MP attaches it to a pawn. |
| `Trails_Jet_D_FX` | Tracer_MOD_MAT | (89, 65, 255) alpha 50 | 7 | 0.6 s | jet chassis (Wing / VaporTrail sockets) | yes, jets; thin and faint |
| `Tracer_SniperRifle_FX` | Tracer_Smoke_MAT | (50, 25, 255) | 25 | 4 s | sniper tracer | no (tracer) |
| `Trail_PlasmaCannon_Sm / Med / Lrg_FX` | Tracer_ADD / MOD_MAT | (122, 89, 255) | 50 / 50 / 150 | 0.5 s | PlasmaCannon / rocket-turret projectiles | no (projectile) |
| `Trails_Bumblebee_FX` | Trail_Blur_MAT | white (1, 1, 1) × 0.9 | 20 | 2 s | car-chassis transform / boost / melee notifies (11 sockets) | yes, but white blur, not blue |
| `SwordTrail_Red_FX`, `Hammer_Trail_DCPT_FX` | Trail_Blur / Distort | white / distort | 40 / 20 | 1 / 0.5 s | melee notifies (WeaponTrailSocket) | yes, not blue |

Shells / magazines / ammo trails (Blaster_Trail_MAT_INST, width 10–15, default (255, 100, 100)) are small and red by default. The
team colour parameter can recolour them, but they are thin and short.

## 2. Verdict (H, reasoned from the data)

- **The only "large electric-arc ribbon" in the MP Trail2 set is `Trail_FusionCannon_FX`.** Lightning material, 300 UU wide, blue
  (0, 50, 215), 0.35 s.
  - It is authored as the **Bazooka projectile's flight trail**. In the original it rides the projectile; it is **not** a pawn
    effect.
  - If the x14 view shows it **on a robot** (a bot holding a Bazooka on Molten), the likely cause is a runtime attaching the
    projectile trail to the firer (muzzle / pawn) instead of the spawned projectile, or a projectile that doesn't move (trail
    drawn at the spawn point).
  - So it is **not an authored on-robot effect**. Check what owns that PSC.
- **Authored blue effects on a robot are short bursts, not ribbons:**
  - `TF_Death_MED_FX` at TargetingSocket on car / jet deaths (0.25 s dense light-blue streaks);
  - `Exp_Energy_MED_FX` / `Drain_Blast_FX` (≤ 0.25 s).
  - None persists as a ribbon.
- **Code of Power's electric arcs are never attached to MP pawns.** It's loaded on Molten only through the base referencer.
- **Shock / EMP hits:** the EMP / Tesla-style hit effects are **not Trail2**. The EMPShotgun trail is red smoke (Trail_Smoke_10,
  default (255, 65, 65)). So an electric arc from an EMP or shock hit wouldn't come through the Trail2 path.

## 3. To confirm on Rendering's side

Log the PSC's template and owner for the ribbon in the x14 frame:
- `Trail_FusionCannon_FX` owned by a pawn or weapon → a rebuild attachment bug;
- owned by a `TnProjectile` → fine, but then it should move with the projectile.
