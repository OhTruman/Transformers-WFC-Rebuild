# Package 7 regress base (pre-check, 2026-10-17 09:11)

- Base: work/ab/fast_af4d073_hover = af4d0731 + tools/fidelity/gatebase/hover_cfca1717_srcgame.patch (cfca1717's src/game:
  BotSmart.h, CharacterMovement.{h,cpp}, VehicleTests.cpp, WorldBots.cpp; its Application.cpp hunk = NAVQ TIP rows, left out).
  Built with BASE_REF=af4d0731 gate-base.sh; patch file (not byte-checked: cfca1717's parent carries 1e11edc2's presentation-only
  noteHitDir line in WorldBots.cpp) - proven by the same-match row.
- Reference head: work/ab/fast_cfca171 (cfca1717 built in full).
- Verify (Streets, lockstep steps 600-9600, RT off, 1280x720, seed 1234, PlayerBot): base vs cfca1717 **same match True**
  (SIMHASH identical on all 10,200 steps).
- Negative control: af4d0731 vs cfca1717 **same match False**; SIMHASH equal through step 602, first difference at **step 603**.
- Use: GATE_BASE=work/ab/fast_af4d073_hover GATE_BASE_NAME="af4d0731 + hover cfca1717 (src/game)"; later sim changes
  (Gameplay's per-weapon recoil + melee batch next) join as further items before the package 7 gate.

## Base with the headless runner (2026-10-17 12:50)

- work/ab/fast_af4d073_hover_hl2 = af4d0731 + hover_cfca1717_srcgame.patch + gatebase/headless_runner_be54be6d_af4d.patch
  (be54be6d's runner minus its one online-hold line `frontend_->flow().simHeld()`, which af4d0731's GameFlow lacks and which is
  never true offline).
- Sim-neutral and headless == windowed, proven without the card: two headless runs (WFC_NULLRENDER=1, Streets lockstep 600-9600,
  seed 1234, PlayerBot) give SIMHASH identical to each other AND to this morning's windowed run of the hover base without the
  runner (work/p7base_verify/base-r1), on all 10,200 steps.
- gate-base.sh HEADLESS=1: the VERIFY same-match row runs both exes under WFC_NULLRENDER (CPU only). The head must carry be54be6d
  (any integration head from be54be6d on). Exit code 1 from the runner is expected today (Systems' init-return path).
- Use: GATE_BASE=work/ab/fast_af4d073_hover_hl2.

## Base + 814e7cf5's sim-only diff (2026-10-17 14:5x) - NOT yet the same match

work/ab/fast_af4d073_p7_t = the hl2 base + gatebase/TRANSACT_sim_only_af4d.patch (Gameplay's diff). Headless, Streets lockstep
600-9600, seed 1234, PlayerBot, 31 v 32 bots:
- p7_t vs 814e7cf5: same match False, first SIMHASH difference at step 5215.
- negative control hl2 (no TRANSACT) vs 814e7cf5: differs from step 895.
- hl2 vs 02b4bbfa (integration just before 814e7cf5, with the runner): same match True on all steps - the pre-814e7cf5 sim is
  fully in the base.
Gameplay's own check (4 v 5 bots) did not reach the step-5215 difference; the diff is back with them. Raw: work/p7t_verify/.
