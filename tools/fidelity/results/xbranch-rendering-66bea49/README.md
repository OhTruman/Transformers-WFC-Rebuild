# Cross-branch observation: origin/agents/rendering 66bea49 (read-only)

`ab.ps1 -Ref origin/agents/rendering -Measure` (isolated export under `work/ab`), then the full
Milestone 03 gate inside that export (`m03-gate.ps1 -SkipBuild -Quick`, plus the map audit and perf
steps re-run after two gate fixes). Render data was built by that tree's own `tools/render` and
Rendering's `audit_map.py` was consumed.

**Result:** 344 PASS / 0 FAIL / 71 KNOWN (`M03-GATE.md`).
- **Map:** WRONG MATERIAL props 124 → 0; movers WRONG EFFECT 12 → 3. "lights NOT INSTANTIATED 0 → 1"
  is the LightsVisibilitiesVolume newly reported by Rendering's audit, not a regression.
- **Performance:** held fire 51.5 → 38.1 ms/frame (light-visibility memo: visibility 12 → 0.3 ms).
  Collision is unchanged because the traversal fix is on agents/systems.
- **Visual:** 25 stills changed vs milestone-02 (original FX materials on hover/boost, a clean vehicle
  after the transform). Review `vehicle_visual.png` and `transform_r2v_side.png`.
