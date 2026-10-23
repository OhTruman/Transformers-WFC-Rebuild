# User playtest 2026-10-23: original values for #11, #12, #15, #8 (RE)

Data: `notes/data/weapon_mp_table.json` (`tools/weapon_mp_table.py`). Labels: C = authored data / script; H = inference.

**The data rule** (C, TnWeapon.GivenTo): versus uses each weapon's **MultiplayerData**. **If a weapon has none, it uses PlayerData**,
the campaign row: AssaultRifle, BurstRifle, EmpShotgun, Shotgun, HeavyMG, RepairRay. Projectile damage comes from the
projectile's own MultiplayerData / Data (`*_PROJDATA`).

## #11 Spread, recoil, aim assist

### Spread (C, TnWeapon)
- `CurrentSpread` starts at **Modifier.Min**; **each shot adds ModifierChangePerShot** (clamped to Max).
- When not firing it **recovers by (Max − Min) × dt / Cooldown**, i.e. a full recovery in *Cooldown* seconds.
- The aim offset = X + rand·spread·Y + rand·spread·Z: spread is the **tangent of the cone half-angle**.
- Multipliers:
  - **fine aim** (zoom) × FineAimSpreadModifier;
  - the pawn's spread modifier: **airborne × 2.0** (0.25 s ramp-up, 0.5 s ramp-down on landing, 0.1 s from hover);
  - the TnBuffDecreaseWeaponSpread buff;
  - per-difficulty SpreadMultipliers (the MP index 0).

### Spread application, exact (C, added after Gameplay's question)
- **Offset = the stock UE3 Weapon.AddSpread:** RandY = FRand() − 0.5; RandZ = Sqrt(0.5 − RandY²)·(FRand() − 0.5); aim = X + RandY·S·Y +
  RandZ·S·Z. That's a roughly disc-shaped cone of about **±0.5·S** (a uniform ±1 square would be twice as wide).
- **S** = Data.Spread[mode] (0) + Pawn.GetWeaponSpreadModifier()
  = CurrentSpread × SpreadMultipliers[0] × the DecreaseWeaponSpread buff, then × airborne (1 → 2) × FineAimSpreadModifier when zoomed.
  The airborne and zoom factors apply **in robot form only**.
- IncrementSpread runs once per fire call, after all pellets.
- **Shotgun / EMPShotgun use a fixed SpreadPattern** (8 / 13 offsets, no randomness), scaled by GetSpread() / GetInitialSpread().

### Recoil (C)
- `RecoilCameraParams`: PitchAdjustmentDeg (camera kick up per shot), RecoilTime, RecoilDelay. Plus CameraShakeParams
  (WEAPON_CAMERA_SHAKE_RE).

### Per weapon (versus; TTK assumes 300 HP and every hit landing, no range falloff / headshots: H)

