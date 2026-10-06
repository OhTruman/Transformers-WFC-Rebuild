# Systems M08d glue addendum: Gameplay Pass 24b / 24c signals

Apply this after merging agents/gameplay Pass 24b (d42367f) and 24c, on top of `SYSTEMS_M08D_integration_glue.patch`. It replaces two glue fallbacks with Gameplay's own state. Gameplay agreed to these signals and to the `moveIntent()` accessor.

## 1. Tank 180 quick turn (Pass 24b: `VehicleState::quickTurnSerial`)

In `World::tickVehicleBoost`, in the Systems M08d signals block, before `tickVehicleAudio(dt, s);`:

```cpp
s.special180 = vs.quickTurnSerial != quickTurnSeen_;   // TnTankForm.ClientPlaySpecialMoveSound -> PlayOneEightySound
quickTurnSeen_ = vs.quickTurnSerial;
```

In `World.h`, next to `audioPrevGrounded_`:

```cpp
unsigned quickTurnSeen_ = 0;
```

## 2. Repair Ray explicit beam (Pass 24c: `World::fireRepairBeam`, HudState `repairBeam*`)

1. **Remove** the trace-derived block that the glue patch put in `fireHitscanWith`:
   `if (CharacterAudio::weaponIsBeam(firedCls)) { ... onBeamWeapon(...) ... }`
2. **Remove** the beam timeout at the top of `World::tick`:
   `beamSinceShot_ += dt; if (weaponAudio_.beamActive() && ...) onBeamWeapon(..., false, 0);`
3. **Add** the state-driven call in `World::tick`, right after Gameplay's beam timer update (agents/gameplay d0452a5):
   `if (repairBeam_.time <= 0.0f) repairBeam_.active = false;`

```cpp
// [Systems M08d] TnWeaponBeam / TnWeaponRepair sound state from Gameplay's beam (Pass 24c): teammate -> heal loop
// (WP_Fire), enemy -> damage loop (WP_FireSecondary), no pawn -> neither; release -> WP_LoopingTail.
{
    const bool firing = repairBeam_.active && repairBeam_.time > 0.0f;          // = HudState::repairBeam
    const int target = !firing ? 0 : repairBeam_.healing ? 1 : (repairBeam_.target >= 0 ? 2 : 0);
    if (firing || weaponAudio().beamActive())
        onBeamWeapon("TransContent.TnWeaponRepairRay", firing, target);
}
```

* If Gameplay fires another beam class (the heavy or mounted Repair Ray), pass that weapon's class (`"TransContent.TnWeapon" + w.def->id`).
* `repairBeamStart` / `repairBeamEnd` are the beam's end points (`Vec3`), for Rendering's beam effect. The sound needs only the firing / target state: the loops are owner-attached, as the original's weapon-mesh sounds are.
* Ownership: death, class change and match reset already stop the beam (`weaponAudio_.stopAll`).

Systems side: unchanged. `World::onBeamWeapon(cls, firing, target 0/1/2)` and its sound rules are as in M08d. The beam's heal, damage and tail loops are covered by the Systems suite.
