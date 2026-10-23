# KISMETTEST - 9b6adbdd (the Kismet VM merge) (2026-10-23 06:11)

- Clean build from git (Kismet.cpp / .h, Application_KismetTest.cpp tracked): OK.
- WFC_KISMETTEST=1 headless: **KISMETTEST SUMMARY: 9/9 checks passed**, exit 0 (work/kismet_9b6/run.log).
- Versus identity (Kismet not wired into the sim): Streets TDM 4 v 5 Smart, seed 7, 60 s headless with WFC_SIMHASH - the 4,210 SIMHASH lines are byte-identical between bfee6521 and 9b6adbdd.

KISMETTEST joins the per-merge headless set (with VOICETEST, the five profiles, late join and the leave arms).
