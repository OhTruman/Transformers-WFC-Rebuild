# Rendering: Molten plan (Realistic reads as a different game on a lava-lit map)

Batch-4 score on Molten (Realistic vs Off, one seeded frame): contrast 1.00, brightness -3.1 - the RT relighting matches
the bake's contrast because Molten's light is the lava, not the sun or the lamps that the v3 rows sharpened.

## a) Emissive surfaces as RT light sources (built, tuning pending)
- R4's bounce rays already add a hit surface's emissive (HitSub row 6, the material's emissive estimate) - lava and
  glowing conduits do light the RT lightmap and the probes, but at the estimate's strength (lava's animated / textured
  emissive averages low).
- New: GiExtra.z = an emissive gain on what bounce rays (lightmap texels and the character probes) pick up from emissive
  surfaces: WFC_RT_GIEMISSIVE (default 1 until measured; batch 5 runs 1 / 3 / 6 on Molten). Realistic only (the GI runs
  only there). Effect: red-orange light pooling on floors and walls near lava, robots taking the glow through the probes.
- Next if it reads well: the lava's emissive also as direct light for specular (X2's lightmap specular already reflects
  whatever the RT lightmap holds; nothing extra needed).

## b) Per-map exposure targets (built)
- X1's adaptation key per map: Molten 0.03 (its halls sit at the same log-average as Streets' exterior, 0.02): ~1.3x lift
  at adaptation strength 0.65; WFC_TONE_KEY overrides every map. More maps join the table from the sheet's numbers.

## c) Molten's lamps in the shadowed set
- Today the RT pass shadows the 4 point / spot lights the characters used most this frame (item 2). On Molten, check the
  shadowed set with the item-2 diagnostics (lights used / world-shadowed per frame); if the hall lamps miss the set, rank
  by screen coverage x brightness rather than character use, and raise the set to 8 (LightVis has 4 channels x 2 bits:
  needs a second target - ~0.3 ms).

## d) Contrast lever specific to lava light
- The lava surface itself: X1's soft bloom on its emissive (already on) + a heat-haze distortion already authored in the
  original's FX (check it renders in Realistic).

## Order
a (measure gains, pick, default) -> b (per-map key from the sheet table) -> c (diagnostics first) -> d (check only).
Yardstick: Experimental's per-map score table, Molten contrast clearly above 1.
