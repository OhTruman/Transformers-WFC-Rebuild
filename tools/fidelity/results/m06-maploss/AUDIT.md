# Audit: why the M05 / M06 visual checks passed a build with the Streets / Berth environment missing

Product under test: the user's executable, `Rebuild\build\release\bin\wfc_rebuild.exe`.
- Built 2026-10-04 20:18 from the Integration tree (milestone-06b work, which includes the Rendering 398b732
  render-root search).
- SHA-256 `f4c08f9301ef7265e93e…`, identical to the copy tested here.

Every row below was checked in the tool sources (`tools/fidelity`) or in run logs. "State-verified" means the check
read counters or events, not pixels.

| question | finding | affected checks |
|---|---|---|
| manually provide render paths? | **Yes.** `WFC_RENDER_DATA` is set by 22 of the Experimental scripts. The product's own default lookup was never exercised | m05-e2e-gate, playtest-acceptance, map-matrix, m06-maps-tdm, streets-sweep, playtest-regressions, motion-jitter, stress tools, lib/Shots |
| run from a different working directory? | **Yes.** Every product run uses `-WorkingDirectory <fresh run dir>`, so every run starts with a fresh profile; the user's saved profile / display state was never loaded | all (lib/Run.ps1, lib/Flow.ps1) |
| branch-local generated render data? | **Yes.** `m05/build-target.ps1` regenerates render data inside each export (`work\ab\<n>\work\render`). Integration's tree uses a different, older set (Streets 947 files / 60 MB vs 150 / 14 MB generated fresh) | all export-based runs |
| compare a broken runtime with another broken runtime? | **Yes.** `visual-compare` (merged vs int-04 vs Rendering lane, "131 / 131 unchanged") and `playtest-regressions` compare direct-boot frames between builds that share the same frontend-route defect, so "unchanged" proved nothing about the route | visual-compare, playtest-regressions, M06 report |
| fixed views that fail to detect a missing environment? | **Yes.** The fixed / free cameras are direct boot. The frontend route is where the environment disappears, and direct boot draws it | map-matrix, streets-sweep, playtest-regressions, presentation-gate `reference` part |
| object state instead of draw output? | **Yes.** match.loaded, MATCH, audio.loaded, mapstate counts, movers, hidden actors and spawn checks were all reported as "map works" | m05-e2e-gate, m06-maps-tdm |
| draw counts that omit the expected environment? | **Yes (product side).** `WFC_VISUALCHECK` (Rendering M10) PASSes every sample on counts and simple image statistics. The user-path log reports Streets `draws=1928–2177 world=1566–1750 bsp=330–379 materials=156–163 -> PASS` on frames whose environment is missing, and lobbies at `world=5 -> PASS`. The counts are issued draws, not visible environment | Integration / Rendering self-check |
| skip resolution / fullscreen / profile state? | **Yes.** Never set before this pass: always the 1280×720 windowed default of a fresh profile | all |
| different executable layout from the user's Release build? | **Yes.** Exports put Release in `work\ab\<n>\build-release\bin`, so `<exe>\..\..\work\render` = the export's render data. The user's layout is `build\release\bin` (three levels below the tree). M06 95edd7b's fixed lookup `<exe>\..\..\work\render` therefore resolved to the non-existent `Rebuild\build\work\render` for the user. The M06b exe searches upward and finds `Rebuild\work\render` | all export-based runs |
| visual verdicts only from Release while the human runs Debug (M06 95edd7b)? | Yes, for the earlier pass. This pass runs the exact Release exe the user ran | M05 / M06 finals |

## Conclusion
The environment loss is **independent of the render-data root, the profile, the exe layout and the Release / Debug
split**:
- it reproduces in the user's exe, in the user's layout, with the data root found and the healthy counts;
- it is caused by the frontend-launched match route (bisected to Frontend b1fce97).

Every earlier visual check either never took that route (direct boot), or took it while judging only state, counts
and "not black". So everything that looked at pixels looked in the wrong place, and everything on the right path
looked at state.


## Correction (2026-10-05, M08b FAST gate)
The figure "Streets world draws in play 35 / BSP 20 vs direct 1641" compared the **median of a scripted walk** with a
direct boot **standing at the spawn**. Re-judging the same M06b user-exe evidence shows the following.
- **At the spawn** the broken build drew 1276 world draws (ratio 0.78).
- A healthy M08b build gives the same walking median (43): the auto-walk ends against a pillar that occludes the scene.
- So the draws were issued but not visible, which fits the confirmed root cause: the UI pass left GL_DEPTH_TEST /
  GL_CULL_FACE disabled (b1fce97, fixed in a96f841).
- The **pixel** world verdict, which still flags all three M06b visits as lost, and the GL entry state are the
  discriminating checks.
- `draws_vs_direct` is now INFO (map-loss-gate.ps1) and compares the spawn samples.
