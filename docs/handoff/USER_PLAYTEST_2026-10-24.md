# Human playtest findings 3, 2026-10-24 (package 48e42af6), condensed from the user's 72-item report

Verdict: graphics / settings much better; MP closer than campaign; Escalation improved, incomplete; campaign far off.
Priority (user): 1 MULTIPLAYER, 2 ESCALATION, 3 shared frontend / settings, 4 campaign later (documented, deferred).
Remaining usage is limited (14 %): batch work, no per-patch chatter, no confirm sheets.

## Multiplayer (owner order per the user)
1. Aim: shots land left / top-left of the crosshair (sniper obvious, shotgun too; likely ALL weapons): audit crosshair
   position, camera centre, trace origin / direction, muzzle, fine aim, hitscan + projectile. [Gameplay + Rendering]
2. XP no longer awarded on kills (worked in the previous build): kill / assist / objective XP, challenges, persistence.
   Suspect the solo-host offline path (gl.online false) or profile routing. [Gameplay + Frontend]
3. Scout Scatter Blaster: ~3 s unable to fire after fire / reload (reload state, anim lock, input gating). [Gameplay]
4. Join-code panel leaks into nested menus (Create a Character: over abilities / upgrades / chassis). [Frontend]
5. Create a Character: Decepticon Chassis control still misplaced (reported twice). [Frontend]
6. Vehicle headlight flicker (reported before). [Rendering]
7. Invisible / oversized collision around the central blue / black cylinder feature (Streets). [Gameplay + AssetTools]
8. Orbital Beacon killstreak: no visible effect (verify the original's effect, AV feedback). [Gameplay]
9. Perf regression at max settings: ~82 fps at 1440p Enhanced + RT, 5v5; earlier builds faster. [Rendering]
10. Scoreboard / pre-match names stacked; results layout (row height, spacing, icons, columns, bot counts). [Frontend]
11. Countdown cancel: the user still could not cancel (our cancel = Back during the countdown): make it an obvious
    control (prompt text / button) and verify with mouse + controller. [Frontend]
12. Start Game looks grayed out but works. [Frontend]
13. Pickup cube could not be interacted with (controller X tap = reload, hold = pickup per RE: show the hold prompt;
    verify the interactable). [Gameplay + Frontend]
Keep: team switching, Game Settings, bots, match start / completion / results, killstreak earning, RT look, colours.

## Escalation
1. Solo play: hosting alone must start without another human. [Frontend + Gameplay]
2. Only District offered: verify the ESC map list (Remnant / BrokenHope + 4 DLC must be selectable). [Frontend]
3. Duplicate Optimus Prime in the roster. [Frontend]
4. ~30 s load into Escalation: profile (suspect the new match-load preloads of every chassis / cue set / hull). [Systems]
5. Then an end-to-end ESC gameplay validation + rewards.

## Settings / frontend
- First-boot modals stack (Brightness + HD prompt together): queue them. Graphics opens at the bottom: open at top.
  Scroll arrows jump position: fixed position, visibility only. Preview panel: standard size; LEFT = baseline OFF,
  RIGHT = selected; hover-to-zoom on the image; FSR 3.1 Q / B / P previews; Frame Generation = text, no image; verify the
  RT preview (Realistic much brighter than Faithful: real or wrong?); add Volumetric + Lens Flare previews. Language
  out of Video (Audio / general); Split-Screen Layout to the bottom near FPS Counter. Keep menus at 60.
- Main-menu vignette fades out after a couple of seconds. Backing out of Campaign lands in Escalation.

## Campaign (documented, deferred): chapter VI unlocked on a fresh profile (RE: A1 is unlocked by design: explain in the
UI or confirm), chapter / checkpoint selection UX + oversized checkpoint image, cutscene audio mix (gameplay audio over
the cinematic, a looping sound), interaction prompt does nothing, floating arms / untextured actors, long scripted camera
with no context, no direction after it, NPCs inactive, exit returns to Escalation.

## Exit flows (user): MP: menu -> Private Match -> Game Settings -> team -> character -> map -> start -> cancel -> start
-> spawn -> move -> aim = hit at the crosshair -> kill -> XP -> weapons / reload -> ability -> killstreak earn + use ->
transform -> complete -> results -> lobby. ESC: menu -> Escalation -> host SOLO -> valid map -> character -> start ->
load -> spawn -> waves -> rewards -> complete / fail -> return.

## User follow-up (2026-10-24 ~17:30)
- The "pickup cube" = the cubes that spawn around the map (the Energon / Allspark cubes): the user expects to pick them
  up and could not.
- The hitbox = the central glowing blue Energon pool / crater with the dark spiky debris on Streets (photo:
  playtest3/streets_hitbox_pool.jpg), NOT a cylinder: walking toward it blocks the player, "which makes no logical
  sense unless it's missing additional assets for it".
