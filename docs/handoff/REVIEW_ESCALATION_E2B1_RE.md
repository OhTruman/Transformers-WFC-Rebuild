# Review: Escalation E2b part 1 (integration d6220894, Gameplay ab47bd6f) vs the original (RE, 2026-10-23)

**Checked:**
- `src/game/WorldSurvival.cpp` (`svTickKismet`, the Taken path), `Survival.h`;
- the generated `kismet` block in `escalation_MP_ESC_BrokenHope.json`.

**Against:**
- BrokenHope `Main_Sequence.Wall_Crashers` and `Genarators.*` Kismet (authored.db / next_map);
- `SeqAct_TnToggleDestructible`, TnSurvivalGeneratorBase.CheckPower (bytecode);
- ESCALATION_E2B_ANSWERS_RE §4.

**Verdict:**
- The data block matches the Kismet exactly:
  - walls 9486 → 10489, 11960 → 6870, 10811 → 15525, 7836 → 4868;
  - gates 11067 → zone 15078 @ 4, 14603 → 7763 @ 4, 11856 → 7739 @ 3, 8879 → 6666 @ 6;
  - window enable TV 8879 / AI volume 13849 / wall 5491 / 200.
- The crash walls, the round gates and the G1 Taken path are correct.
- **The window needs one change (W1)**, plus a missing hook (W2).
- And **I correct my own G1 wording** (§3): Gameplay's code is right and my sentence was wrong.

## 1. Correct (C)
- **Crash walls:** any pawn (players and horde) inside, once (MaxTriggerCount 1) → 200 damage to the wall (InitialHealth 1).
- **Round gates:**
  - `roundBegins >= round` turns the touch on (SeqCond_Increment A == B → Toggle Turn On of the disabled SeqEvent_Touch);
  - players only (the default bPlayerOnly), unlimited;
  - each enter → zone on, each leave → zone off, per pawn.
  - **A player already standing inside when the gate turns on fires nothing** until they leave or re-enter. That's the same in ours,
    since `gateInside` keeps tracking while the gate is off, and in the original, where Touched fires on touch begin.

## 2. The window

**W1 (change): arming isn't a latch, and it also toggles the window's invulnerability.** The original chain:
1. `SeqEvent_PreGameplaySetup` → `SeqAct_TnToggleDestructible` input **1** on 5491.
   - Per `SeqAct_TnToggleDestructible.Activated`: input 0 = `_Invulnerable = false`, **input 1 = `_Invulnerable = true`**, 2 = toggle.
   - **So the window starts invulnerable.**
2. TV 8879 (players, unlimited):
   - **Touched** → Turn On the AI touch of TV 13849 **+ ToggleDestructible 0 (vulnerable)**;
   - **UnTouched** → Turn Off **+ ToggleDestructible 1 (invulnerable)**.
   - Per pawn event, so the first player leaving disarms.
3. TV 13849 (AI only, once) → CauseDamage 200 → destroyed (health 5).

So the window is **armed and damageable only while a player is in TV 8879**. Ours latches `windowArmed` forever after the first
entry and never models the invulnerability.

**Fix:**
- `armed` follows TV 8879's enter / leave events, like the gates;
- the AI touch fires only while armed;
- the wall is damageable by **any** damage (H: player weapons too, through its generic-damage link) only while armed, and
  invulnerable otherwise.

**W2 (hook, for part 3 nav):** `SeqEvent_HmDestructibleStateNotification` on 5491 ("BOOM!") → **Toggle Turn On PathNodes 14807 / 606
/ 13593**. `PreGameplaySetup` turns them **Off**. Until the nav part lands, note that those three nodes are blocked until the
window breaks.

## 3. G1 Taken path: correct; my G1 sentence corrected
- **Ours:** Taken → hide; pool = max(0, pool − cost); if the generator was full → re-fire the actions (UnHide + volume off); the
  volume back on after 2 s. A take at level 0 only hides.
- **Original** `CheckPower` with `−cost` at **level 0**: `CurrentPower < 0` and `CurrentPowerLevel == 0` → **CurrentPower = 0 and
  return**, so there's **no event** and the item stays hidden.
- So the original cycle is: **buy → the bought item + one free respawn** (the first take's level drop re-shows it) → **the second
  take hides it** until bought again.
  - Any partial charge drained in between is wiped by that second take (power clamps to 0).
- **Gameplay's implementation matches this exactly.**
- My REVIEW_ESCALATION_E2A G1 "stays out for good" was wrong: it's one free re-show per purchase, not permanent. I've corrected
  that doc.

## 4. Minor
- Crash wall 15525's extra ToggleHidden / ChangeCollision have unset targets (no-ops). Ours does nothing for them, which is right.
- None of the round gates or the window has a runtime test yet. The logic above is by reading; the window needs W1 before a test
  is worth writing.
