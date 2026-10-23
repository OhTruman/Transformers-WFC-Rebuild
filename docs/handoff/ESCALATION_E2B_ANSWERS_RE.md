# Escalation E2b: weapon pickup factories, grenade and heavy-weapon generators, Wall_Crashers (answers to Gameplay, RE, 2026-10-23)

**Labels:** C = bytecode / authored data / map Kismet; H = stock UE3 / inference.

**Sources:**
- TnPickupFactory, TnPickupManager, TnWeapon, HmWeapon, HmInventoryManager, TnGrenadeBag (`work/script/decomp`);
- pass 3 §J1 (`notes/TARGETED_PASS3_2026-10-05.md`);
- BrokenHope `Main_Sequence.Wall_Crashers` (`next_map/MP_ESC_BrokenHope_kismet.json`, authored.db).

## 1. TnWeaponPickupFactory (C)
The class has no script of its own: it's TnPickupFactory / PickupFactory behaviour.

**How it's taken:**
- **Not on touch for a new weapon:** auto-pickup needs `TnWeapon.PickupWhenTouched`, which is true **only if the pawn already
  holds that weapon class**. So:
  - owning the class → touching it gives ammo automatically;
  - not owning it → the **contextual pickup button** (TryPickup → TnPickupManager.Pickup, the "pick up" prompt). The closest valid
    pickup among the pawn's touching actors wins, by PickupRating then distance.
- `CanPickupInventory` refuses while downed, meleeing, in vehicle form (for any TnWeapon), or while a revivable pawn is targeted.
- The prompt: `TnContextualCommandPickupWeapon` for a new weapon. State 2 (the ammo variant) for an owned weapon or an ammo crate.

**Which slot is replaced** (`TnPickupManager.DropRobotWeapon`, before the take):
1. If the pawn already has the class as primary, secondary or current: **no toss**, ammo only (below).
2. If the current weapon is a **heavy** (WT_Heavy) or empty: **toss it first**.
3. If the primary **or** secondary slot is empty, or the new weapon is a heavy: no further toss. It goes into the free slot, or in
   hand as the heavy.
4. Otherwise toss **the current weapon** if it's a throwable robot weapon, else the primary, else the secondary.
- **Tossing spawns a TnDroppedPickupWeapon**, carrying the dropper's clip and reserve ammo when the dropper is a player.

**Ammo:**
- **New weapon from a factory:** `PostInstantiateFromPickupFactory` → `SetupAmmoForDefaultPlayerPickup`:
  clip = min(DefaultPickupAmount, MaxAmmoClipCount); reserve = clamp(DefaultPickupAmount − clip, 0, MaxAmmoCount).
  - These come from the weapon's **PlayerData**. Escalation's DesiredWeaponDataType 4 has no data on these weapons, so GivenTo
    falls back to PlayerData.
  - Examples: EmpShotgun 16 (clip 2, reserve 14); SniperRifle 12 (4 + 8); HeavyRocketTurret 10; GatlingGun (Ionic Displacer)
    300; grenade bags 3.
