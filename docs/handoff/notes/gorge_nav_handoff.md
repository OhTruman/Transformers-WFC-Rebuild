# MP_UND_Gorge off-mesh traps: nav handoff for AssetTools

Read-only analysis by Gameplay (2026-10-11). Sources:
- Experimental's m1v-ad4e6c5-510-* wfc.log ("no way found" positions).
- ExtractedAssets MP_UND_Gorge collision.glb, probed with work/pass29/colprobe.js.
- bot_nav.json (installed dry6), probed with work/pass29/trapcells.js.

All coordinates are glTF metres (x, y up, z).

Map facts:
- KillZ is effectively none (-262143 UU).
- The chasm bottom (y -124 .. -164) is inside PhysicsVolume_6787: bPainCausing, DamagePerSec 1e7, y -137.9 .. -87.4 over the whole map. Falling into the chasm kills, and the rebuild implements that as a HazardVolume.
- Nav limits: jump_up_max 5.0 m, double_jump_up_max 9.5 m, max_drop 34 m.

Why bots get stuck: the stranded rejoin search only accepts a nav cell at most 3 m above the bot (findCell(q, 3, 60)). It never jumps up. The floors below are unmeshed, probably culled as islands with no connection to the main piece. So a bot that lands on them finds no way back, and the 30 s stranded suicide takes it.

## T1: girder strips at y -12 (most frequent: 15 of the 41 no-way logs fall in one 4 m bin)
Floor, highest at or below y -10:
- Two strips at y -12: x -170 .. -161, z 17 .. 20 and z 26 .. 29.
- A lower floor at y -15: x -176 .. -164, z 4 .. 32.
- At x -160 .. -156: a y -18 floor for z 17 .. 29, and a y -10 floor for z 4 .. 15.
- Gaps at x -163 .. -161 (z 21 .. 25 and z 14 .. 16) drop into the chasm (y -124 / -164).

Nearest nav: cells 4007 and 4147 at y -7.8 (x -164 .. -163, z 13 .. 16; clearance 2.5 / 1.5 m), 1 .. 4 m from the north edge of the z 17 strip.
- Rise from the strip: 4.2 m, within jump_up (5.0).

Ask:
1. Mesh the y -12 strips and the y -15 floor.
2. Add jump_up links: from the y -12 strip at about (-163.5, -12, 17.2) to cell 4147 (-163.5, -7.8, 15.5).
3. Add a jump_up link from the y -15 floor to the y -12 strips (rise 3 m).
4. If the y -10 floor at x -160 .. -156, z 4 .. 15 is unmeshed too, link it to 4007 (rise 2.2).

## T3: sunken bowl at y -6 .. -16, x -82 .. -42, z 118 .. 134
- The surface slopes up to the east: y about -7 at x -42, and -12 .. -16 in the middle around x -72 .. -66.
- Experimental's bot dropped in here from the edge of cell 6154 (x -41 .. -40, z 110 .. 121, y -2.0).

Nearest nav:
- Cell 6154 (y -2.0) above the east lip: rise about 4.5 .. 5 m from the lip at x -42, y -6.5 .. -7. Borderline for jump_up, safe for double_jump_up.
- Cells 6376 / 6377 (y -2.1, x -54 .. -51, z 126 .. 131): rise about 9 m from the bowl surface at x -54 (y -11). Double jump only, not recommended.

Ask:
1. Mesh the bowl.
2. Add a jump_up (or double_jump_up per your rise test) link from the east lip around (-42.5, -6.8, 118 .. 121) to cell 6154.

## T2: rock outcrop in the chasm at y -20 .. -26, x -20 .. -8, z -50 .. -36
- The nearest nav is 18.3 m above (cells 2646 / 2668 / 2690, about 18 .. 20 m away), beyond any jump.
- Every edge drops into the pain volume.
- No nav fix is possible. The 30 s stranded suicide is the recovery, the same result as walking off the edge. No action needed.

## T4: a Classic jet wedged on cell 4486 (-199.6, 32.0, 27.3)
- Fixed in code (Gameplay, pending gate): after 3 deferred landings within 8 s without moving, the jet lands if the robot spot is within 3 m of the mesh, or else climbs out.
- No nav change needed. Optional: check why the robot spot next to cell 4486 misses the mesh at zero vertical tolerance (the ledge edge).

## Validation after the install
Rerun Experimental's Gorge t64 verdict runs, or my FFA 64 STUCKWATCH runs on Gorge. Expect:
- "no way found" at T1 / T3 → 0.
- T2 unchanged.
- New nav data changes bot paths for both AIs on Gorge, so it is a re-baseline like the dry6 install: run the 5-config gate against the new nav, with the old nav named by WFC_BOTNAV as the control.

## Outcome (AssetTools dry run, 2026-10-11)
- T1: fixed by a Gorge-only "exit rescue" rule (10 links for T1, plus 45 other one-way pockets).
- T3: no exit link. In collision_pawn.glb the bowl floor is at -8.93 (-7.39 in collision.glb), so the real rise is at least 9.2 m and my 4.5-5 m estimate was wrong. It stays on the stranded suicide.
- y -10 floors: these are the inside of the deck structure, reached through false drop links. A prune rule is parked; strand30 covers them meanwhile.
- Use collision_pawn.glb for height estimates.
