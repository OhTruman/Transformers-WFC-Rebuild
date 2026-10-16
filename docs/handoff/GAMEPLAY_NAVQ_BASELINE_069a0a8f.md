# NAVQ baseline: integration 069a0a8f (2026-10-15)

Shipping bot-movement numbers on 069a0a8f. That build has squad tactics (Smart default `squadTactics = 2`), the lockstep input drop, and tip-over v2: Smart bot hover vehicles self-right with the contact-relative gate, while Classic stays on the player path. It replaces `GAMEPLAY_NAVQ_BASELINE_30b2fa74.md` for Smart. **Use 12 seeds and the pooled gate** (below). The raw per-run NAVQ lines are in `GAMEPLAY_NAVQ_BASELINE_069a0a8f.txt`.

## How it was run

- **Env per run:** `WFC_NULLRENDER=1 WFC_MUTE=1 WFC_AUTODETECT=0 WFC_SIMTHREADS=2 WFC_NAVQTEST=1 WFC_BOTAI=<smart|classic> WFC_MAP=<map> WFC_SEED=<1..12>`. Run from the direct-boot run dir, CPU only.
- **Maps:** MP_IAC_Streets, MP_KON_Molten, MP_ORB_Debris, MP_UND_Gorge.
- **Source logs:** pass29 b99 (Smart) and b94 + b98 (Classic). They come from Gameplay's tree, whose sources match 069a0a8f for these files.
- **Verified on the 069a0a8f build:** a 16-run sample (seeds 1 and 7, every map, both AIs) gives byte-identical NAVQ SUMMARY lines (pass29 b103). The runs are deterministic, so any later run with the same env must reproduce the .txt lines exactly. A difference means behaviour changed.

## Gate for later rounds

- Compare 12 seeds against 12 seeds.
- A column counts as worse only if its pooled delta exceeds the noise reference, which is the spread between seed groups: |mean(s7-12) - mean(s1-6)|.
- Use `work/pass29/navq_pooled.js` for the mechanics.
- Why 12 seeds: once two arms differ by one step, every match takes its own trajectory, and a 6-seed 1-SE gate is below match-to-match spread.

## Per-map tables

Means ± SE over 12 seeds.

### Smart (default: tipped hover vehicles self-right, contact-relative gate)

| Map | slow % | reversals / travel-min | off-mesh % | idle5 / bot-min | stucks / bot-min | repaths / bot-min | failed / bot-min |
|---|---|---|---|---|---|---|---|
| IAC_Streets | 5.82 ± 0.58 | 4.70 ± 0.57 | 4.91 ± 0.72 | 0.16 ± 0.02 | 1.56 ± 0.13 | 51.75 ± 1.53 | 0.81 ± 0.09 |
| KON_Molten | 1.60 ± 0.17 | 3.22 ± 0.32 | 1.75 ± 0.17 | 0.21 ± 0.03 | 0.53 ± 0.06 | 49.85 ± 1.40 | 2.50 ± 0.67 |
| ORB_Debris | 3.68 ± 0.43 | 2.90 ± 0.41 | 1.98 ± 0.18 | 0.12 ± 0.02 | 1.02 ± 0.12 | 50.26 ± 1.11 | 0.95 ± 0.22 |
| UND_Gorge | 5.11 ± 0.55 | 4.84 ± 0.51 | 5.20 ± 0.59 | 0.18 ± 0.03 | 1.44 ± 0.20 | 53.36 ± 1.14 | 1.35 ± 0.35 |

### Classic (player path; byte-identical to 30b2fa74)

| Map | slow % | reversals / travel-min | off-mesh % | idle5 / bot-min | stucks / bot-min | repaths / bot-min | failed / bot-min |
|---|---|---|---|---|---|---|---|
| IAC_Streets | 6.62 ± 0.81 | 3.07 ± 0.40 | 4.88 ± 0.77 | 0.02 ± 0.01 | 1.61 ± 0.16 | 51.38 ± 1.34 | 0.64 ± 0.05 |
| KON_Molten | 2.02 ± 0.38 | 2.20 ± 0.40 | 1.60 ± 0.20 | 0.01 ± 0.01 | 0.88 ± 0.17 | 48.46 ± 1.23 | 1.75 ± 0.97 |
| ORB_Debris | 3.82 ± 0.47 | 1.20 ± 0.17 | 2.03 ± 0.20 | 0.03 ± 0.01 | 0.97 ± 0.12 | 50.87 ± 1.48 | 2.78 ± 1.61 |
| UND_Gorge | 6.21 ± 1.09 | 4.63 ± 1.19 | 7.08 ± 1.40 | 0.08 ± 0.04 | 2.12 ± 0.53 | 57.21 ± 1.29 | 1.57 ± 0.36 |

## Smart tipped time

Hover-vehicle episodes over 1 s (jets excluded), summed over 12 seeds:

| Map | Tipped time | Episodes |
|---|---|---|
| Streets | 194.3 s | 72 |
| Molten | 57.8 s | 40 |
| Debris | 39.0 s | 19 |
| Gorge | 235.7 s | 102 |

## Next round's targets (after the playtest)

- **Gorge:** stucks 1.44, slow 5.11, off-mesh 5.20 vs 1.26 / 4.10 / 4.50 before tip-over v2.
- **Squad follow reversals** (the NAVQ reversals split, follow part).