- **Already owned:**
  - `HmWeapon.DenyPickupQuery` **refuses the pickup if reserve ammo is full**.
  - Otherwise `HmInventoryManager.AddInventory` sees the duplicate class → `TryTransferAmmoFrom(new)` → `AddAmmo(new
    clip + reserve)` (the new item's DefaultPickupAmount, clamped by AddAmmo; H) and **reload if empty**. The duplicate item is
    destroyed.
- **RespawnTime 2.0 s** on every Escalation weapon factory. The generator / hide cycle is the same as items (REVIEW_E2A G1: once
  bought it stays out).
- **Bots:** `GetInventoryTypeFor(AI)` = AiInventoryType, which is **None** on every Escalation factory, and the horde's GOAP sets
  have no pickup goal. So **the horde never takes them** (H on the goal side).

## 2. Grenade / mine generators (C)
- FlakGrenades, FlashBangs, HealGrenades and **KamikazeMines** are **TnGrenadeBag** subclasses (a TnWeapon) behind a
  TnWeaponPickupFactory.
- **Slot:** the single grenade slot. `DropGrenades`: holding a **different** bag class → **toss it** (TnDroppedPickupWeapon),
  then take the new one. The same class → ammo merge (refused if full).
- **Count:** DefaultPickupAmount **3** (SP data: MaxAmmoCount 3, clip 0), i.e. 3 grenades / mines.

## 3. Ion / Rocket "turret" generators (C)
- They're **carried heavy weapons**, not emplacements:
  - `TnWeaponHeavyRocketTurret` (WT_Heavy, HeavyTurret_Rocket SP data: 10 rockets, clip 10);
  - `TnWeaponGatlingGun` (WT_Heavy, the Ionic Displacer SP data: 300).
- They're the same class family as a turret ripped off its mount. Held in hand, with the heavy-weapon rules: tossed when you take
  another heavy or any new weapon (step 2 above), no vehicle form (CanPickupInventory).
- The fixed mountable turrets (TnMountableTurret*) are separate actors and not generator items.

## 4. Wall_Crashers (BrokenHope, C)
Three mechanisms in the sequence.

**(a) Four crash walls** (destructibles 15525 / 6870 / 10489 / 4868, blueprint `DES_BrokenHope_Wall`, **InitialHealth 1**):
- `SeqEvent_Touch` on TriggerVolumes 10811 / 11960 / 9486 / 7836, **bPlayerOnly false** (any pawn: players **and** the horde),
  MaxTriggerCount default 1 (once).
- Touched → **CauseDamage 200 (TnDamageTypeAux1)**, which breaks the wall in one hit (health 1), + an emitter Turn On.
- Wall 15525 also gets ToggleHidden + ChangeCollision (NoCollision) with unset targets (H: no-ops).
- Console "blow" (debug).

**(b) Four round gates:**
- `TnSeqEvent_SurvivalEvents` Round Begin → `SeqCond_Increment` (ValueB **4 / 4 / 3 / 6**) → on A == B (that many Round Begins:
  wave 4 / 4 / 3 / 6) → `SeqAct_Toggle` **Turn On** of a **disabled `SeqEvent_Touch`** (TV 11067 / 14603 / 11856 / 8879).
- That Touch (default bPlayerOnly true: players only; H, stock default; MaxTriggerCount 0) **Touched → Turn On a spawn zone**
  (TnSurvivalZoneVolume 15078 / 7763 / 7739 / 6666), **UnTouched → Turn Off**.
- **Per event, not per count:** each Touched / UnTouched fires per actor (stock SeqEvent_Touch). So with two players inside,
  **the first one leaving turns the zone off** even though the other is still in (H, stock).
  - Faithful: on / off on each enter / leave event.
  - These zone toggles go through NotifyZoneActivated / Deactivated (re-sort all).

**(c) The terminal window** (destructible 5491, `DES_BrokenHope_TerminalWindow`, InitialHealth 5):
- **PreGameplaySetup:** PathNodes 14807 / 606 / 13593 **Turn Off** (paths blocked) + TnToggleDestructible input 1 on 5491.
- `SeqEvent_Touch` TV **8879** (players only by default, unlimited):
  - **Touched** → Turn On a disabled `SeqEvent_Touch` on TV **13849** + TnToggleDestructible input 0;
  - **UnTouched** → Turn Off + input 1.
- **TV 13849's touch** has `IgnoredClassProximityTypes = PlayerController` (**AI pawns only**), bPlayerOnly false →
  **CauseDamage 200** → the window breaks (health 5).
- Its `HmDestructibleStateNotification` ("BOOM!") → the **PathNodes Turn On** (the horde's route opens).
- **Net:** while a player stands in TV 8879, a horde pawn entering TV 13849 smashes the window and opens three path nodes.
  TV 8879 is also the wave-6 gate's touch volume (b).

## 5. Follow-ups (2026-10-23)

**5.1 TnDroppedPickupWeapon (C)**
- **LifeSpan:** `PostBeginPlay` → `MultiplayerLifeSpan` **30 s** when the skill-data index is 0, else `CampaignLifeSpan` **120 s**.
  **TnSurvivalGame.GetSkillDataIndex = 1, so Escalation drops last 120 s.**
- **Taken by press only:** TnDroppedPickup CDO `bPickupWhenTouched = false`. The same `TakePickUp` path as factories: any
  player's contextual press, subject to `CanPickupInventory`. The horde doesn't (no pickup goal; H).
- **A weapon with no ammo is destroyed instead of dropped** (`TnWeapon.ShouldDestroyOnDrop`).

**5.2 Carried heavies (C)**
- **Ground speed × 0.75** while equipped (`GroundSpeedMultiplier` in both HeavyTurret_Rocket_SP and IonicDisplacer_SP data,
  applied on WeaponEquipped, removed on put-down).
- **Tossed on any switch away:** `WeaponPuttingDown.EndState` tosses a WeaponType 5 weapon. `TnInventoryManager.ChangedWeapon`
  tosses any heavy that isn't current. `TnPawn.Transform` to vehicle form calls `DropHeavyWeapons`.
- **Robot weapons stay in inventory.** The heavy isn't a slot (AddInventory re-types only primary / secondary). H: it's activated
  as the current weapon on pickup (stock AddInventory activation; the robot weapon is put down, which keeps it).
- **Ammo-based, not heat:** HeatMax 0 in the SP data; clip only (rocket 10, ion 300, no reserve).
  - Empty → `ShouldDestroyOnDrop` = true, so when it's put down it's destroyed instead of dropped.
  - H: an empty weapon triggers the stock switch-to-best, which puts the heavy down. The overheat-destroy path
    (`Overheated.OverheatEndComplete` → DestroyActor for a non-AI heavy) only applies to heat-based data.

**5.3 One press inside a generator volume with a pickup in reach (C mechanism, H priority)**
- `TnPlayerControllerSurvival.PressXButton` / `PressKeyboardInteractKey` first calls the base handler (the contextual command:
  pickup), then **always** `GeneratorInputHandler.OnPress`.
- So **the same press arms draining** (unless a revivable pawn is targeted) **and runs the active contextual command**. If the
  pickup command is the active one, it's taken too.
- H: which contextual command is active when both the generator and the pickup commands are registered (ContextualCommander
  priority; not traced).

**5.4 SetPlayerDefaults in SV (C)**
- `TnSurvivalGame.SetPlayerDefaults`: base defaults (the class loadout), then **remove the primary (type 1) and secondary
  (type 2) weapons**, then `CreateInventory(TnWeaponAssaultRifle)` (with bDoNotActivate when in vehicle form).
- **The grenade bag and the class abilities are untouched**: only the two typed weapons are removed.
