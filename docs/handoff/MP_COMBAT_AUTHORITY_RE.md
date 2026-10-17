# MP combat authority: who decided a hit in the original (rules reference, RE 2026-10-18)

Scope: a rules review of the original's versus combat semantics from script bytecode (Engine / HM_Engine / TransGame /
TransContent). It is not networking code. Labels: CONFIRMED (C) = bytecode; HIGH (H) = inference (RPC reliability flags and native
replication were not read).

**Summary: the shooter's client decides.** For instant-hit weapons, melee, ability triggers, fire rate and overheat, the firing
client does the work and the server trusts it. The **server applies damage and death**, which then replicate back. There is **no
server re-trace, no validation and no lag-compensated rewind** in script.

## 1. Instant-hit weapons (C)

`Weapon.FireAmmunition` → `InstantFire`, per shot, **on the firing client**:
- trace from `GetWeaponStartTraceLocation` along `GetAdjustedAim` (`CalcWeaponFire` → ImpactList);
- per impact on a non-authority (client): map the hit component to an index, `ProcessInstantHit` locally, play fire / impact
  effects;
- then **`ServerImpact(FireMode, Impact, bFirstImpactOfShot)`** sends the impact itself: hit actor, location, component index,
  ray.

**Server side:**
- `Weapon.ServerImpact` only checks that the Instigator exists and the component index maps back. Then
  `ProcessInstantHit(mode, Impact)` → **`HitActor.TakeDamage`** with the weapon's InstantHitDamage / momentum / damage type.
- For the first impact of a shot it also does `SetFlashImpact` (replicates the flash / impact to other clients) and
  **`ConsumeAmmo`**.
- **No re-trace and no range / line-of-sight / rate check.**

**Damage on clients:** `HmWeapon.ShouldRegisterDamage` → on a client, `TakeDamage` runs only for **torn-off** pawns (corpses /
ragdolls) or crowd agents. So the shooter sees immediate impact effects and corpse impulses, while health changes only on the
server.

`HmWeapon.InstantFire` (server) also notifies near-miss pawns (`NotifyNearlyShot` within NearlyShotDistance of the bullet path):
the suppression / flinch feedback.

## 2. Projectiles (C)

- **Client:** `Weapon.ProjectileFire` computes the real start (muzzle) and aim, corrected toward the crosshair impact, and calls
  **`ServerFireProjectile(mode, StartLocation, AimDirection × 10000)`**. It does **not** spawn its own projectile; it only plays
  fire effects.
- **Server:** spawns the projectile **at the client-supplied start location** with the client's aim, `ServerSpawnedProjectile`,
  `ConsumeAmmo`.
- The projectile then simulates and collides **on the server** (stock replicated projectile; its explosion damage is server-side,
  H: stock). Others see it via replication. The shooter sees it appear after a round trip.

## 3. Melee (C)

- **Start:** `TnMeleeManager.StartMeleeAttack` on a client → `ServerStartMeleeAttack(type, target)`. The server rejects it only if
  the attacker is dead or downed / reviving. Both sides play the attack.
- **Hit:** `Attacking.ProcessMeleeHit` runs where the attack animation detects contact. **If the attacker is locally controlled**
  → `ServerProcessMeleeHit(Info, AttackIndex, Controller)`. The server applies `TakeDamage(MeleeDamageModifier(Attack.Damage),
  momentum = direction × Impulse, Attack.DamageType)` **without re-checking contact**.
- The **hit reaction** (`PlayHitReaction`) plays locally on the attacker's machine immediately.
- Impact hits (`ProcessImpactHit`, e.g. charges) are applied **only on the server** (`Role == Authority`).

## 4. Abilities (C)

- **Client:** `TnAbilityManager.TriggerAbility` checks spam cooldown, availability and `LocalTriggerAbility` locally, starts the
  local cooldown / spends the resource, then **`ServerTriggerAbility(n)`**.
- **Server:** `ServerTriggerAbility` checks only that the owner isn't dead or dying, then runs the ability's
  `ServerTriggerAbility`, `ClientOnTriggerAbility`, the replicated trigger toggle, resource reset and stats. **No server-side
  cooldown / resource re-check** in this path. `ServerCanStartCooldown` exists per ability class for its own cooldown start.
- Ability effects (AoE, spawned projectile / sentry / barrier, buffs) are created on the server.

## 5. Fire rate, ammo, overheat (C)

- **Fire rate:** client-side. `RefireCheckTimer` at `GetFireInterval` drives `FireAmmunition` on the firing machine. The server
  just processes each ServerImpact / ServerFireProjectile it receives (no rate check).
- **Ammo:** both sides count. The client calls `ConsumeAmmo` in FireAmmunition (Role < Authority). The server calls it per shot
  in ServerImpact (first impact) / ServerFireProjectile. **The server does not refuse a shot when its own count is empty** (no
  HasAmmo check in those RPCs).
- **Overheat (heat weapons):** client-authoritative. The client runs the heat model. On entering Overheated and on cool-down
  complete it sends **`ServerSyncWeaponheat(currentHeat)`**, and the server adopts it (`WeaponHeat.SyncCurrentHeat`).

## 6. Kill confirmation and display (C, with stock UE3 parts)

- **Death happens on the server:** `TakeDamage` → health ≤ 0 → `Died` → `GameInfo.Killed`:
  - ScoreKill, PRI deaths;
  - **`BroadcastDeathMessage(Killer, Victim, DamageType)`** (the kill feed, a server broadcast to all clients) when
    ShouldAnnounceKill;
  - CheckScore, DiscardInventory;
  - `TnGame.Killed` adds the respawn helper and kill achievements.
- **The victim's client sees its death when the server's death replicates** (stock bTearOff / TornOff: corpse ragdoll, death
  camera; DEATH_CAMERA_RE). Kill feed entries arrive by the same server broadcast.
- **When views disagree** (H, by construction):
  - the shooter's view wins: a hit the shooter's client registered is applied, even if on the victim's screen they had already
    moved behind cover;
  - the victim takes the damage and dies about one round trip after the shooter fired, and sees the kill feed / death camera then.
  - There's no rewind or arbitration. The only server checks are "Instigator exists" and component mapping (instant hit), and
    "not dead / downed" (melee start, ability).

## 7. Disconnected player (C)

- **`PlayerController.Destroyed`:** if it had a pawn → **`Pawn.Health = 0; Pawn.Died(self, DmgType_Suicided)`**. A driven vehicle
  ejects its driver first, and the driver dies. **The pawn is killed as a suicide, not removed silently, not handed to a bot, not
  left idle.**
- Then `Controller.Destroyed` → removed from the team → `Game.Logout`:
  - `TnGame.Logout`: unmute, spawn / respawn helper logout, `GRI.NumPlayersLeft++`;
  - **`TnVersusGame.Logout` sets a `ForfeitDelay` = 1.5 s timer → `CheckForfeit`.** In progress, if either team has 0 players →
    clear that team's score and **`EndGame(none, "Forfeit")`**.

## 8. For Gameplay / Systems (lockstep)

- Lockstep removes the disagreement by construction. **Presentation should follow the local shooter** where the original did:
  - immediate local impact / flash effects;
  - melee hit reactions on contact;
  - ability start and cooldown on press;
  - the projectile appearing from the muzzle (the original showed it after a round trip; showing it at once is an improvement).
- **Health / death / kill feed** in the original came from the server, a round trip later. In lockstep they come at the
  simulated tick, which is fine.
- **Disconnect:** kill the pawn as a suicide (Killer = the leaving player; scoring follows the mode's suicide rule), then apply the
  1.5 s forfeit check for team versus.
