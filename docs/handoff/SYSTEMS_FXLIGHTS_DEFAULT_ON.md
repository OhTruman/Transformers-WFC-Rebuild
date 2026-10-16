# SYSTEMS_FXLIGHTS_DEFAULT_ON (against integration 56a9beca)

Package 6 default flip (Integration 2026-10-16): the RT-era effect lights (SYSTEMS_RT_LIGHT_HOOKS; explosions, impact squibs,
boost, pickups - NOT original lights) are on by default in Realistic. WFC_FXLIGHTS=0 turns them off; "=1 forces them in other
modes" was not added (not cheap). Off / Faithful never add or submit one.

- World.cpp: fxLightsOn() = !(WFC_FXLIGHTS == "0") (was: only "1").
- Off / Faithful live-list fix: fxLight() returns unless the last drawn frame was Realistic (mutable fxRtMode_, set in the
  const draw). Before, an rtOnly explosion light entered transientLights_ in every mode and was only skipped at submit; its
  swap-with-last expiry could reorder the original muzzle / death lights in Off / Faithful. Now nothing enters the list there.
- Headlights (no change): already default-on in Realistic (kHeadlightsOn = !(WFC_HEADLIGHTS == "0"), gated on RT mode 2).
- Log at the first Realistic frame: "RT-era lights (first Realistic frame): effect lights on|off, headlights on|off".
- verify_slim.ps1: new RT-gate row rtcheck.rtlights - the gate's default Realistic run must log both on (an exe before this
  patch fails it by design, like the HD runtime row).

Checks (patched vs 56a9beca base, Release, same tree):
- SIMHASH: 18,000 steps identical; BOTTEST 45 / 46 on BOTH (the one FAIL "bots toss grenades (2)" is in the base too).
- Off identity (RT off, lockstep TDM 4 v 5 + playerbot, seed 7, 16 captures per run, tools/systems/off_identity.ps1): Gorge
  16 / 16 identical; Streets over 8 runs (2 base vs new, base vs base, new vs new): every run of the patched exe gives the
  common hash on every frame; two base runs each had one frame 1 pixel off by 1 (m01200 in one, m02400 in the other) = the
  base exe's own run-to-run noise. No FX LIGHT line in any Off run.
- Realistic crop: pending an RT slot.

Realistic (RT slot, Streets lockstep 30 s 4 v 5, no WFC_FXLIGHTS / WFC_HEADLIGHTS): patched logs "RT-era lights (first Realistic
frame): effect lights on, headlights on" and 37 FX LIGHT spawns; base neither; 0 [error] both. Of 12 captures, 5 change
visibly where effect lights burn (steps 150 / 600 / 1050 / 1200 / 1350: 0.5-20 % of pixels > 2 levels, step 1050 12.6 % > 16),
the other 7 differ by at most 1 level. Crop: fxlights_default/streets_realistic_ab.png.
