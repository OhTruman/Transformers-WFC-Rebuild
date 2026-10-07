# Map visual sweep on 09c aa5a74c (after the 09c full regenerations; includes 84064ef)

All 10 launchable maps by direct boot, 3 frames each (sheet_maps.jpg):
- 0 material fallbacks, 0 particle fallbacks, 0 GL errors, 0 out-of-bounds draws, clean exits on every map.
- Same classes as the previous sweep (mapsweep-fdffa7f) - no regression:
  - Seed / Berth: PLAYABLE - DARK (human check): the known authored-dark CLUT;
  - BrokenHope: BLOCKED BY MISSING SOURCE DATA (known; white untextured floor);
  - Rust / Remnant / Debris / Streets / Molten / Gorge: PLAYABLE;
  - Complex: classed "PLAYABLE WITH VISUAL DEFECTS" only because frames 2-3 face a darker area (frame detail 0.24 / 0.20
    vs 0.34). The frames are textured and intact, so this is a harness classification artefact, not a product defect.
