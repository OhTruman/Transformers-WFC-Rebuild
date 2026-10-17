# Weapon + melee authored values batch (package 7): design and expected changes

Gameplay, 2026-10-17. One sim-identity change with one Classic re-baseline (Integration): per-weapon spine / hand recoil (RE GAMEPLAY_WEAPON_PRESENTATION_AUDIT_RE.md) + the per-chassis melee set values (RE PRESENTATION_AUDIT_2_RE.md part 2, 4bb3148; data: RE notes/data/melee_presentation_audit.json) + PlaneRocket bursts (4 at 0.15 s, BurstDelay 0.75; audit part 4).

## Today (rebuild, one generic melee set)

WeaponAttack 150 dmg / impulse 30000 / GroundSpeedMultiplier 0.75 for every chassis; flag / bomb 9999 / 80000 without a lunge; Whirlwind 85 / 2000 / 1.2; Poke 9999 / 200000. Every weapon uses IonBlaster's SpineRecoil / RightHandRecoil.

## Authored melee values (player sets; AI sets omitted)

| Set | Attack | Weapons | Damage | Damage type | Impulse | Dash | GroundSpeedMult |
|---|---|---|---|---|---|---|---|
| HeavyMedium | WeaponAttack | Poke | 9999 | Poke | 200000 | 2500 for 0.25 s (end x0.3) | 1.50 |
| HeavyMedium | Whirlwind | Any | 85 | Whirlwind | 2000 | 2000 for 0 s (end x0) | 1.20 |
| HeavyMedium | WeaponAttack | Any | 150 | Melee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| HeavyMedium | WeaponAttack | Turret | 150 | Melee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| HeavyMedium | WeaponAttack | Mace, Club | 150 | Melee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| HeavyMedium | WeaponAttack | Axe, Hammer | 150 | Melee | 50000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| HeavyMedium | Stomp | Any | 150 | Melee | 70000 | 0 for 0 s (end x1) | 1.00 |
| HeavyMedium | WeaponAttack | Flag | 9999 | Melee | 80000 | 0 for 0 s (end x1) | 0.75 |
| HeavyMedium | WeaponAttack | Bomb | 9999 | Melee | 80000 | 0 for 0 s (end x0) | 0.75 |
| HeavyMedium | Combo | Any | 15 | WeakMelee | 0 | 0 for 0 s (end x0) | 0.75 |
| HeavySmall | Combo | Any | 15 | WeakMelee | 0 | 0 for 0 s (end x0) | 0.75 |
| HeavySmall | Stampede_Miss | Any | 0 | Melee | 0 | 0 for 0 s (end x1) | 1.00 |
| HeavySmall | Stampede | Any | 25 | Melee | 0 | 2000 for 0.75 s (end x1) | 1.00 |
| LightMedium | Whirlwind | Any | 85 | Whirlwind | 2000 | 2000 for 0 s (end x0) | 1.20 |
| LightMedium | WeaponAttack | Any | 150 | Melee | 20000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightMedium | WeaponAttack | Turret | 150 | Melee | 20000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightMedium | WeaponAttack | Axe | 150 | Melee | 50000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightMedium | WeaponAttack | Mace | 150 | Melee | 50000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightMedium | WeaponAttack | Club | 150 | Melee | 20000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightMedium | WeaponAttack | Poke | 9999 | Poke | 200000 | 2500 for 0.25 s (end x0.3) | 1.50 |
| LightMedium | WeaponAttack | Flag | 9999 | Melee | 80000 | 3000 for 1 s (end x0.3) | 0.75 |
| LightMedium | WeaponAttack | Bomb | 9999 | Melee | 80000 | 3000 for 1 s (end x0.3) | 0.75 |
| LightMedium | Stomp | Any | 150 | Melee | 70000 | 0 for 0 s (end x0) | 0.00 |
| LightMedium | Combo | Any | 15 | WeakMelee | 20000 | 0 for 0 s (end x0) | 0.75 |
| LightSmall | Whirlwind | Any | 85 | Whirlwind | 2000 | 2000 for 0 s (end x0) | 1.20 |
| LightSmall | WeaponAttack | Any | 150 | Melee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightSmall | WeaponAttack | Turret | 150 | Melee | 30000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightSmall | WeaponAttack | Mace, Club | 150 | Melee | 40000 | 2500 for 0.25 s (end x0.3) | 0.75 |
| LightSmall | WeaponAttack | Flag | 9999 | Melee | 80000 | 0 for 0 s (end x0) | 0.75 |
| LightSmall | Stomp | Any | 150 | Melee | 70000 | 0 for 0 s (end x1) | 1.00 |
| LightSmall | WeaponAttack | Bomb | 9999 | Melee | 80000 | 0 for 0 s (end x1) | 0.75 |
| LightSmall | Combo | Any | 15 | WeakMelee | 20000 | 0 for 0 s (end x0) | 0.75 |

## Differences that change the sim (both AIs)

- LightMedium WeaponAttack impulse 20000 (ours 30000); per melee-weapon impulses (Axe / Hammer 50000, LightSmall Mace / Club 40000).
- LightMedium flag / bomb lunge 3000 for 1 s (ours: no lunge; the other sets 0).
- Stomp (150 / 70000), Combo (WeakMelee 15 dmg), HeavySmall Stampede (25 dmg, dash 2000 for 0.75 s): where our chassis reach them (to check per chassis melee set).
- Per-weapon spine / hand recoil (shot origins through the hand sockets).
- PlaneRocket: bursts of 4 at 0.15 s, then BurstDelay 0.75 s.

## Gate

- One Classic SIMHASH re-baseline (deliberate; the authored values are the evidence); NAVQ / BOTTEST; the eye check of the recoil per gun class.
