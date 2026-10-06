# M05 preparation artefacts (not a verdict)

- `lane_rendering_187e6e9/`: playtest-regressions reports + sheets on agents/rendering 187e6e9 (the "lane" leg of
  `m05/visual-compare.ps1`; pass it as `-LaneRendering` to `m05-final.ps1`). Against int-04 a03d7f7 it changes 33
  of 131 measured viewpoints (steam / fog-sheet coverage and hue, one glass view) and matches Rendering's own M06
  measurements exactly.
- `DRYRUN-frontend-08ef880-NOT-A-VERDICT.md` + `dryrun_frontend_08ef880_R1_sheet.png`: the gate run on the
  frontend branch alone, used to debug the gate. Its FAILs are expected there (no Gameplay match core, no Systems
  audio wiring in that branch) and say nothing about the integrated build. Notable observation for Integration:
  on that branch the in-match frame is black with a "LOADING..." spinner while the trace reports InGame.
