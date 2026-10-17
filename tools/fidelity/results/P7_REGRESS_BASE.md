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
