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
