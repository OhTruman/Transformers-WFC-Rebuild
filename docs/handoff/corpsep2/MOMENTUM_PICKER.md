# The full momentum picker (patch 2, part 4): design

Gameplay, 2026-10-17. Replaces the interim momentum type in `spawnCorpse` (the push away from the killer's position).

## What the original uses

`TnRobotDeathPicker`'s momentum types (ForwardMomentum → Death_BackShot, BackwardMomentum → Death_FrontShot, SideMomentum → the default)
read **TearOffMomentum**: the momentum of the killing `TakeDamage` call. Its direction depends on the damage path:

| Path | Momentum direction | Our call site (af4d0731) |
|---|---|---|
| Instant hit (local) | the shot direction × InstantHitMomentum | World.cpp ~810 |
| Instant hit (bots) | the shot direction | WorldBots.cpp ~125 |
| Radial / splash (HurtRadius) | victim − the hurt origin | World.cpp ~3440 / 3443 (radius damage), ~4510 / 4515 (explosions) |
| Shockwave | victim − self (already a direction) | World.cpp ~4787 |
| Melee / other splash with a direction | the hit's dir | World.cpp ~5047 |
| Environment, drain, repair, sentry, flash-bang | the site's own direction where one exists, else none | — |

**Authored magnitudes:** projectiles' MomentumTransfer is 1.0 almost everywhere (RocketTurret 250,000); TnWeaponDataPlasmaCannon's
InstantHitMomentum is (1, 1, 1). Momentum is therefore **non-zero but tiny** on nearly every kill: the direction counts, the size
doesn't. The picker only reads the direction against the pawn's facing (cos ≥ 0.7071 forward, ≤ −0.7071 backward, else side).

## Change

- A **presentation-only** per-victim record: `lastHitDir_[victim]`, a unit vector plus a valid flag.
  - Set by the real hit sites just before they call `applyMatchDamage`, via one helper `noteHitDir(victim, dir)`.
  - Never read by the sim, so SIMHASH is unchanged. It isn't part of GameplayEvent, so event hashes are unchanged.
- `requestCorpse` uses `lastHitDir_[victim]` when valid, else the interim (away from the killer's position; PROV, the same as today).
- Cleared on respawn.

**Expected visible difference:** splash kills (rockets, grenades, tank shells) now pick from the blast direction instead of the
killer's position. A rocket landing behind a victim who faces the shooter now plays the forward-momentum death. Instant-hit kills
mostly pick the same as today, since the shot direction ≈ killer → victim.

## Check

- **Headless:** WFC_CORPSETEST logs the entry per death. Over a BOTTEST seed, list how many deaths change entry against the head (only
  splash kills should).
- SIMHASH identical; BOTTEST 48 / 48.
- **Eye:** one staged splash kill from behind.

**Estimate:** about 2 h.
