# Final combined gate (Integration 2026-10-23)

The last broad pass, on the cut candidate after Gameplay's last batches and AssetTools' chain: the combined timed gate plus the
stability set. Runner: `tools/fidelity/final-gate.sh <head> pre|gate [--whatif]` (dry-run checked 2026-10-23).

## Inputs Integration / Gameplay provide before `pre`
- **The head sha** (the cut candidate).
- **FINAL_BASE_ITEMS**: the regress base = af4d0731 + every change of the simulation since then, as sim-only diffs against af4d0731
  (patch files, as tools/fidelity/gatebase/ holds today: hover cfca1717, the headless runner be54be6d, TRANSACT, WEAPON). Still to
  come from Gameplay when the head is named: spread f8a02fc8, dodge cap 0d8e3c06, bot skill e33a2c85, leave + Forfeit 65c7ccc8 and the
  leave-to-bot rule (ae9bbb6e), the Escalation sim (E1 .. E3b, horde strand exemption, NAVOFF) and the campaign sim (runtime 3-11,
  squad strand move-back). The base must play the head's match: `pre` checks Streets (TDM) AND Remnant (ESC) headless and stops if
  either differs - a missing diff shows there, before any timed minute.

## pre (CPU only, no hold, no card)
1. Build the head (build-target -j 4, link-render; requires WFC_OPUS=1 - third_party/opus).
2. Build the base (gate-base.sh, BASE_REF=af4d0731) and the negative control (af4d0731 + hover + runner only).
3. Same match, headless, lockstep 600-9600, seed 1234: base vs head must be True on 508 (TDM) and 506 (ESC); the negative control
   must differ.
4. The stability set on the head exe (work/udp/batch.ps1 -SkipBuild): 10 maps x 300 s TDM x 2 seeds, both Escalation maps with
   ESCLOG, D5 + A1 campaigns x 2 seeds, CAMPAIGNTEST / KISMETTEST / VOICETEST, five net profiles, late join (+ early pick), relay,
   leave.

## gate (timed; only after Integration's GO and every lane's ack - no interpreters / compiles anywhere during the hold)
- The render data frozen into one snapshot (matb_root.ps1 -NoCandidate) for every arm.
- PERF_HOLD set; waits for a clear card (busy(): interpreters, compilers, other wfc exes).
- 10 MP maps (501-510; 505 BrokenHope / 506 Remnant boot ?GameModeTag=ESC - lockstep-ab.ps1 since 2026-10-23) x {2560x1440,
  1920x1080} x {base, head}, lockstep 600-9600, seed 1234, PlayerBot, 2 reps. A map with a CONTAMINATED rep reruns whole once.
- The pool guard at every step (stops cleanly above 9.5 GB nonpaged); the hold removed at the end.
- Estimated hold: about 80-100 min (2 arms x 2 reps x 20 map-resolution rows, from package 6's per-map times), plus reruns.

Output: work/finalgate/<head>/ (finalgate.log with the per-row 'same match' + p50 / p90 / p99 lines, stability.log, regress-<res>/).

## DLC maps (Integration 2026-10-23, docs/DLC_PLAN.md)
- lockstep-ab.ps1 takes `-Map <id>` or a bare map name (`-Map MP_IAC_Horizon`); the ids known so far are 511 Havoc and 512 Fortress
  (add the rest from RE's DLC_RE.md); every MP_ESC_ map boots in Escalation.
- final-gate.sh: each DLC map (Havoc, Fortress, Tranquillity, District, Horizon, Metropolis, Sector, Pulse, Static) joins the timed rows
  and the headless same-match list as soon as Rebuild/work/render/<map> exists (FINAL_MAPS set = exactly those maps). The base can only
  play a DLC map once Gameplay's FINAL_BASE_ITEMS carry the DLC sim (unlocks, the District ESC enemy) and the map data exists for it -
  the same-match row says so before any timed minute.
- The stability set (tools/fidelity/broad-pass.ps1 -> tools/fidelity/stab-soak.sh) picks the DLC maps up the same way: versus in the
  10-map x 300 s soak, the DLC ESC maps in the Escalation rows.
- FINAL_ASSETS=<VerticalSlice root with the DLC maps> (today Rebuild/work/assets_dlc/VerticalSlice) is passed as WFC_ASSETS to every arm (same match and timed rows); Fortress (512) already joins the map list automatically.
