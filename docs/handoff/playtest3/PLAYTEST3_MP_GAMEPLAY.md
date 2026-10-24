# Playtest 3 (2026-10-24), multiplayer: Gameplay's findings and patch (on c185ef12)

Patch: `GAMEPLAY_PLAYTEST3_MP.patch` (glue `edit_playtest3_mp.js`).

1. **P0 aim, fixed (the vertical part).**
   - Cause: `PlayerController::fireRobotShot` applied the weapon's permanent camera pitch step (TnRecoilCameraBehavior
     PitchAdjustmentDeg, `onLocalShot`) before it built the shot from the camera rotation. So every robot shot carried its own
     recoil, above the crosshair: Ion Blaster 0.1 deg, Sniper 1.0 deg at its 20 deg fine-aim FOV, Bazooka / Heavy Pistol
     1.5 deg, EMP Shotgun 4.0 deg. The shot is now built first, then the recoil steps the camera (the vehicle path already did).
   - Headless proof (WFC_AIMLOG=1, Streets TDM 60 s, the bot-driven human): the shot rotation minus the trigger's rotation.
     - Old order: +0.100 deg pitch on all 61 Ion Blaster shots, +0.050 deg on all 78 Assault Rifle shots.
     - Fixed: 0.000 deg on all 204 shots (Truck / Car2 / Tank3).
     - Yaw: 0.000 deg in both builds.
   - **So "left" is not the sim.** The shot leaves along the camera's own centre ray (start trace = the crosshair ray nearest
     the pawn). A shot landing left of the crosshair means the drawn crosshair is not at the 3D projection centre: the HUD rect,
     the split-screen view rect or the reticle anchor. Route to Rendering / Frontend.
2. **XP on kills, fixed.**
   - Cause: `GameFlow::canGainXp` applied the original online rule (private earns nothing) whenever the session role was not
     Offline. A private match is now a hosted session even when nobody else is in it.
   - Fix: a host with no session members keeps the PC offline rule.
   - New frontend test `playtest3.solo_host_xp` passes (all frontend tests pass).
3. **Scatter Blaster, no bug in the sim.** Systems measured it headless: 6 shots 0.4 s apart, then the 2.5 s reload, all from
   the Shotgun WEPDATA. The ~3 s is the empty-clip reload. Open for RE: does the original TnWeaponShotgun reload per shell, or
   let fire cancel a partial reload?
4. **Orbital Beacon, fixed.**
   - Cause: the versus pawn markers asked for the setup "TransformerEnemyMarkerSetup". TnObjectiveMarkerTypeTransformerVersus
     authors "EnemyMarkerSetup" (render/_ui/hud_markers.json), and HudMarkers skips an unknown setup, so no enemy marker ever
     drew. This hid Orbital Beacon's whole effect and Hard Lock's marker.
   - The sim side was right (WFC_PARTICIPANTTEST).
   - The original's effect (no RE material; derived from the data): TnBuffSeeEnemyObjectiveMarkers on the team for 30 s, i.e.
     enemy objective markers unless the enemy has Warcry, plus the MP_OrbitalReconActivated{Self,Friendly,Enemy}Dialog
     announcer lines (AssetTools assets.json) and the streak icon.
5. **Pickup cube, not identified.** The interact input path is intact: a pad X press sets the pickup request, the Code of Power
   / bomb are in `localInteractAvailable`, and the CTF test passes. The placed ammo / health / overshield crates are touch
   pickups with no button: a full-ammo touch does nothing, as in the original. Which object the user meant needs one line from
   the user or a screenshot.
6. **Streets central cylinder, to AssetTools.** The likely mesh is `TubeDecoCylinder_STAT` (6 placements, collide / block
   true): a blue core section plus an `IAC_Glass_Trans` glass section. The map export builds static-mesh collision from the
   render sections ("simple collision bodies not extracted"), so the transparent glass shell becomes an invisible, oversized
   wall. Needs AssetTools: exclude the non-colliding (translucent) section, or extract the mesh's collision body.
7. **Escalation solo.** No minimum-player gate on the launch path. lobby.numRequiredPlayers only drives the public autostart,
   and the sim's Escalation launch runs with one human and no others (headless FLOWSIMTEST ESC / ESCTEST). The menu side is the
   Frontend's.

SIMHASH note: item 1 changes the local player's shot directions (deliberate); bot-only rows are unaffected.

## Follow-ups (Integration / RE / Rendering, 2026-10-24)

