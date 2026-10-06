# M07 gate dry run (2026-10-05)

These are calibration runs of the M07 suites. They are not milestone verdicts.

- `m6b_*.md`: integration/milestone-06b (681fd29), re-judged after the overnight tooling fixes. Real 06b defects:
  - depth test off after every overlay;
  - OPTIMUS FALLBACK and IonBlaster-only for all 4 class presets;
  - CTF / EXT rules not implemented;
  - Streets + Berth frontend world lost.
- `gameplay_1216e80_chassis.csv`: agents/gameplay 22a, read-only lane build, `WFC_CHASSIS` per chassis. All 33 roster
  chassis spawn with their own robot and vehicle bodies, and the active chassis equals the request. A `Truck` (Optimus
  Prime) boot-default body loads first in every run; it is not the active body. Gameplay WFC_CHASSISTEST: 13/13.
  The OPTIMUS FALLBACK rule counts both "Optimus" and "Truck" (negative control: a Car2 run with Truck applied last
  trips the rule).
