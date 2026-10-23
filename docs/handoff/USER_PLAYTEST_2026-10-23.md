# User playtest findings, 2026-10-23 (verbatim)

Recorded by Frontend for every lane. Build played: not stated by the user. Screenshots: none attached.
Routing (Integration, rebuild-84): Frontend #1, #2, #4 (UI side), #6, #7, #8, #17 (UI half); Gameplay #11, #12, #15, #17
(World half), #18; Rendering #3, #14, #16; Systems (effects) #5, #9; AssetTools + Gameplay #10, #13.

---

This is a new HUMAN PLAYTEST FINDINGS pass.

These observations come directly from me playing the current build. Treat them as real open issues even if automated tests currently pass.

Please coordinate the appropriate Gameplay, Rendering, Frontend, Systems, AssetTools, RE, Experimental, and Integration work as needed.

Do not blindly patch symptoms if several problems share one underlying cause. Find and fix the root issue where possible.

For anything where I specifically say I am unsure whether the behavior is authentic, compare against original WFC data/behavior before changing it.

==================================================
1. SCREEN-TO-SCREEN LOAD TIMES
==================================================

Transitions between screens are still taking longer than I would like.

This includes moving through menus/screens where there is noticeable waiting between one screen and the next.

Please profile these transitions and reduce the load time where possible.

Look for actual causes rather than simply hiding the delay with different animation timing.

I want navigating the frontend to feel much more immediate and responsive.

==================================================
2. RESPAWN TIMER UI GETS PERMANENTLY STUCK
==================================================

There is a serious UI-state bug with the respawn timer.

After dying in a match, the respawn timer UI can become permanently stuck on the screen.

It can remain visible even after I have respawned.

More importantly, it can survive leaving the match and actually follow me all the way back to the home/main menu.

That UI state clearly is not being destroyed/reset correctly.

Please fix the ownership/lifetime of the respawn overlay so it only exists during the correct death/respawn state and cannot leak into other gameplay or frontend screens.

==================================================
3. VEHICLE HEADLIGHT FLICKERING
==================================================

Vehicles that have headlights or forward-facing lights currently flicker while driving.

The light can flicker both:

- in front of the vehicle;
- underneath the vehicle.

This needs to look smooth and stable.

Sometimes the resulting shadows also look strange, although that may simply be a consequence of the light itself flickering.

Please investigate the actual source of the lighting instability.

==================================================
4. REALISTIC TEXTURE PACK MISSING FROM SETTINGS
==================================================

The texture setting currently only shows:

Faithful

I do not see the Realistic texture pack that we made.

The Realistic/high-end texture option needs to appear properly in the graphics/video settings alongside the Faithful option.

Please verify that:

- the option exists;
- the UI exposes it;
- changing it actually switches the intended texture set;
- it persists correctly;
- the appropriate assets are actually being used.

==================================================
5. DEATH EXPLOSION SMOKE LASTS FAR TOO LONG
==================================================

When an enemy robot dies and explodes, the remaining smoke trail/cloud stays around for much too long.

The smoke should remain for only a handful of seconds after the death and then fade away naturally.

Right now it looks like the effect persists far beyond the point where it should.

Please check:

- particle lifetime;
- emitter lifetime;
- fade behavior;
- cleanup/despawn;
- whether old emitters are remaining alive incorrectly.

Do not make the smoke abruptly disappear.

It should fade naturally.

==================================================
6. VIDEO SETTINGS MENU NEEDS SCROLLING
==================================================

The Video Settings menu currently places all of the settings vertically until they reach basically the very top of the screen.

This looks awkward.

Make the Video Settings screen a properly scrollable menu.

The settings should remain inside an appropriately sized menu/view area rather than simply continuing upward across the screen as more options are added.

==================================================
7. SCOREBOARD SPACING
==================================================

When initially spawning into a match, the scoreboard/player list is much too spread out vertically.

There is too much space between each player.

It should be considerably more concise and visually put together.

Please tighten the vertical player spacing while keeping names/ranks/stats readable.

This is especially important now that we support substantially larger matches.

==================================================
8. TEAMMATE HEALTH BARS THROUGH WALLS
==================================================

Teammate information currently appears through walls.

Having teammate NAMES visible through walls is probably fine.

However, I do not think their HEALTH BARS should remain visible through walls when the teammate is somewhere completely different or across the map.

The health bar should be limited much more appropriately.

For example, when I am actually in the same area/room as the teammate it can make sense, but I should not constantly see health bars belonging to teammates through the entire level.

Please determine the appropriate behavior and clean this up.

==================================================
9. SCIENTIST/JET BOOST TRAILS GET STUCK IN MID-AIR
==================================================

This is an older bug that still has not been fixed.

When playing Scientist and using the jet/vehicle boosters, the boost trails can become stuck in mid-air.

They remain floating in the world instead of following/despawning correctly.

Please fix the boost trail lifetime/attachment behavior.

Boost trails should:

- originate from the correct vehicle;
- move correctly with the vehicle while active;
- stop correctly;
- fade/despawn properly;
- never leave permanent trails hanging in the world.

==================================================
10. SEED OF CORRUPTION — INVISIBLE/OVERSIZED COLLISION
==================================================

There is a collision problem around the middle of Seed of Corruption.

Near the central hole/blue-hole-looking area where the black material/effect comes upward from the ground, there is a solid object/collision volume.

The collision extends vertically WAY higher than the visible object should.

You can be driving or flying and suddenly hit this invisible/oversized vertical collision and get stopped.

Please inspect the geometry and collision around that central feature.

