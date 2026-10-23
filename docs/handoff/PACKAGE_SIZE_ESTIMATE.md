# Final package size: estimate and the optional-pack question (Systems, 2026-10-23)

## Today (internal package v3, be5b7cdc)

| part | size |
|---|---|
| HD pack D + M6 maps (`AssetTools/out/hd`, opt-in, off by default) | **21.7 GB** |
| everything else (game, MP maps, render data, DDS, movies 1.5 GB, Escalation, campaign D5) | **~6.1 GB** |
| total | **27.84 GB** |

## The campaign's render data: 98 % duplicate lightmaps

Only A1, A2 and D5 are exported so far (`AssetTools/out/campaign_render`): A1 **3.2 GB**, A2 **2.3 GB**, D5 0.13 GB.

Content hashes of A1:
- **55,825 PNG files (2.42 GB), only 1,320 distinct contents (0.05 GB).** Almost all of it is `lightmaps/`.
- Each of the chapter's **48 unit folders holds a full copy of the chapter's 1,050 lightmaps** (`<unit>/lightmaps/LightMapTexture2D_*.png`, 50,400 files, 2.17 GB).
- They are real lightmaps (256x256 etc.), not placeholders, but stored 48 times.
- A2 is the same: 35,831 files (1.68 GB), 1,107 distinct.
- JSON (0.61 GB) and meshes (0.22 GB) are **not** duplicated.

**Fix at the source:**
- **AssetTools:** export one chapter-level `lightmaps/`, not one per unit.
- **Rendering:** the chapter loader reads the units' lightmaps from the chapter folder (or from the first unit holding them).

A package-level dedup (links) is not portable (zip, copy to a laptop), so it is not the place.

## Estimate for all 10 chapters

The other chapters are scaled from the AssetTools pilot sizes (`work/campaign_pilot/VerticalSlice/Maps/<CH>`, 4.1 GB in all). The exported-to-pilot ratio is A1 4.1x and A2 4.5x as exported, and about 1.6x once the lightmaps are stored once.

| | lightmaps as exported now | lightmaps once per chapter |
|---|---|---|
| campaign render data, 10 chapters | **~17 GB** | **~6.5 GB** |
| full package incl. HD | ~45 GB | ~34 GB |
| package without HD | ~23 GB | **~12.5 GB** |

**Not counted:** campaign-only assets outside `campaign_render` that `slim_files.tsv` (MP-focused) may not list yet, i.e. campaign characters, weapons and effects, and the campaign dialog banks' waves. These need the campaign file list (AssetTools) to measure. The D5 package boot found none missing, but D5 is the smallest chapter.

## Recommendation

1. **HD as an optional pack: yes.** It is 21.7 GB, two-thirds of today's package, and it is already opt-in and off by default (the user's rule), so the base game is complete without it. build_slim already builds the base with `--no-hd`, and the HD folder sits under its own root (`AssetTools/out/hd`, `WFC_HD_ROOT`). A separate "HD pack" zip dropped into the same folder needs no code.
2. **Campaign: keep it in the base package once the lightmaps are deduplicated** (~6.5 GB; base ~12.5 GB, about the original game's install). Split it only if the lightmap fix doesn't land; as exported it alone would be ~17 GB.
3. **Before the final cut:** AssetTools exports the remaining 7 chapters (with the lightmap fix), and the campaign file list joins `slim_files.tsv`, so the package carries the campaign's own assets.
