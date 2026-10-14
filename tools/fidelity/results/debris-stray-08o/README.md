# Debris "stray untextured turret / head-like objects" (user, TDM on 08o)

**Tested:** integration/milestone-08o 03fde67 (Release, direct boot TDM). `prop-capture.ps1 -Mesh 'DeadSoldier|DeadCarSoldier'`
puts a fixed camera at 9 of the 16 placements.

**Finding:** pale grey, untextured chunks inside the wrecked-soldier piles, in these two shots:

| shot | mesh | component | UE position |
|---|---|---|---|
| p01 | DeadCarSoldier01_STAT | see props.csv | (6676, 3411, 13317) |
| p04 | DeadCarSoldier01_STAT | see props.csv | (8756, 1043, 12997) |

- Material: `PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST`.
- No "failed to build; using glTF fallback" line is logged for it. Rendering's diagnosis: a section on a material that
  failed to compile (RandomSeed), so it draws flat grey.
- Rendering's fix is on agents/rendering; it is verifiable only on an integration build. Rendering's lane tree has no
  `WFC_MAP` direct boot (Streets only), so prop-capture cannot load Debris there.
- The other placements (p00, p02, p03, p05-p08) show dark, textured wreckage.
