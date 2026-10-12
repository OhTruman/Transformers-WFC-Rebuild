# Lockstep exact identity: integration 0502051 vs 4ef2208 (M5 code-review fixes), 2026-10-12, untimed

Integration request. Both builds Release from `git archive` of the integration tree, the integration tree's options, with its
uncommitted third_party/ffx_sdk + d3d12_agility copied in (so the FSR / FG runtime ships beside the exe as theirs does): `build.sh`.

Flow (`ident.sh`): WINDOWED only (profile 1920x1080, Fullscreen=0; the display is degraded, so nothing changed modes), default
settings, frontend flow -> 64p TDM, PLAYERBOT, seed 7 / flowseed 1, WFC_LOCKSTEP + WFC_LOCKSTEP_EXACT, 3840x2160 render size in
the window, 25 captures (steps 60..1500 every 60) and the per-step SIMHASH. One exe at a time; free commit 13-17 GB throughout.

| map | SIMHASH (common steps) | captures identical |
|---|---|---|
| Gorge 510 | 3002 / 3002 equal | 25 / 25 (see A/A) |
| Rust 504 | 3002 / 3002 equal | 25 / 25 |
| Streets 508 | 3002 / 3002 equal | 25 / 25 |

Gorge, first pair: one capture (step 660) differed by 6 px, max 1 level, at (103,366)-(120,372). The A/A rerun (`aa.sh`) shows that
this is base run 1 differing from itself: base r2 = head r1 = head r2 on all 25 captures, and base r1 vs base r2 shows the same 6 px.
This is the known same-build capture nondeterminism on Gorge under EXACT (pawn-occlusion query results, m1-reverdict-5aa2d22 section 4),
not the merge.

Verdict: 4ef2208 is identical to 0502051 at default settings, windowed, on all 3 maps (25/25 + SIMHASH).
