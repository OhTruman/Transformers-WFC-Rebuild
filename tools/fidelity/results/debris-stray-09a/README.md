# Debris wrecked-soldier piles: re-shoot on 09a (verifies Rendering 4466eef, RandomSeed)

**Tested:** integration/milestone-09a cd0f826 (Release, own build in work/ab, render data regenerated). Same command as
on 08o: `prop-capture.ps1 -Map MP_ORB_Debris -Mesh 'DeadSoldier|DeadCarSoldier' -Max 9`, the same 9 placements.

**Result:**
- **FIXED**, compared visually with ../debris-stray-08o.
- p01 (6676, 3411, 13317): the pale grey untextured chunks are now dark, textured wreckage like the rest of the pile.
- p04 (8756, 1043, 12997): the grey piece behind the ledge is now textured.
- The other seven placements are unchanged (dark textured wreckage).
- No material fell back in this run, including the props' material `PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST`.

Scope: these are fixed cameras at 9 of the 16 placements in TDM. The other 7 placements were not looked at.