- **Aim, part 2: `GAMEPLAY_AIM_VIEW_ROTATION.patch` (on c39b7d29).**
  - Robot and vehicle shots now use the camera POV the frame draws, strategy wiggle and per-shot shake included. This follows
    the UE3 rule: Pawn.GetBaseAimRotation -> Controller.GetPlayerViewPoint = the PlayerCamera's POV after its behaviours
    [UE3 engine script, HIGH].
  - Rendering's audit: the crosshair is the projection centre. Ours traced the unshaken orbit rotation.
  - Headless (WFC_AIMLOG, Streets 60 s): shot vs trigger 0.000 deg on 139 shots. View vs orbit at the trigger was also
    0.000 deg in these runs: each weapon's ShakeFalloffDuration (Assault Rifle 0.06 s, Ion Blaster 0.06 s) ends before its
    next shot. So the shake can't explain a steady left offset at the bot's fire cadence.
  - The shake units (WeaponCameraTable Intensity x the strategy's 3 / 5 deg spectrum) still need RE to confirm degrees vs
    rotator units. A units error would only change how big the shake looks, not where the shot goes, now that aim follows
    the drawn view.
  - Rendering's windowed proof shot comes next.
- **Scatter Blaster.** RE confirms one reload timer for all TnWeapons (WeaponReloadAnimTime), the whole clip at its end, and a
  cancel that loses it. Ours matches: one reloadTimer, finishReload fills the clip, canFire is false while reloading. So the
  ~3 s (2.4 s to empty the clip at 0.4 s per shot, then the 2.5 s reload) is authored. RE's "a shot consumed during the reload
  completes it instantly" is a hypothesis only, and with an empty clip nothing can be consumed, so it is not implemented.
- **Streets cylinder.** It is not built from render sections: `TubeDecoCylinder_STAT` collides with its extracted simple
  collision body (collision_pawn.glb kind prop_simple), a box of 32.2 x 30.6 x 41.0 units. The visible mesh is a radius ~15.2
  blue cylinder over a radius ~13.9 glass section, and the box spans both, so its corners stand up to ~7 units outside the
  visible cylinder. That is the original's authored collision, so the "skip glass sections" fix does not apply. Keeping the
  original box is the faithful choice. A tighter hull would be a PC adaptation and is the user's decision.

## User follow-up answers (2026-10-24 ~17:30)

- **The cubes are the placed pickups, and they work.** Streets has 15 ammo crates, 9 health "Energon" cubes (sound
  HEALTH_PU_ENERGON) and 1 overshield, all touch pickups with no button.
  - Headless WFC_PICKUPTEST on Streets: each is taken on touch, plays its sound, sleeps, and respawns after its authored time
    (ammo 30 s, health 60 s, overshield 120 s). A health cube re-takes when you stand on it at respawn (CheckTouching).
  - Payloads: health +50 (AddedHealth); ammo refills; overshield.
  - As in the original, a pickup is not taken when you don't need it: a health cube at full health, an ammo crate at full
    ammo, an overshield at a full shield. That is the likely reason the user saw "nothing happened".
  - Fixed: `GAMEPLAY_AMMO_CRATE_ALL_WEAPONS.patch` (on 5239fc0f). The ammo crate checked and refilled only the active weapon
    (a leftover from the one-weapon slice). It now covers every primary / secondary / vehicle weapon (ValidWeaponTypes), and
    WFC_PICKUPTEST passes unchanged.
- **The Streets crater hitbox is `ENV_CraterUpNoRing_STAT`** (10 colliding placements). Pawns use its simple collision
  (physics.json: UseSimpleBoxCollision true by default, none authored).
  - The collision is one convex hull: 18 vertices, ±27.7 m wide, 7.8 m tall. The visible mesh is ±18-19.5 m, so the hull
    reaches 8-10 m past the visible crater edge.
  - Its shape is a flat lens. The underside rises from 0 at the centre to a knife edge 1.1 m above the mesh origin at the
    27.7 m rim; the top faces slope 21 deg then 11 deg.
  - Measured against the collision floor just outside the rim at 10 placements, that edge stands 0.4-0.8 m above the floor.
    Our step height is the original's 0.35 m (TnRobotForm MaxStepHeight 35 UU), so our knee probe (a horizontal ray at
    0.55 m) hits the downward-facing underside and stops the pawn, far from anything visible.
  - A UE3 cylinder sweep that meets the walkable 21 deg face would most likely slide up it, so the original probably let
    players walk over the lip. That is plausible from the engine, not confirmed.
  - Nothing is missing: every collidable near the crater has a drawn mesh, and no export failed. The difference is our
    provisional movement model (centre point + knee probe, no cylinder sweep) against the authored convex hull.
  - Options, for Integration / the user:
    - (a) movement model: a contact with a walkable-sloped hull face lets the pawn ramp up, as the UE3 sweep does
      (the faithful fix; changes walking everywhere, needs a broad regression);
    - (b) a per-mesh data override: per-poly collision for ENV_CraterUpNoRing_STAT only (narrow, but not the original
      collision);
    - (c) keep it as is.
