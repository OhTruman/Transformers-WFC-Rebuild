# Pickup idle motion: weapon factories and dropped weapons (answer to Rendering, RE, 2026-10-23)

**Labels:** C = bytecode / CDO data (authored.db); H = stock UE3 native behaviour.

**Sources:** Engine.PickupFactory, Engine.DroppedPickup, TransGame.TnPickupFactory / TnDroppedPickup / TnDroppedPickupDefrag /
TnDroppedPickupWeapon / TnWeapon (`work/script/decomp`); CDOs in authored.db.

## 1. Weapon pickup factories (TnWeaponPickupFactory: no script of its own)

**Spin (C):**
- `PickupFactory.Pickup.BeginState`: `RotationRate = InventoryType.default.PickupRotationRate`, then **`SetPhysics(PHYS_Rotating)`**.
- `Pickup.EndState` sets PHYS_None, so the item spins **only while it's available**. It's stopped (and hidden) while sleeping /
  respawning.
- **`TnWeapon.PickupRotationRate` = (Pitch 0, Yaw 10000, Roll 0)** on the TnWeapon CDO. No weapon subclass overrides it.
  - That's **10000 / 65536 × 360 = 54.93° per second, one turn every 6.55 s**, about the factory actor's Z axis.
  - The mesh is a component of the factory, so it turns with the actor.
- H: the spin is continuous. Stock PHYS_Rotating with no bRotateToDesired adds RotationRate × dt every tick.
- The ammo crate and ammo beacon use the same Yaw 10000.

**Mesh setup (C):**
- `TnWeapon.CreatePickupFactoryMesh` clones the weapon mesh template's pickup mesh (`TnWeaponMesh.GetValidPickupMesh`), then:
  - `SetRotation(default.SpawnOffsetAngle)`: **TnWeapon = (Pitch 3000, 0, 0) = a 16.48° pitch tilt** of the mesh relative to the
    factory. Overrides: (0, 0, 0) on TnWeaponKamikazeMines, TnWeaponBombBase and TnWeaponFlagBase.
  - `SetScale(default.PickupScale)`: **1.0** (TnWeapon). Only **TnWeaponKamikazeMines = 1.2**.
- So the tilted mesh spins about the factory's vertical axis. The tilt is applied before the spin, in component space.

**Bob: none (C, by absence).** No script on PickupFactory, TnPickupFactory or the weapon classes offsets the mesh over time, and no
bob-like property exists in the CDOs. The only motion is the yaw spin.

## 2. Dropped weapons (TnDroppedPickupWeapon → TnDroppedPickupDefrag → TnDroppedPickup → DroppedPickup)

**The mesh (C):**
- `TnDroppedPickupWeapon.SetPickupMesh` applies the same **SpawnOffsetAngle** tilt and **PickupScale**.
- `TnDroppedPickupDefrag.SetPickupMesh` sets **`RotationRate = InventoryClass.default.PickupRotationRate` (Yaw 10000)** and
  **hides the pickup** until it lands.

**Physics path (C, chosen by data):**
- `TnDroppedPickup.SetPickupMesh` returns **before** the rigid-body setup when the pickup mesh has `bDisableAllRigidBody`.
- **`TnWeaponMesh.PickupMesh0` has `bDisableAllRigidBody = true`**, so weapons **don't** ragdoll / tumble. They take the
  DroppedPickup fall: H, stock default Physics = PHYS_Falling, tossed with the drop velocity.
- **On `Landed`** (TnDroppedPickupDefrag):
  - **`SetPhysics(PHYS_Rotating)`** on the floor actor;
  - `ClientLanded` **unhides** the pickup and starts the **Defragger fade-in** (the materialize effect);
  - once fully faded in, the HighlightFx material is attached and the PickupEffect particle system is activated.
- The press-pickup only works once fully faded in (`Pickup.ValidTouch`).
- **So a dropped weapon falls (hidden), lands, materializes and then spins in place at Yaw 10000 (54.9°/s) on the floor, with the
  16.5° tilt.** No bob.
- (The rigid-body / at-rest path, with CheckAtRestInterval 0.2 s, applies only to pickup meshes without bDisableAllRigidBody. No weapon
  default uses it. H: unless a specific weapon's mesh template overrides PickupMesh0.)

**End of life (C):**
- LifeSpan = 120 s in Escalation / campaign, 30 s in versus (`TnDroppedPickupWeapon.PostBeginPlay`, by skill-data index).
- `Pickup.BeginState` sets a timer at **LifeSpan − 2 s** → state FadeOut:
  - `TnDroppedPickupDefrag.OnFadeOut` removes the highlight, stops the effect, and runs the **Defragger fade-out**;
  - LifeSpan = DefragTime;
  - touches are refused.
- Its OnFadeOut **doesn't** call DroppedPickup's (Yaw 60000) fast spin, so the spin rate is unchanged during fade-out.

## 3. For the build
- **Factory weapon:** mesh at the factory origin, local pitch +16.48° (rotator 3000), scale 1.0 (mines 1.2); the factory yaw
  advances 54.93°/s while available. No bob.
- **Dropped weapon:** same tilt and scale; hidden while falling; on landing, a defrag fade-in, then the same 54.93°/s yaw spin;
  a defrag fade-out at 118 s (Escalation).
