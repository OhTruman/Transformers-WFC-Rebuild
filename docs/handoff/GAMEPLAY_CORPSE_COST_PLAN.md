# Death-body presentation: cost plan that keeps the look (package 6 candidate)

Gameplay, 2026-10-16. A plan; code follows only if the bisect shows the bodies' share is large.

**Trigger:** the package 6 gate regress table (RT off, best p90 of 2) shows every map +1.66 to +3.76 ms vs the base, tracking the dynamic / FX draws:

| Map | Dynamic draws | FX draws |
|---|---|---|
| Gorge | 212 → 356 | 127 → 245 |
| Debris | 144 → 260 | 102 → 193 |
| Berth | 122 → 176 | 69 → 122 |

The bisect after the block (`WFC_NOCORPSE=1`, `WFC_NOMINEPRES=1`, from `GAMEPLAY_PRES_SWITCHES.patch`) measures the bodies' and mines' share.

## What one death presents today (from `World.cpp`, all authored)

**Robot:**
- The body: 1 skinned draw for TotalTime (0.3-4 s by entry).
- TnDeathModifierPlayEffect: **2 × Exp_Death_Sm_FX** (head socket + body).
- At the gib:
  - **Exp_Death_FX** (TnPawn.Dying.Gib → PlayGib);
  - **4 RB_Gib chunks**, each a static draw for 5 s;
  - **4 × TF_Death_GibsTrail_FX**. HmGibsDestructionEffect: NumChunks 4, LifeSpan 5, a trail on every chunk (authored, so the per-chunk trails are faithful).
- Total: **≈ 5 draws and 7 FX systems** alive for about 5 s.

**Vehicle:**
- 1 × TF_Death_Buildup / MED_FX, then TF_Death_MED_FX (Exp_Death_Lrg_FX for jets);
- the same 4 chunks + 4 trails;
- **the hull wreck** (TnSkeletalGibSpawner: LifeSpan U(3, 5) s, jets 7, a Defrag dissolve) and its smoke.
- Total: **≈ 6 draws and 6-7 FX** for 3-7 s.

**At scale:** with 63 bots (ExtendedPlayers, our extension; the original is ≤ 10 players), deaths overlap. About 8-15 recent deaths alive at once gives +50-100 draws and +50-100 FX, which matches the regress deltas.

## Levers, in order (each keeps the authored look up close)

1. **Particle LOD by distance, as authored.**
   - UE3 ParticleSystems carry `LODDistances` / `LODSettings` per emitter. The engine drops to cheaper LODs (fewer particles, emitters off) with camera distance.
   - If the rebuild's FX runtime spawns every death FX at LOD 0 regardless of distance, honouring the authored LODs is **faithful and the biggest saving**: most deaths in a 63-bot match are far away.
   - Owner: Systems (FX runtime). This is the first thing to check against the ParticleSystem data for Exp_Death_FX, Exp_Death_Sm_FX, TF_Death_GibsTrail_FX, TF_Death_MED_FX and the wreck smoke.
2. **A cap on simultaneously presented bodies**, oldest dissolve first.
   - The original never had more than about 10 players, so at most a handful of bodies at once.
   - Cap the presented corpses at **10** (the original's maximum player count) in extended matches. A new death beyond the cap starts the oldest body's Defrag dissolve / gib early.
   - 5 v 5 never reaches the cap, so it's faithful there and bounded at 63 bots.
3. **Far-body early dissolve / chunk cull.** Bodies beyond about 80 m from the camera (well beyond the gib FX's visible size):
   - skip straight to the gib at half TotalTime;
   - drop chunk meshes beyond 120 m, keeping the FX burst, which is what reads at distance.
   - Up close nothing changes.
4. **Trail emitter count, only if 1-3 are not enough.** Keep all 4 trails within 40 m. Beyond it, 2 trails on the 2 fastest chunks. Since the trails are authored per chunk, this is a distance LOD, not a removal.
5. **Wreck smoke:** follow lever 1's authored LOD. If none is authored, cut the wreck's smoke at its Defrag start (the dissolve hides it anyway).

Never: removing the bodies, gibs, trails or wrecks, or shortening their authored LifeSpans up close.

## Measurement plan

1. **Bisect (Experimental, after the block):** head vs head + `WFC_NOCORPSE=1` vs + `WFC_NOMINEPRES=1`, same match, RT off, p90 and draw counts per map. That gives the bodies' and mines' share of +1.7-3.8 ms.
2. **If the bodies' share is large (≥ 1 ms):** lever 1 first (Systems checks the authored LODs), then lever 2 (Gameplay, small). Re-measure, and add 3 / 4 only if still needed.
3. **Gate for each lever:**
   - p90 / draws per map vs `WFC_NOCORPSE`;
   - corpse crops at 10 m / 40 m / 100 m (the look up close unchanged);
   - BOTTEST "DEATH every presented death has a corpse" (still true; the cap dissolves, it never skips);
   - SIMHASH identical (presentation only).
