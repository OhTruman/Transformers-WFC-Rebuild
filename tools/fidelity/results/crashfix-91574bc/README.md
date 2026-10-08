# End-of-match AVM1 crash (64 p results screen) - fix verification on playtest candidate 09c 91574bc (2026-10-08)

Crash: 0xc0000005 read 0xffffffffffffffff in gfx::avm1::ScopeVars::set <- nativeUpdateInterpObjects (EndGameStats + its
PlayerList.swf define the tween library twice in one VM; the second binding overwrote a per-VM key; the first script was
collected while its native kept using it - Frontend 038d6db roots each native's own script).

| check | build | result |
|---|---|---|
| 20 s matches, 64 p real play, 10 processes | 0afd806 (unfixed) | 0 crashes / 20 end screens (too short to trigger) |
| 60 s matches, 64 p real play, 5 processes | 0afd806 (unfixed) | **1 crash / 9 end screens** (same signature: `baseline_0afd806_crash.txt`) |
| 60 s matches, 64 p real play, 5 processes | 91574bc (candidate) | **0 crashes / 10 end screens** |
| WFC_GFX_FORCEGC=1 + GCCHECK=1, 64 p real play, 2 processes | 91574bc | **0 guard hits**, 0 crashes, 4 end screens |
| positive control, same switches, 64 p lobby + match | Frontend unfixed11 (fix reverted) | **20 guard hits** (switch is active) |
| negative control, same switches | Frontend fixed11 | 0 guard hits |

Verdict: fixed. The deterministic check (forced GC after every loadClip) separates unfixed (20 use-after-collect hits) from
fixed (0) and the candidate shows 0; the 60 s loop adds 10 clean end screens against a 1-in-9 baseline. PLAYERBOT on 91574bc:
p0 kills 0 / 2 / 0 / 0 / 1 over the five loop processes (Gameplay's fire fix is effective but the pilot rarely kills).
