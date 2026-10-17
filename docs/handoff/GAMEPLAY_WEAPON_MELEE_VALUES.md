## Per-weapon recoil (HmSkelControlRecoil): resolved value [source]

Source: arch = TransGame.Default__TnWeaponMesh (authored.db); mesh = the weapon mesh's authored member (RE weapon_presentation_audit.json, <WEP>_WEPMESH).
Overlay rule: an authored vector member replaces the archetype member whole (absent axes 0); Params override per axis.

| Weapon | Control | TimeDuration | RotAmplitude | RotFrequency | RotParams | LocAmplitude | LocFrequency | LocParams |
|---|---|---|---|---|---|---|---|---|
| PlasmaCannon (WEP_PlasmaCannon_p.PlasmaCannon_WEPMESH) | Spine | 1 [mesh] | (-4000, 3000, 0) [mesh] | (10, 10, 0) [arch] | Zero/Zero/Zero [mesh] | (-10, 0, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Zero/Zero [arch] |
| PlasmaCannon (WEP_PlasmaCannon_p.PlasmaCannon_WEPMESH) | RightHand | 1 [mesh] | (8000, 0, 0) [mesh] | (10, 20, 20) [mesh] | Zero/Zero/Zero [mesh] | (-15, 0, 0) [mesh] | (20, 0, 0) [mesh] | Zero/Zero/Random [arch] |
| SniperRifle (WEP_SniperRifle_p.SniperRifle_WEPMESH) | Spine | 0.6 [mesh] | (2000, 6000, -1000) [mesh] | (10, 10, 10) [mesh] | Zero/Zero/Zero [mesh] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| SniperRifle (WEP_SniperRifle_p.SniperRifle_WEPMESH) | RightHand | 0.8 [mesh] | (3000, -6000, -0) [mesh] | (15, 10, 10) [mesh] | Zero/Zero/Zero [mesh] | (-8, -10, 0) [mesh] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |
| AssaultRifle (WEP_AssaultRifle_p.AssaultRifle_WEPMESH) | Spine | 0.8 [mesh] | (500, 1000, 0) [mesh] | (10, 10, 0) [arch] | Zero/Zero/Zero [mesh] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| AssaultRifle (WEP_AssaultRifle_p.AssaultRifle_WEPMESH) | RightHand | 0.5 [mesh] | (2000, 500, -2000) [mesh] | (15, 10, 10) [mesh] | Zero/Random/Zero [arch] | (-8, 0, 0) [arch] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |
| IonBlaster (WEP_IonBlaster_p.IonBlaster_WEPMESH) | Spine | 0.8 [mesh] | (500, 1000, 0) [mesh] | (10, 10, 0) [arch] | Zero/Zero/Zero [mesh] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| IonBlaster (WEP_IonBlaster_p.IonBlaster_WEPMESH) | RightHand | 0.5 [mesh] | (2000, 500, -2000) [mesh] | (15, 10, 10) [mesh] | Zero/Random/Zero [arch] | (-8, 0, 0) [arch] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |
| HeavyPistol (WEP_HeavyPistol_p.HeavyPistol_WEPMESH) | Spine | 0.8 [mesh] | (1000, 2000, 1800) [mesh] | (10, 10, 15) [mesh] | Zero/Random/Zero [arch] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| HeavyPistol (WEP_HeavyPistol_p.HeavyPistol_WEPMESH) | RightHand | 0.6 [mesh] | (1200, -1000, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Zero/Zero [mesh] | (-60, 0, 0) [mesh] | (10, 10, 0) [mesh] | Zero/Zero/Zero [mesh] |
| BurstRifle (WEP_BurstRifle_p.BurstRifle_WEPMESH) | Spine | 0.5 [mesh] | (1500, -1000, 0) [mesh] | (10, 10, 0) [arch] | Zero/Random/Zero [arch] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| BurstRifle (WEP_BurstRifle_p.BurstRifle_WEPMESH) | RightHand | 0.5 [mesh] | (1000, 100, -3000) [mesh] | (10, 5, 10) [mesh] | Zero/Random/Zero [arch] | (-8, 0, 0) [arch] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |
| HeavyMG (WEP_HeavyMG_p.HeavyMG_WEPMESH) | Spine | 0.6 [mesh] | (1800, 3000, 500) [mesh] | (10, 5, 10) [mesh] | Zero/Zero/Zero [mesh] | (-5, 0, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Zero/Zero [arch] |
| HeavyMG (WEP_HeavyMG_p.HeavyMG_WEPMESH) | RightHand | 0.4 [mesh] | (-1000, 500, -250) [mesh] | (15, 5, 5) [mesh] | Zero/Zero/Zero [mesh] | (-8, 0, 0) [arch] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |
| GrenadeLauncher (WEP_GrenadeLauncher_p.GrenadeLauncher_WEPMESH) | Spine | 0.6 [mesh] | (5000, 150, 0) [mesh] | (10, 10, 0) [arch] | Zero/Random/Zero [arch] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| GrenadeLauncher (WEP_GrenadeLauncher_p.GrenadeLauncher_WEPMESH) | RightHand | 0.8 [mesh] | (-10000, -3000, 0) [mesh] | (10, 10, 10) [mesh] | Zero/Random/Zero [arch] | (-8, 0, 0) [arch] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |
| Bazooka (WEP_Bazooka_p.Bazooka_WEPMESH) | Spine | 1 [mesh] | (6000, 6000, -3000) [mesh] | (5, 5, 10) [mesh] | Zero/Zero/Zero [mesh] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| Bazooka (WEP_Bazooka_p.Bazooka_WEPMESH) | RightHand | 0.8 [mesh] | (-2000, 150, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Zero/Zero [mesh] | (-60, 0, 0) [mesh] | (10, 0, 0) [arch] | Zero/Zero/Zero [mesh] |
| RepairRay (WEP_RepairBeam_p.RepairBeam_WEPMESH) | Spine | 1 [mesh] | (500, -150, 0) [mesh] | (10, 10, 0) [arch] | Zero/Random/Zero [arch] | (0, 0, 0) [arch] | (0, 0, 0) [arch] | Zero/Zero/Zero [arch] |
| RepairRay (WEP_RepairBeam_p.RepairBeam_WEPMESH) | RightHand | 0 [mesh] | (0, 0, 0) [mesh] | (0, 0, 0) [mesh] | Zero/Random/Zero [arch] | (-8, 0, 0) [arch] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |
| EMPShotgun (WEP_EMPShotgun_p.EMPShotgun_WEPMESH) | Spine | 0.8 [mesh] | (1500, 2100, 2000) [mesh] | (15, 10, 5) [mesh] | Zero/Zero/Zero [mesh] | (-20, 0, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Zero/Zero [arch] |
| EMPShotgun (WEP_EMPShotgun_p.EMPShotgun_WEPMESH) | RightHand | 0.6 [mesh] | (-4000, -4000, 2000) [mesh] | (15, 5, 10) [mesh] | Zero/Zero/Zero [mesh] | (-15, 0, 10) [mesh] | (10, 0, 10) [mesh] | Zero/Zero/Random [arch] |
| Shotgun (WEP_Shotgun_p.Shotgun_WEPMESH) | Spine | 0.8 [mesh] | (1500, 2100, 2000) [mesh] | (15, 10, 5) [mesh] | Zero/Zero/Zero [mesh] | (-20, 0, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Zero/Zero [arch] |
| Shotgun (WEP_Shotgun_p.Shotgun_WEPMESH) | RightHand | 0.6 [mesh] | (-4000, -4000, 2000) [mesh] | (15, 5, 10) [mesh] | Zero/Zero/Zero [mesh] | (-15, 0, 10) [mesh] | (10, 0, 10) [mesh] | Zero/Zero/Random [arch] |
| HomingRocket (WEP_RocketLauncher_p.RocketLauncher_WEPMESH) | Spine | 1 [mesh] | (-4000, 3000, 0) [mesh] | (10, 10, 0) [arch] | Zero/Zero/Zero [mesh] | (-10, 0, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Zero/Zero [arch] |
| HomingRocket (WEP_RocketLauncher_p.RocketLauncher_WEPMESH) | RightHand | 0.7 [mesh] | (8000, 0, 0) [mesh] | (10, 0, 0) [mesh] | Zero/Random/Zero [arch] | (-15, 0, 0) [mesh] | (10, 0, 0) [arch] | Zero/Zero/Random [arch] |

Weapons without authored recoil (vehicle weapons, grenades, melee) use the archetype row.

## Melee sets (player TnMeleeSet): values used [source: RE melee_presentation_audit.json, PRESENTATION_AUDIT_2_RE.md part 2]

| Set | Chassis | Attack | Weapons | Damage | Damage type | Impulse | Dash | GroundSpeedMult | Used in ours |
|---|---|---|---|---|---|---|---|---|---|
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | WeaponAttack | Poke | 9999 | TnDamageTypePoke | 200000 | 2500 for 0.25 s (end x0.3) | 1.5 | yes |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | Whirlwind | Any | 85 | TnDamageTypeWhirlwind | 2000 | 2000 for 0 s (end x0) | 1.2 | yes |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | WeaponAttack | Any | 150 | TnDamageTypeMelee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 | yes |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | WeaponAttack | Turret | 150 | TnDamageTypeMelee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | WeaponAttack | Mace, Club | 150 | TnDamageTypeMelee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | WeaponAttack | Axe, Hammer | 150 | TnDamageTypeMelee | 50000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | Stomp | Any | 150 | TnDamageTypeMelee | 70000 | 0 for 0 s (end x1) | 1 | no (not reachable in Versus) |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | WeaponAttack | Flag | 9999 | TnDamageTypeMelee | 80000 | 0 for 0 s (end x1) | 0.75 | yes |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | WeaponAttack | Bomb | 9999 | TnDamageTypeMelee | 80000 | 0 for 0 s (end x0) | 0.75 | yes |
| HeavyMediumMeleeSet | Tank, Truck, Truck7, Jet5, Jet7, Tank2, Tank3, Tank4, Tank5, Truck5 | Combo | Any | 15 | TnDamageTypeWeakMelee | 0 | 0 for 0 s (end x0) | 0.75 | no (not reachable in Versus) |
| HeavySmallMeleeSet | Minion2 | Combo | Any | 15 | TnDamageTypeWeakMelee | 0 | 0 for 0 s (end x0) | 0.75 | no (not reachable in Versus) |
| HeavySmallMeleeSet | Minion2 | Stampede_Miss | Any | 0 | TnDamageTypeMelee | 0 | 0 for 0 s (end x1) | 1 | no (not reachable in Versus) |
| HeavySmallMeleeSet | Minion2 | Stampede | Any | 25 | TnDamageTypeMelee | 0 | 2000 for 0.75 s (end x1) | 1 | no (not reachable in Versus) |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | Whirlwind | Any | 85 | TnDamageTypeWhirlwind | 2000 | 2000 for 0 s (end x0) | 1.2 | yes |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Any | 150 | TnDamageTypeMelee | 20000 | 2500 for 0.25 s (end x0.3) | 0.75 | yes |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Turret | 150 | TnDamageTypeMelee | 20000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Axe | 150 | TnDamageTypeMelee | 50000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Mace | 150 | TnDamageTypeMelee | 50000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Club | 150 | TnDamageTypeMelee | 20000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Poke | 9999 | TnDamageTypePoke | 200000 | 2500 for 0.25 s (end x0.3) | 1.5 | yes |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Flag | 9999 | TnDamageTypeMelee | 80000 | 3000 for 1 s (end x0.3) | 0.75 | yes |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | WeaponAttack | Bomb | 9999 | TnDamageTypeMelee | 80000 | 3000 for 1 s (end x0.3) | 0.75 | yes |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | Stomp | Any | 150 | TnDamageTypeMelee | 70000 | 0 for 0 s (end x0) | 0 | no (not reachable in Versus) |
| LightMediumMeleeSet | Jet, Car5, Jet2, Jet3, Jet4, Jet6, Jet8, Truck2, Truck3, Truck4, Truck6 | Combo | Any | 15 | TnDamageTypeWeakMelee | 20000 | 0 for 0 s (end x0) | 0.75 | no (not reachable in Versus) |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | Whirlwind | Any | 85 | TnDamageTypeWhirlwind | 2000 | 2000 for 0 s (end x0) | 1.2 | yes |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | WeaponAttack | Any | 150 | TnDamageTypeMelee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 | yes |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | WeaponAttack | Turret | 150 | TnDamageTypeMelee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | WeaponAttack | Mace, Club | 150 | TnDamageTypeMelee | 40000 | 2500 for 0.25 s (end x0.3) | 0.75 | no (not reachable in Versus) |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | WeaponAttack | Flag | 9999 | TnDamageTypeMelee | 80000 | 0 for 0 s (end x0) | 0.75 | yes |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | Stomp | Any | 150 | TnDamageTypeMelee | 70000 | 0 for 0 s (end x1) | 1 | no (not reachable in Versus) |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | WeaponAttack | Bomb | 9999 | TnDamageTypeMelee | 80000 | 0 for 0 s (end x1) | 0.75 | yes |
| LightSmallMeleeSet | Car, Car2, Car3, Car4, Car6, Car7 | Combo | Any | 15 | TnDamageTypeWeakMelee | 20000 | 0 for 0 s (end x0) | 0.75 | no (not reachable in Versus) |

## RE verification (re-workspace-0c, against authored.db / the CDOs)

- Recoil: all 28 rows OK; RotSinOffset / RotOffset / LocSinOffset / LocOffset resolve to 0 on all 14. Overlay rule CONFIRMED.
- Correction applied: two Versus pickups (TnWeaponPickupFactory) author their own recoil, now rows of the table:
  - HeavyRocketTurret (WEP_TurretRocket_p.Turret_Rocket_WEPMESH): Spine 0.4 s, Rot (4000, 150, 0) x (5, 10, 0); RightHand 0.3 s, Rot (1000, 150, 1000) x (10, 10, 10), Loc (-8, 0, 0) x (10, 0, 0) [arch].
  - GatlingGun (WEP_TurretIonGun_p.TurretIonGun_WEPMESH): TimeDuration 0 and zero amplitudes on both: inert (the archetype fallback would have given it recoil).
- Melee: all 32 rows OK. The Speed > 0 / Time 0 reading CONFIRMED (TnFlier.StartFlight returns at FP.Time == 0: no dash, no EndVelocityScaler). The dash starts only with a melee target and Attack.AllowFlight (every Versus WeaponAttack row has it): ours lunges only through the assist target, as before.
- PlaneRocket: the burst drop CONFIRMED for Versus (MultiplayerData has no ShotsPerBurst); the SP and SV data have it (Escalation, if TnSurvivalGame selects SurvivalData).