| weapon | data | damage | pellets | interval s | auto | clip | reload s | DPS | shots to kill | TTK s | spread min | spread max | +/shot | recover s | fine-aim × | recoil ° | snap range | equip s | put-down s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| AssaultRifle | Player | 18 | 1 | 0.095 | | 32 | 1.5 | 189.5 | 17 | 1.52 | 0.06 | 0.15 | 0.005 | 3 | 0.5 | 0.05 | 1000–10000 | 0.2 | 0.5 |
| BurstRifle | Player | 29 | 1 | 0.1 | burst | 24 | 1.5 | 290 | 11 | 1.0 | 0.05 | 0.12 | 0.01 | 2 | | | 1000–10000 | 0.2 | 0.2 |
| HeavyPistol | MP | 80 | 1 | 0.6 | semi | | 1.5 | 133 | 4 | 1.8 | 0.02 | 0.08 | 0.02 | 3 | 0.25 | 1.5 | 1000–10000 | 0.2 | 0.5 |
| SniperRifle | MP | 162 | 1 | 1.25 | semi | 4 | 2.5 | 130 | 2 | 1.25 | 0.2 | 0.4 | 0.1 | 4 | (scope) | 1 | 1000–10000 | 0.2 | 0.2 |
| IonBlaster | MP | 15 | 1 | 0.065 | auto | 50 | 1.5 | 231 | 20 | 1.23 | 0.08 | 0.18 | 0.005 | 2 | 0.5 | 0.1 | 1000–10000 | 0.2 | 0.5 |
| EmpShotgun | Player | 13 | 13 | 0.5 | semi | 2 | 2.5 | 338 | 2 | 0.5 | 0.09 | 0.15 | 0.02 | 4 | 0.5 | 4 | 1000–10000 | 0.2 | 0.5 |
| Shotgun | Player | 13 | 8 | 0.4 | | 6 | 2.5 | 260 | 3 | 0.8 | 0.08 | 0.14 | 0.02 | 4 | 0.5 | 2 | 1000–10000 | 0.2 | 0.5 |
| HeavyMG | Player | 32 | 1 | 0.4 (wind-up) | | 60 | 2.5 | 80+ | 10 | 3.6 | 0.08 | 0.18 | 0.004 | 4 | 0.5 | 0.2 | 1000–10000 | 0.2 | 0.2 |
| PlasmaCannon | MP | 115 (proj) | 1 | 0.15 | charge | 100 | 2.5 | – | 3 | – | 0.04 | 0.09 | 0.02 | 3 | 0.5 | | 1000–10000 | 0.2 | 0.5 |
| RocketLauncher | MP | 135 (proj, splash) | 1 | 1.2 | semi | 4 | 2.5 | 112 | 3 | 2.4 | 0.04 | 0.08 | 0.02 | 3 | 0.5 | 0.25 | 0–16852 | 0.2 | 0.5 |
| GrenadeLauncher | MP | 145 (r 1500) | 1 | 1.1 | semi | 3 | 2.5 | 132 | 3 | 2.2 | 0.04 | 0.04 | 0.06 | 5 | 0.75 | 0.5 | | 0.2 | 0.2 |
| Bazooka (Fusion) | MP | 70 | 1 | 0.75 | | 5 | 2.5 | 93 | 5 | 3.0 | 0.05 | 0.11 | 0.02 | 5 | 0.75 | 1.5 | 1000–10000 | 0.2 | 0.2 |
| GatlingGun (turret) | MP | 25 | 1 | 0.065 | auto | 300 | | 385 | 12 | 0.71 | 0.02 | 0.1 | 0.005 | 4 | 0.5 | 0.15 | | 0.2 | 0.2 |
| RepairRay | Player | 100 / s (beam) | | 0.1 | | 100 | 2.5 | | | | 0.02 | 0.02 | | | | | 0–5000 | 0.2 | 0.5 |
| car AR (vehicle) | MP | 12 | 1 | 0.065 | auto | 60 | 1.5 | 185 | 25 | 1.56 | 0.02 | 0.1 | 0.005 | 1.5 | 0.5 | | | 0.2 | 0.2 |
| car rockets | MP | 55 × 2 | 2 | 0.7 | | 10 | 1.5 | 157 | 3 | 1.4 | 0.02 | 0.02 | | 3 | | | | | |
| jet MG | MP | 10 | 1 | 0.06 | auto | 80 | 2 | 167 | 30 | 1.74 | 0.02 | 0.08 | 0.005 | 2 | | | | 0.1 | 0.1 |
| homing rockets (jet) | MP | 125 | 1 | 1.0 | | 5 | 2.5 | 125 | 3 | 2.0 | 0.02 | 0.08 | 0.02 | 4 | | | 0–16852 | 0.2 | 0.2 |
| tank cannon | MP | 170 | 1 | 2.0 | | 1 | 2 | 85 | 2 | 2.0 | 0.02 | 0.08 | 0.06 | 2.5 | | | | 0.2 | |

The full 75-weapon table, including all fields, range-damage modifiers and burst / fire-interval ramp data, is in the json.

