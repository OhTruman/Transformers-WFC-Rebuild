# Human playtest findings 4, 2026-10-24 (package ae63135a), condensed; usage left ~9 %: one more build, no waste

Fixed per the user: join code leak, chassis alignment, XP, aiming improved, ESC map selection + load into gameplay.
Priority: MULTIPLAYER, ESCALATION, shared frontend, perf / polish, campaign later. Previews: a time-boxed QoL item.

## Multiplayer
1. Private Match bot counts must be team-aware: the human's team gets 1 human + up to N-1 bots, the other team up to N
   bots (today Autobots max 4 / Decepticons max 5 regardless of the human's side); all player-limit configs. [Frontend]
2. Game Settings vanish after leaving Private Match and re-entering (only the first open works). [Frontend]
3. Map escape: double-jumping against ordinary walls climbs out of the playable space; then stuck outside for good.
   Systematic boundary pass (blocking volumes, ceilings, kill / recovery volumes, authored boundaries; the shared
   collision-generation rule across maps). A few intentional high spots are fine. [Gameplay (+ map data)]
4. Hit registration pass: trace / projectile direction, crosshair agreement, hitboxes, local latency, hitscan
   registration; no aim assist. [Gameplay]
5. XP notifications overlap: queue / stack / combine. [Frontend]
6. "Cubes that do nothing": the Energon / ammo touch pickups only act when the player is hurt / short; confirm with the
   user; if these are other placeholder objects, identify. [Gameplay: one line]
7. Main-menu vignette still disappears ~1 s after entering the menu (reported 3 times; not reproduced windowed at
   1600x900: reproduce at the user's mode: 2560x1440, fullscreen likely). [Frontend, one card slot]
Keep: aim, XP, join panel, chassis, team switch, Game Settings, match completion.

## Escalation (Forsaken = Tranquillity)
1. Wave 1 never progresses: ~3 soldiers + 1 spider killed, then nothing; HUD stays on wave 1; a flashing icon.
2. Enemies spawn but are trapped behind the blue barriers (a machine-gun soldier seen stuck); a spider stuck in a
   wall. Spawn locations, barrier / door states, wave release, nav through ESC geometry, the wave controller waiting on
   permanently stuck enemies. [Gameplay: full wave-1 flow with a human-like player on Tranquillity + the other ESC maps]
3. Purchasable doors: "E to open door for 300" floats at a strange spot, no terminal / button, no barrier visual: an
   open doorway with invisible collision. Reconstruct the original's door presentation (model / energy barrier /
   terminal, prompt placement, cost, opening animation, collision + nav update). [Rendering presentation + Gameplay]
4. Collision / clipping into geometry, glitching through walls by jumping: broader ESC collision pass. [Gameplay]
5. Water / liquid material on Forsaken looks wrong. [Rendering, later]
Exit flow: host -> map -> solo start -> countdown -> wave 1 spawns completely -> enemies reach the player -> doors /
barriers work -> all wave enemies killable -> wave 1 completes -> wave 2 -> multiple waves without stalling.

## Settings / previews (time-boxed)
- Hover zoom: keep the FRAME fixed, zoom the CONTENT inside it toward one per-setting focal point, ease back on leave.
- Same frame size for both images; HD's second image overflows the panel; consistent spacing.
- Volumetric + Lens Flare pairs look identical: pick a scene where the effect is obvious or use text instead.
- Ray Tracing: verify the Realistic preview is the real rendering (do not over-light it).
- If previews keep costing effort: simplify or remove; gameplay first.

## Disk cleanup (conservative): delete only obsolete generated / rebuildable output (superseded packages, stale copied
release dirs, old validation builds, benchmark / staging output); never source, worktrees, Git, the current package
ae63135a, Game Dump, ExtractedAssets, HD work, manifests, toolchains, anything uncertain. Report dirs / GB / preserved.

## Exit flows (user)
MP: menu -> Private Match -> Game Settings EVERY time -> team -> bot limits adapt -> map -> character -> start -> spawn
-> aim -> hits register -> kill -> XP clean -> move without escaping -> finish -> lobby -> back out -> Private Match ->
Game Settings still work. ESC: as above.
