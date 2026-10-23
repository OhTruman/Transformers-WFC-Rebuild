# Togetherness volumes, TnSeqEvent_Together, and BuddyLead emotes (RE, 2026-10-23)

Closes the CAMPAIGN_RE_INDEX items "TnSeqEvent_Together not traced" and "TnTogethernessVolume timers; BuddyLead emote finder
details".
Labels: C = TransGame script bytecode (decomp) or authored.db; H = inference.
Data: 153 TnTogethernessVolume; 13 TnSeqEvent_Together; 905 TnInteractEmote (48 emote blueprints).

## 1. TnTogethernessVolume (C)
All of this is server-side: every handler returns early on a client (NetMode 3).

**Friendlies** (`GetFriendlies`) = every TnPawn that is **human-controlled**, or whose controller is an **active member of the
campaign AI squad**.

**Touch / UnTouch** (TnPawn only; the volume keeps a `TouchedPawns` list):
- **Touch:** add the pawn.
  - If every friendly is now inside → event **AllIn**.
  - Else → event **Touch**.
- **UnTouch:** remove the pawn.
  - If every friendly is now outside → event **AllOut**.
  - Else → event **UnTouch**.
- **Every event first clears both wait timers**, then reaches every TnSeqEvent_Together in the volume's GeneratedEvents.

**TnSeqEvent_Together** (outputs 0 AllIn, 1 AllOut, 2 AllInTimeout, 3 AllOutTimeout; defaults AllInWaitTime 1 s,
AllOutWaitTime 1 s; 12 of the 13 authored events keep the defaults):

| volume event | the Together event does |
|---|---|
| AllIn | fire output **AllIn** |
| AllOut | fire output **AllOut** |
| Touch (someone entered, not all in) | start timer **AllInWaitTime** → when it expires and **≥ 1 pawn is still inside**, fire **AllInTimeout** |
| UnTouch (someone left, not all out) | start timer **AllOutWaitTime** → when it expires, fire **AllOutTimeout** |

So the timeouts fire if the group doesn't complete entering or leaving within the wait time. Any later touch or untouch restarts
the clock.

**`OnActivateTogethernessVolume`** (the Kismet action; `Action.InstigatingPawn`, `Action.HideTeleports`):
1. The AI squad's `SpawnPendingMembers(0, true)`: buddies waiting to respawn appear now.
2. `TnGame.NotifyPlayerWithinTogethernessVolume(PC)`. This is empty in TnGame. The campaign's TnTeammateSpawner prunes that
   player's records (`PrunePlayerRecords`).
3. **`TeleportFriendlies`:**
   - Skipped if teleportation is disabled or the instigator isn't human.
   - **> 3 friendlies only logs an error** (it still runs).
   - **Teleport-point ordering:**
     - `UpdatePlayerVisibilities`: for each human inside the volume, count how many players see each point. The test is the PC
       camera position, with the cone half-angle = FOV / 2 around the pawn's facing (yaw, pitch and roll from Rotation).
     - Points are sorted ascending by (SeenByPlayer, then SeenByPoint). SeenByPoint is precomputed at PostBeginPlay:
       point-to-point, with a 90° half-angle around each point's rotation.
   - **Start index:** `AIOutOfView` (HideTeleports) → the least-seen point, walking forward; otherwise the most-seen point,
     walking backward.
   - **Pass 1:** AI friendlies outside the volume. **Pass 2:** human friendlies outside the volume. Pawns already inside are never
     moved.
   - **Per pawn:** take the next point; `IsSafeSpawnLocation(point, radius, height)`, else try the next. Running out of points
     logs an error and stops.
   - **TeleportPawn:** `Teleport(point.Location, point.Rotation)`, SetViewRotation, ClientSetRotation, and **revive if downed**.

## 2. BuddyLead: the buddy walks ahead to an interact emote (C)
This is a goal in Buddy, Engineer, NoWeapon_Buddy, Engineer_Boss and NPC_Healer goal sets (priority: CAMPAIGN_AI_LAYER_RE §2).

**PopulateGoals:**
- needs a **Move-To-Objective subject** (the objective actor) and a **`_Friend`**, the player the buddy follows;
- `Emote = Finder.FindEmote(friend, objective)`;
- the goal is "InteractedWith(Emote)". The buddy paths to the emote's hotspot and plays it.

**Emote finder (`TnEmoteFinder`, defaults MinEnterTime 4 s, MinExitTime 1.5 s, UsageInterval 300 s):**
1. **The friend's path to the objective:**
   - `Pathing.FindPath(friend → objective)` with walk + jump forced on;
   - the friend's anchor is prepended;
   - truncated at the first **gated, closed** node (result 2 = "blocked by gate"); no path → no emote.
2. **Candidates:** TnInteractEmote actors registered in the path's navigation cells, up to a closed gate. The emote's
   **HotSpotPathNode** must not be occupied by another controller. Each is counted once.
3. **The path is smoothed** (PathSmoother) from the friend's location.
4. **Time to reach an emote** = distance along the smoothed path to its closest point to the emote / speed. **Speed** = max(friend
   GroundSpeed, |friend velocity|).
5. **Pick:** skip emotes reached in **< 4 s**, and emotes used within the last **300 s**. Of the rest, take the **soonest**.
   - If the friend's whole remaining path takes < 4 s, take the **furthest** emote instead (it can be null).

**Keep / drop (VerifyRunningDesiredEffects):**
- Re-validated **every 0.5 s**. The goal drops when the objective or friend is gone, or when the emote becomes invalid.
- **IsEmoteValid:**
  - the path still passes through the emote's cell;
  - **invalid if** (time to reach − (the out-anim length if the buddy is already using it)) < MinTime, where MinTime = 1.5 s if
    in use, else 4 s;
  - **unless** the friend is within 4 s of finishing the path;
  - or the path is gate-blocked, and a fresh FindEmote returns the same emote or none.
- So the buddy leaves an emote once the player gets within ~1.5 s of it (plus the out-anim), and lets the player catch up.

**The emote actor (TnInteractEmote):**
- In / Loop / Out animations come from its EmoteBlueprint (e.g. Emote_Open_NoWave, Emote_PointAt) via a skeleton → AnimSet map.
- Defaults:
  - AI-only;
  - register radius 500 (the nav-cell registration);
  - position / rotation adjust speed 200 UU/s / 5;
  - "close enough" distance 10 UU.

## 3. For Gameplay
- **Volume:** keep TouchedPawns, with the friendly set = humans + active squad AI.
  - Fire AllIn / AllOut at once.
  - Otherwise start the 1 s timeout; any membership change restarts it.
  - Activation = spawn pending buddies + teleport outsiders (AI first) to the least-seen safe points (if HideTeleports) + revive.
- **BuddyLead:** pick an emote on the player's path at ≥ 4 s ahead, not used in the last 300 s; walk there and play the in / loop
  anim; leave when the player is < 1.5 s away; re-check every 0.5 s.
