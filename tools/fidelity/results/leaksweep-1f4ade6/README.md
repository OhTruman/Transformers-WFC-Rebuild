# Leak sweep rerun - 09c 1f4ade6 (2026-10-09; Systems' trims + music fix + per-heap MEM lines, Gameplay eviction, Rendering GL fixes)

Settled lobby samples (20 s after each return); GL sites symbolised (build's gltrace_sym.py + .map) and judged on a MONOTONE rise
of the site's live total over the later half of the dumps; GlCensus probe sites excluded (trace artefact).

| scenario | verdict |
|---|---|
| modes: TDM / CTF / DOM / KOTH x 6 on Streets (24 matches, 10 v 10) | **PASS** - settled private flat (later half), C++ heap 376.5 -> 380.7, GL textures 51 -> 54 (+3 over 24), buffers / FBOs / RBOs / VAOs flat, programs 707 flat. Process heap (CRT) ALLOCATED 292 -> 297 flat; only COMMITTED rises (323 -> 357) = fragmentation (Systems' criterion) |
| maps: 8 TDM maps x 2 passes, 32 v 32 | **no leak** - process-heap allocated 455 -> 865 MB over the FIRST pass (each new map), then flat on the second pass (863 -> 865); 4-16 MB private bucket 1481 -> 2169 then flat; GL programs plateau 1504 (Rendering's cache), textures plateau 63. Watch: C++ heap +3.3 MB/match on the second pass (398 -> 401 -> 405) |
| long: 20 min 32 v 32 | flat for 15 min; the final-stretch step is now +64 MB heap / +150 MB private (was +236 / +356 on 942cbe2 - Systems' picked-wave decode) |

Note: ~400 MB of process-heap (CRT / driver) memory accumulates per distinct map visited and is retained (bounded; consistent with
the driver holding the program cache's compiled shaders).

## Watch item closed - maps x 4 passes (32 matches, 1f4ade6)
C++ heap (operator new live) at settled lobby samples: 398.0 -> 400.2 -> 404.7 -> 404.0 -> 404.1 -> 402.3 -> 404.7 -> 405.3 ->
406.3 -> 407.2 MB - max 407 (< Systems' 410 MB bar), ~0.2 MB/match over passes 3-4 = flat. Process heap (CRT) committed /
allocated: 739/665 -> 745/669 -> 769/672 -> 769/673 -> ... -> 781/674 -> 782/674 - ALLOCATED flat from pass 2 on, only committed
creeps (fragmentation). Settled private later-half slope 7.4 MB/match tracks the committed creep. **No leak.**
