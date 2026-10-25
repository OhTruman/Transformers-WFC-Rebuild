# Playtest 4 (2026-10-24c): Gameplay findings

1. **Escalation wave 1 stalls: fixed (the cause), `GAMEPLAY_ESC_SPAWN_REACHABLE.patch` (on 28d7807b).**
   - Before (head, invulnerable bot-driven human, 360 s): wave 1 cleared on none of Tranquillity / District / Pulse / Static /
     Remnant. A periodic diagnostic showed the survivors with a target but no path: they spawned where our generated bot nav
     does not reach the players.
     - Authored TnSurvivalSpawnPoints with no nav cell nearby: 40 on Tranquillity, 35 on Remnant.
     - Or points in a nav piece sealed from the players (Remnant's opening top zones 11696 / 1584 / 9840).
     - So they stood behind barriers / in walls, and the wave (budget spent AND horde dead) could never clear.
   - Fix (PC ADAPTATION for our nav's coverage; the original AI used its own path network):
     - SpawnDude's weighted top-3 draw runs over the zones that have a point reachable from the players' nav piece;
     - the point moves to that cell's centroid (40 m, same storey);
     - with none reachable, the original list is used.
   - After:
     - Pulse clears wave 1 and starts wave 2.
     - On the other maps the horde now comes to the player (Remnant survivors 3.8-8.7 m away with paths). The remaining time
       is the harness human's kill speed, not a stall (Remnant's HoloBrutes take backpack shots).
     - One Tranquillity spider still had no path.
   - SIMHASH changes by design (Escalation).
2. **DLC doors (Rendering's note).** The sim does load them: District 22, Static 34, Pulse 2 transforming actors (the per-map
   fallback in TransformingActors::load). Tranquillity's per-map manifest has 0 actors, so that is an AssetTools export gap; its
   blue barriers are the floodgate mechanisms, which load (3 floodgates, 2 blockers). The rendering side is Rendering's.
3. **Map escape by double jump: not done (usage).** The authored BlockingVolumes are already in the movement collision
   (collision_pawn.glb: 71 BlockingVolume nodes on Streets). Escapes over ordinary walls need a per-map audit of where the
   original relies on height or volumes we lack (top-of-wall caps, DynamicBlockingVolume, KillZ recovery). Open for the next
   session.
4. **Hit registration: report.** No defect found. Rendering's windowed proof after items 29 / 30: trace vs drawn view mean
   0.02 px, worst 0.8 px over 30 shots. Hitscan and projectiles start on the camera's crosshair ray (projectiles from the muzzle
   toward the trace hit).
5. **The cubes, one line for the user:** the glowing Energon cubes restore health and are only picked up when you are below full
   health; ammo crates only when a weapon is short of ammo (as in the original).

FINAL_BASE_ITEMS 32: cut with work/cut_base_items.sh + work/fbi_verify<N>.sh once this patch merges (needs the merge sha).
