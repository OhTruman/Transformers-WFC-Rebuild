# Review: Escalation E2a (integration 29c1ce7d, Gameplay 48e594b0) vs the original (RE, 2026-10-23)

**Checked:**
- `src/game/WorldSurvival.cpp`, `Survival.h`, `PickupFactory.{h,cpp}`, `PlayerController`, `World.cpp`, `Match.cpp`;
- `tools/gameplay/gen_escalation.py` (the diff 29c1ce7d~1..29c1ce7d).

**Against:**
- the bytecode: TnSurvivalGeneratorBase, TnSeqEvent_SurvivalGenerator, TnPlayerReplicationInfoSurvival,
  TnPlayerControllerSurvival, HmButtonInputStateHandler, PickupFactory;
- the BrokenHope / Remnant Kismet (`next_map/*_kismet.json`);
- ESCALATION_E2_ANSWERS_RE, the E1 review, `escalation_zone_order.json`.

**Verdict:** the zone order, sort, F1–F3 and the drain / power economy are correct. **One behavioural finding (G1) in the item
generators' re-arm.** Two minor notes.

## Correct (C)
- **Zones list:** the active zones in level actor order (the generator sorts the zones by the RE actor order), then activations
  appended and deactivations removed, and every sort starts from that list.
- **The msvcQsort C++ port** is line-for-line equivalent to `tools/msvc_qsort.py`:
  - shortsort ≤ 8;
  - median of three with strict `> 0` swaps;
  - the two loguy loops and the higuy loop;
  - `mid == higuy → mid = loguy`;
  - the equal-skipping back-off;
  - the smaller-side push order.
  - The comparator is descending (A < B → 1).
  - ESCTEST checks the initial orders against the test vector (per the commit message).
- **F1:** PreGame's clock now runs from setupSurvival (level load) through PendingMatch. MatchAutoStartCountdown is 15.
- **F2:** `svSetZoneActive` appends / removes and re-sorts all five types.
- **F3:** Round Begin / Round End are counted at the right points. Running the map ops they drive (BrokenHope: a
  TransformWorldProp, remote events) is still to do.
- **PowerReward:** the killer must be a human player; += the enemy's PowerReward, clamped at 9999.
- **Drain:**
  - per occupying human pawn, accumulated into 30 Hz ticks;
  - `floor(max(1, tick·rate))`, capped at the remaining cost and at the player's power;
  - the shared pool; base DrainRate 10 when unauthored;
  - `_AllowDraining` set only by a **press** while inside, reset on entering, cleared on release (= HmButtonInputStateHandler event
    1 / 2 / 4 and NotifyUseableGenerator);
  - 0 drained → the fail flag;
  - no draining once at full (AtMaxPowerLevel), or while the volume is off.

## G1 (behaviour): after the item is taken, the original **unhides it again**, so a bought item stays out for good
The map Kismet per item generator (`Genarators.AmmoGenerator_Seq_*`, and the same for health / overshield / weapon) is:
- PreGameplaySetup → **Hide**;
- `New Power Level` → **UnHide** → the generator's TriggerVolume **Turn Off**;
- `Taken` → **Hide** → `ModifySurvivalGeneratorPower(−cost)` → Delay 2 s → volume **Turn On**.

The script makes the `−cost` re-fire `New Power Level`:
1. `OnModifySurvivalGeneratorPower`: `CurrentPower += −cost` → `CheckPower`. CurrentPower < 0 with CurrentPowerLevel = 1, so:
   level → 0, CurrentPower += PowerLevels[0] → 0, then **`OnPowerLevelChange(false)`**.
2. `OnPowerLevelChange` calls `TnSeqEvent_SurvivalGenerator.ActivateEvent(level)` for **both** positive and negative changes.
   The event has a single output, "New Power Level", with no level filter (MaxTriggerCount 0).
3. So the drop runs the `New Power Level` chain again: **UnHide** the factory (after Taken's Hide, since the Hide → Modify order is
   sequential in that chain) and **Turn Off** the volume. Then the 2 s Delay **turns the volume back On**.

**Net original behaviour:** the first purchase unhides the item permanently. From then on it's a normal factory that respawns
after its RespawnTime (2.0 s). The generator returns to level 0 and is chargeable again, but buying it again changes nothing.
- The Kismet event's frame timing is H (queued activation); the outcome is C.
- **Ours:** re-hides after Taken and requires a new purchase ("one purchase per charge", from my E2 §1, which missed the re-fire).
  **My E2 answer §1 was wrong on this point; corrected here.**
- **Fix:**
  - **Faithful:** on Taken, pool → 0 (level 0), the volume **off then on after 2 s**, and the factory **stays unhidden** (not
    re-hidden).
  - Because the drop re-fires the whole chain, **apply the generator's action list again on the drop** (it's idempotent for these
    item chains: UnHide + volume off).
  - That also leaves the pickup on its normal 2 s respawn.
- This is a gameplay-balance decision for Integration / the user if "pay per item" is preferred. The original is "pay once".
- The same re-fire applies to any generator whose power drops. Only the item generators are ever lowered (by their Taken
  chains): door and zone generators have no −power ops, so they fire once.

## Notes (minor, no change needed now)
- **N1:** `PickupFactory::tick` returns while hidden, which freezes the respawn timer. In the original the Sleeping-state timer
  runs regardless of bHidden (H, stock), so a factory unhidden mid-respawn is available sooner. With G1's fix the hide is only
  the pre-purchase state, so it doesn't matter.
- **N2:** an original quirk: the generator's `_Draining` is one flag shared by every occupant. A second player standing in the
  volume without holding drains 0, which plays `DrainingFailedSound` once (while `_Draining`) and clears the flag. That's
  cosmetic; ours flags the fail only for a holder. Note it for the audio pass.
- **E4 reminder:** the original ignores the generator press while a revivable pawn is targeted (revive first). Add that when the
  downed state lands.