The collision should correspond reasonably to the visible geometry/effect and should not create a giant invisible wall extending upward through the map.

==================================================
11. WEAPON BULLET SPREAD / AIMING
==================================================

Please investigate weapon spread carefully.

Some weapons have spread that feels far too wide.

Hip-fire can sometimes make sense, but when aiming/zooming in, some shots appear to go nowhere near where I am actually aiming.

That looks wrong enough that I suspect there may be a bug.

Please compare the following against original WFC behavior/data:

- hip-fire spread;
- fine-aim/ADS spread;
- crosshair location;
- actual projectile/trace direction;
- weapon muzzle direction;
- camera aim direction;
- recoil;
- bloom;
- spread recovery.

The visible crosshair and the actual shot destination need to correspond properly.

If original WFC intentionally has weapon-specific spread, preserve that.

But if the rebuild is applying the wrong spread, applying hip-fire spread while aiming, or using the wrong trace origin/direction, fix it.

==================================================
12. TIME TO KILL MAY BE TOO FAST
==================================================

I also want TTK investigated.

From my playtest, I seem to die extremely quickly.

Other players/bots sometimes seem harder to kill than I am.

I am NOT certain yet that the actual health/damage values are wrong.

This could potentially be related to the weapon spread/aiming problem or another combat issue.

Please compare:

- player health;
- shields if applicable;
- weapon damage;
- damage falloff;
- critical/headshot behavior;
- bot damage;
- player damage;
- difficulty modifiers;
- incoming accuracy;
- damage rate.

Use original data wherever available.

Do not arbitrarily increase player health because I said TTK feels fast.

First establish whether the underlying values/behavior match original WFC.

==================================================
13. SEED OF CORRUPTION GEOMETRY CLIPPING
==================================================

There is some geometry on Seed of Corruption that the player can partially clip into.

I noticed this particularly with some cylindrical objects.

Please inspect collision around those pieces and clean up obvious places where the player can enter geometry that should be solid.

==================================================
14. WATER VISUAL QUALITY
==================================================

Please look into improving the water presentation.

The water currently could look nicer.

Check what original WFC authored for:

- material;
- movement;
- reflections;
- refraction if applicable;
- normal movement;
- transparency;
- depth treatment;
- lighting interaction.

Use the original material behavior as the starting point.

If the current reconstruction is missing authored features, restore them.

==================================================
15. WEAPON SWITCHING ANIMATIONS
==================================================

Right now switching weapons appears to happen almost instantly.

The previous weapon disappears and the new weapon appears without much or any visible switching animation.

I am NOT sure whether original WFC actually behaved this way.

Please investigate the original game/assets.

Determine whether multiplayer has:

- weapon lowering animation;
- weapon raising animation;
- hand/arm transition;
- equip animation;
- unequip animation;
- timing events;
- sounds tied to switching.

If original switching animations exist, implement them correctly.

If the original genuinely switches nearly instantly, document that instead of inventing a modern weapon-switch animation.

==================================================
16. VEHICLE LIGHTS / SHADOWS
==================================================

Related to the earlier headlight issue:

Vehicle lighting can sometimes produce strange-looking shadows.

Please determine whether the shadow problem is caused by the flickering light or whether there is an additional ray-traced/dynamic-light shadow issue.

Fix the underlying cause instead of simply disabling the vehicle light.

==================================================
17. CANNOT SWITCH CLASSES DURING MATCH
==================================================

There is currently a bug where I sometimes cannot switch classes during a match.

I become effectively permanently locked to the character/class I originally selected.

Please investigate the entire in-match Change Character / Change Class flow.

Verify:

- opening class selection;
- selecting another class;
- death state;
- respawn state;
- queued class change;
- spawning as the new class;
- clearing the old pawn/state;
- movement/input after spawning;
- HUD;
- weapon inventory;
- vehicle form;
- abilities.

There have previously been bugs around changing classes during respawn, so please make sure this is fixed as a state-machine/lifecycle issue and not just one narrow case.

==================================================
18. BOTS SOMETIMES FLOAT
==================================================

Bots occasionally appear to float.

I have seen enemy/other characters simply hovering instead of properly standing/moving on the world geometry.

Please investigate what state causes this.

Potentially inspect:

- navigation;
- ground detection;
- animation/root motion;
- capsule/world collision;
- vehicle-transform state;
- falling/landing state;
- replication/presentation state;
- off-mesh recovery.

Do not just visually force them downward if the actual gameplay/navigation state is wrong.

==================================================
19. HUMAN PLAYTEST PRIORITY
==================================================

Please treat these observations as authoritative playtest findings.

Automated tests passing does not close them.

The highest-priority problems from this pass are approximately:

1. Respawn timer UI permanently leaking across screens.
2. Weapon spread / shots not corresponding to aim.
3. In-match class switching becoming locked/broken.
4. Scientist boost trails permanently remaining in the world.
5. Vehicle light flickering.
6. Bot floating/navigation presentation issues.
7. Seed of Corruption collision problems.
8. Slow transitions between screens.
9. Death smoke lifetime.
10. Missing Realistic texture-pack setting.
11. Video settings layout/scrolling.
12. Scoreboard spacing.
13. Teammate health bars showing through the level.
14. Weapon-switch animation investigation.
15. Water presentation.
16. TTK verification.

Do not wait for me between straightforward fixes.

Route the findings to the appropriate lanes, investigate them, fix what can be fixed confidently, and use RE/AssetTools when original behavior or authored data needs to be confirmed.

When this batch is ready for another human test, give me a concise checklist telling me exactly which of these issues I should retest.
