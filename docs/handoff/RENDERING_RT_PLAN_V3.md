# Rendering RT plan v3: "all lighting interacts across the whole map" (gap analysis)

Written 2026-10-16 ~03:40 from the user's direction (03:20, relayed by Integration):

> "push for more interactive shadows ... most notably the buildings surrounding the players / bots: on the open-skybox map,
> when players walk under buildings they're not catching a shadow from the building they're under ... the light from the
> vehicle when you're driving isn't casting on anything, nor when you shoot is it casting on the ground ... I want all
> lighting to interact with one another across the entire map to make it feel like you're in one place ... more things cast
> shadows and more lights cast light and reflections ... colour cast upon one another depending on where people are."

Scope: Realistic only. Off and Faithful stay the original byte for byte; package-5 fidelity fixes (muzzle / death lights)
apply in every mode by design. Quality first (the user: FPS later); budgets are measured and reported, not gates.

States come from the code at rt2 5fa83761 (+ package 5 = integration 398b27ef). Rows marked VERIFY are claims of the code
that a run must confirm before the row is called done.

| # | Interaction | Realistic today | What the user may have seen | Fix | Impact | Effort |
|---|---|---|---|---|---|---|
| 1 | Sun / sky shadows from buildings ONTO robots and vehicles | Robot pixels trace the soft sun per pixel against the world + characters (rt_shadow_ao, ToSun.w 2), BUT the result only dims the env's directional light that points along the RT sun (WfcPipeline wfcRtLV / rtVis). When the robot's light environment folds the sun into its ambient cube (more than 3 lights, or no direct sun picked), nothing darkens. The sky / ambient comes from the RT probes (item 1, package 5): they do darken under cover, but only as ambient. VERIFY on the open-sky map (Streets / Debris) | Off / Faithful arms: the original's per-object light visibility (one value per robot, no per-pixel cover). Package 4: no probes | (a) Always give characters the RT sun as a direct light in Realistic: the map's sun (the bake's sun direction + colour, the R1 fit) added as the env's slot 0 when the env lacks it, with the per-pixel RT visibility. (b) The ambient: the probe term already darkens under cover; add per-pixel RT sky occlusion (the AO rays already traced on dynamic pixels, radius 1 m today, raised for characters to ~4 m) on the ambient | Highest: the user's first complaint | 0.5 day |
| 2 | Robots / vehicles casting SUN shadows onto the world | Done: world pixels trace the sun; where only a character blocks it, the composite darkens to the map's ModShadowColor (Faithful and Realistic) | Off: the original's projected character shadows | Quality: the composite's darkening uses the original's ModShadowColor; in Realistic use the RT lightmap's sun share instead (the 2B method: the share of the decoded irradiance the sun brought), so the shadow's depth matches the light it removes | Medium | 0.5 day |
| 3 | Vehicle headlights lighting the road and walls, with RT shadows | None: the game spawns no vehicle lights (Systems' transient-light audit found muzzle and death / cloak lights only; headlights not checked) | Every mode | Systems: read whether the vehicles author a headlight (a SpotLightComponent on the vehicle mesh, TnVehicle light props) and spawn it each frame through addTransientLight with a cone (the API needs spot cone parameters: direction + inner / outer). Rendering: spot support in the transient path (cone term, as the static spot lights) and its RT shadow (it joins the 4 traced lights). If the original had none, Realistic-only headlights are a PC extension the user asked for: authored values from the vehicle's lens-flare / headlight material colour, ~20 m range | High: the user names it | Rendering 0.5 day + Systems |
| 4 | Muzzle light on the ground near the shooter | Package 5: the original's muzzle lights (86 weapons; MP guns 30 m, brightness 10, exponent 75), Lambert per pixel on the world and characters in every mode, RT-shadowed in Realistic (the 4 strongest). With exponent 75 the light falls to 1 % at ~9 m, so the visible pool is a few metres around the gun | Package 4 had none; Off / Faithful arms of package 5 have them unshadowed | VERIFY the pool on the ground at authored values in a firefight capture (Realistic and Faithful). If it reads weak next to the RT-lit world: the Realistic calibration factor (the static lights' giLightCal scale) applied to transient lights too, so they sit on the same energy basis as the RT lightmap | Medium | 0.25 day |
| 5 | Coloured bounce from nearby surfaces onto robots | The RT probes (1 m grid, 256 rays, traced from the calibrated RT lightmap) replace the original's ambient cube on characters: coloured by the surfaces around them. Strength = the probe's irradiance; the world's bounce went through the calibrated bounce gain, the probes trace the same pages, so the bases match. VERIFY with a robot next to a strongly coloured wall (Molten lava, Rust red containers) | Off / Faithful / package 4: the original's Beast ambient | If weak: the probes sample only usable cells (55 %); missing cells fall back to the bake's ambient (a mix). Raise usable coverage (cells inside walls nearest the character: take the nearest open neighbour) and add the 2-hop (probe -> character local bounce from the floor directly under the character: one extra ray set per character, the floor's RT lightmap colour x albedo) | Medium-high ("colour cast upon one another") | 0.5 day |
| 6 | Dynamic lights and characters in reflections | Characters: package 5 (54d9d039) takes the previous frame's image at the hit. Muzzle / death lights: NOT in reflection hit shading (hits read the RT lightmap + emissive only) | All | rt_reflect: add the frame's transient lights (the ring already carries the 4 traced ones) to the hit shading (Lambert at the hit with its hit normal, + the light's own glow where a ray passes near it) | Medium | 0.25 day |
| 7 | Emissive surfaces lighting their surroundings (energon, screens, glows) | Static emissive world surfaces: in R4 the hit radiance includes each sub's emissive (the RT lightmap is lit by them); characters get it through the probes. Dynamic emissive (pickups, energon particles, ability glows) light nothing | Off / Faithful: the bake's emissive contribution only | (a) VERIFY a static screen / energon panel lights its floor in Realistic (R4 includes it). (b) Dynamic: pickups / energon items spawn a transient light each (colour from their emissive, small radius) - Systems' spawn, Rendering's API | Medium | 0.5 day + Systems |
| 8 | One energy basis for world and characters ("nothing pasted on") | World: the RT lightmap (calibrated to the bake, 0.93-1.07). Characters: the probes (same pages, same gain) for ambient, but their DIRECT lights are still the original's light list at the original intensities, while the world's direct lighting from those same lamps went through the RT calibration (giLightCal). VERIFY: robot brightness next to a lamp vs the wall it stands at | All | Characters' direct lights in Realistic take the same calibrated colour as the RT lightmap's lamps (giLightCal_, by light index); and the sun row 1 (a) uses the R1-fitted sun | High (coherence) | 0.25 day |

## Order (visible impact, quality first)

1. Row 1: robots darken under buildings (sun direct light always present + per-pixel RT sun; a wider character sky occlusion).
2. Row 8: one energy basis (characters' lamps calibrated as the world's).
3. Row 3: headlights (needs Systems' read of the vehicle data first; spot support in the transient path in parallel).
4. Row 5: coloured bounce strength on characters (verify, then coverage + local floor bounce).
5. Row 6: transient lights in reflections.
6. Row 4: muzzle pool calibration in Realistic (verify first).
7. Row 7: dynamic emissive lights (Systems' spawn).
8. Row 2: Realistic's character sun shadow depth from the RT sun share.

The package-6 transient-light cost (RT off, +0.03-0.23 ms p90 after Systems' view-cone cull) stays queued beside these
(RT_FULL_PLAN.md: measure the specular / per-light shares first; cheaper per-light maths coded on rt2's worktree).

## Verification per row

A firefight capture set on the open-sky map (Streets) and Molten, Realistic vs Faithful vs Off, lockstep frames, with
before / after crops per row: a robot walking under a building (row 1), next to a lamp (row 8), next to a coloured wall
(row 5), a driving vehicle at night-like spots (row 3), a firing robot over a glossy floor (rows 4 + 6). Off and Faithful
byte-identical per row (lockstep shots + SIMHASH).