### Aim assist (C; 360 build)
- **No target snap in versus:** `TnGame.CanTargetSnap` = false. The campaign and Escalation return true (TnSurvivalGame), which
  enables FineAim snap-to-target (TnFineAimManager, 1 s timeout).
- **Aim friction ("pan sticky", TnPanStickyAdjuster)** whenever the aim is over a target-assist target:
  - turn rate × the target component's stickiness: MP chassis **yaw 0.5, pitch 0.8**; the HmTargetableComponent default is 1 / 1;
  - × 0.9 if you're moving (× 0.9 if both move), × 1.5 when jumping;
  - clamped to [0.1, 1];
  - blended to 1 with distance up to IdealDistance 1000 UU, so it's strongest close in;
  - ramps out over 1 s after the target leaves.
- **Target pickers** (per chassis TnTargetableComponent): TargetCone and TargetAssist 4° with a 100–375 cm radius (by chassis);
  Snap; Dodge / Melee assist; Homing lock 8°.
- `TargetSnapRange` per weapon (above; 1000–10000 typical) applies only where snap is allowed.
- **PC:** this dump is the Xbox 360 build. The PC-version rules for mouse aim assist aren't present (H: the PC release disabled
  stickiness with the mouse, but that's not verifiable here).

## #12 Damage / TTK
- The original columns are above (damage, pellets, interval, DPS, shots / TTK at 300 HP).
- **Gameplay:** please add your values per row for the comparison.
- Caveats:
  - the MP character health and armour differ per class: H, 300 is a reference value;
  - RangeDamageModifiers reduce damage at range for some weapons (json);
  - headshot multipliers aren't included.

## #15 Weapon switch (C)
- Switching = **old weapon PutDownTime, then new weapon EquipTime**. The weapon can't fire during either (WeaponPuttingDown /
  WeaponEquipping states).
  - **Robot weapons: equip 0.2 s; put-down 0.5 s** for AR, HeavyPistol, IonBlaster, EmpShotgun, Shotgun, PlasmaCannon, RocketLauncher,
    RepairRay; **0.2 s** for BurstRifle, SniperRifle, GrenadeLauncher, HeavyMG, Bazooka.
  - So switching from an AR takes **0.7 s**; from a 0.2 s weapon, **0.4 s**.
  - The visual holster detach happens at DetachDelay (0.5 / 0.2 s, same as the put-down).
- **Animations** (TnWeaponOwnerAnimator, additive, **upper-body mask**):
  - equip `ADD_Shooting_equip_weapon` (blend in 0.2, out 0.3);
  - unequip `ADD_Shooting_UnEquip_Weapon` (blend in 0.3, out 0.0). IonBlaster / RepairRay / Sniper reuse the equip anim for
    unequip;
  - vehicle weapons: `ADD_AssaultRifleVH_Equip_VEH` / `_Drop_VEH` (0.1 s timings);
  - turrets: `Nav_PickUp_Weapon_Turret` (attach delay 0.3 s).
- **Reload** = WeaponReloadAnimTime (1.5 s rifles / pistols, 2.5 s heavy / launchers / shotguns) with `Shooting_Reload_<Weapon>_ROBO`.

## #8 Teammate health bars / names through walls (C, TnObjectiveMarkerTypeTransformerVersus)
- **Allies: always drawn, with no line-of-sight test:** the marker (MarkerAlly_MAT), the name label (focused and unfocused) and
  the **health bar** (TargetHealthBar_MAT). **They show through walls.**
  - Off-screen arrows only for a **downed** ally.
  - Focused size 0.08, unfocused 0.04 (screen fraction); label colour (100, 210, 250).
- **Enemies:** no marker unless you have TnBuffSeeEnemyObjectiveMarkers (and the enemy isn't under a Warcry), your team hard-locked
  it, or a revenge marker is on it.
  - The enemy **name label shows only on focus**, and **only with line of sight** and if not cloaked (HasLineOfSightTo → hidden
    otherwise).
  - No enemy health bar.
- Disguised enemies (TnBuffDisguise): ally style, shown only with line of sight, with the disguise name.
