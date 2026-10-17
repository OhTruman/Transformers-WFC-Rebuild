# X7: the RT lightmap at 2x resolution (package 7, Realistic)

Rendering, 2026-10-17. Deferred in package 5 for its video memory. The user's package 7 scope asks for it with the per-sub GI
array mapping, so the memory stays in budget.

## Today (R4)

- `giRasterMesh` rasterises each lightmapped sub's triangles into **its own lightmap page's texel grid**, 1:1:
  `GiTexel.pageXY` = page | x << 9 | y << 20. Pages are limited to ≤ 2048 wide and page index < 512.
- `giFinishTexels` adds a 2-texel gutter (copy list).
- The bridge traces into one shared RGBA16F GI page per lightmap coefficient page (`giGlTex_`), the same size as the
  lightmap page.
- `giArrays_` copies them layer for layer into arrays shaped like `lmArrays_`, for the MDI rows.
- The GL draws sample the GI with the **lightmap UV**: page / array / layer = the lightmap's.
- **Streets numbers:** 27 lightmap pages from 2,170 subs → 82 shared GI pages, 69.2 MB on D3D12, plus the same again in
  the GL arrays. 937,589 chart texels + 347,064 gutter texels, against ~2.85 M page texels per coefficient set: **~45 %
  coverage**. The load passes take ~210 ms per 6.

## Naive 2x and why not

GI pages at 2x the lightmap page (same UV, 4x texels):
- **Memory:** ~4x memory, ~+420 MB on Streets (D3D12 + GL arrays), the unused 55 % included.
- **Bake:** load passes ~4x.
- **Packing:** the page / x / y packing overflows at 4096 wide.

## Design: per-sub GI charts at 2x

1. **Charts.** In `giRasterMesh`, take each lightmapped sub's lightmap UV rectangle (its triangles' UV1 bounds in page
   texels, + 2 gutter texels). Rasterise at **2x** into a chart of 2w x 2h texels.
2. **Packing.** Pack the charts (shelf packer, sorted by height) into new **GI pages of 2048 x 2048**, separate from the
   lightmap pages, with as many pages as needed. Per sub: `giPage, giScale(2), giBias(2)` maps lightmap UV1 (normalised
   per page) to GI page UV. The texel packing becomes page | x << 11 | y << 22: pages ≤ 1024 x 1024 per 2048 chart, or
   a wider struct field.
3. **Bridge.** Unchanged in kind: one shared RGBA16F GI page per (GI page, coefficient). The texel list, not the page
   map, drives the tracer, so only the texel packing and the page size change.
4. **GL sampling.**
   - **Per-draw subs:** the lightmap path gets `uGiMap = (scale.xy, bias.xy)` and a GI texture per sub. It replaces the
     lightmap UV for the GI fetch only; the bake fetch is untouched for Off / Faithful.
   - **MDI rows:** the per-draw row already carries the lightmap layer. It gains the GI layer + scale / bias (4 floats
     in the row texture), and `giArrays_` becomes one array of the 2048² GI pages.
5. **Gutter.** The same 2-ring dilation, per chart, inside its own padding.
6. **Memory on Streets** (estimate): (937k + 347k) x 4 texels at 8 B x 3 coefficients ≈ 123 MB, + ~15 % packing loss
   ≈ 140 MB on D3D12, plus the GL array ≈ 280 MB total: **+~140 MB over today**, not +420. The GL array copy could go
   later if the MDI rows sample the shared pages directly.
7. **Bake time.** ~4x the texels, so the load passes go ~210 → ~850 ms, and calibration repeats them up to 4 times
   (~6 s of extra load). The lever is fewer calibration rounds at 2x: calibrate the gain at 1x, or reuse the 1x gain.
   Measure first.

## Checks

- **Eye sheet:** authored views + match frames on the five maps, X7 off / on (WFC_X7=0). Sharper contact shadows and
  light edges on floors and walls, no seams at chart edges, no new leaks.
- **Cost line:** load time and VRAM per map (the RT GI lines), plus frame p50 (sampling cost ~0).
- **Identity:** Off / Faithful byte-identical (all X7 text and data are Realistic-only, built at load in Realistic).
- **WFC_SETTINGSTEST:** X7 rebuilds at a map load only.

## Estimate

10-14 h, as sent.
- Chart rasteriser + packer: 3 h.
- GL per-draw + MDI mapping: 3-4 h.
- Bridge texel packing: 1 h.
- Tuning / seams: 2-3 h.
- Captures, identity, eye sheet: 2 h.

## Update 2026-10-17 ~09:30: built as "same UVs at 2x" (822dcc5d)

Measured page sizes change the design:
- Every MP map's lightmap pages are ≤ 512 wide (764 of ~890 are 256 x 256), so a 2x GI page fits the texel packing.
- Page coverage is ~72 %, not 45 %. Per-sub charts would save only ~25 % of the memory.

So X7 is the simple version: GI pages (bridge + GL arrays) at 2x their lightmap page, same UVs. All lookups are
normalised, so nothing needs a remap. The calibration reads the bake at its own size.
- Memory: ~4x today's GI pages (Streets ~69 MB → ~275 MB on D3D12, the same again for the GL arrays).
- Load passes: ~4x.
- WFC_X7=0 gives 1x.

The chart packing stays a later lossless-memory item. Next: Gorge / Streets views X7 off vs on, the RT GI load / VRAM
lines, eye sheet, identity.

## Next-head plan (2026-10-17 evening, for Integration)

### What X7 is now (rt2: 822dcc5d, 02c9ceb4, 9d6ac5b5, 00998378)

- Realistic only: the RT GI pages (D3D12 bridge + the GL arrays) at 2x their lightmap page, same UVs, 4x texels. Off and Faithful never
  build it. WFC_X7=0 gives 1x.
- No data recipe: the 2x pages are baked at map load from the existing lightmap / lighting data. Nothing in work/render changes.
- Bounce gain: the map's 1x fit is reused (Gorge 0.414, Streets 7.668, Molten 8.336, Rust 2.765, Debris 11.683); the calibration rounds are
  skipped at 2x. A 2x fit is ill-conditioned (Gorge landed at 0.527, ~11 % brighter). At the fixed 1x gain, X7 matches the bake 0.99 on
  the measured pages, so the look stays the 1x look, only sharper.
- The bake yields to the loading screen per page (02c9ceb4 / 9d6ac5b5), so no single no-frame gap holds the bake.

### Measured (1280 x 720 runs, today)

| Map | GI pages | 1x D3D12 GI | 2x D3D12 GI | device VRAM after load, 1x -> 2x | GI load passes 1x -> 2x |
|---|---|---|---|---|---|
| Streets | 82 | 69.2 MB | 269.7-273.7 MB | 3265 -> 4250 MB (+~985 MB) | 1.3-2.1 s -> 1.9-7.5 s (run to run) |
| Gorge | 40 | 84.5 MB | 171.4-175.5 MB | 2922 -> 4000 MB (+~1080 MB) | 1.3 s -> 0.8-2.4 s |

- The device VRAM delta (~1 GB) is much larger than the D3D12 GI delta (+100-200 MB). The rest is the GL array copies plus the bake's
  scratch memory. That gap is the main memory question; it needs the R4 leak / VRAM lines on a 2x run to split it.
- Load-pass time varies 4x run to run at 2x (Streets 1.9 vs 7.5 s): GPU contention from other lanes is the likely cause. Measure on a quiet card.

### Not measured yet: Molten, Rust, Debris

Lightmap pages used: Molten 54, Rust 141, Debris 102 (Streets 81, Gorge 39). Scaling from Streets per page: 2x D3D12 GI roughly Molten
~180 MB, Rust ~470 MB, Debris ~340 MB; device delta possibly ~1-2 GB on Rust. This is an estimate only. Gorge's per-page size differs from
Streets' by 2.5x, so per-map runs are needed.

### Views that show it

X7 sharpens the RT lightmap: contact shadows, light pools and their edges on floors and walls at close range. The existing sheets are
work/wobble/x7v_508.png, x7v_510.png, x7cv_510.png (authored views, X7 off / on). The best views are the authored 'lane' / 'firefight'
floors on Streets and Gorge, and the dark interiors on Rust (lamp pools). Far views and the sky are unchanged.

### Open before a merge

1. VRAM: split the ~1 GB device delta (D3D12 GI vs GL arrays vs bake scratch) on all five maps. If the GL array copy is the bulk, the
   lossless lever is to have the MDI rows sample the shared pages directly (design §6).
2. Load: per-map load-pass time on a quiet card. The watchdog's 5 s no-frame limit is already safe (per-page yields).
3. Eye sheet: X7 off / on on the five maps (authored views + match frames), plus identity Off / Faithful (byte-identical by construction).

### Patch

When Integration names the next head: one patch of the four commits above against it (WfcRtGi.cpp, WfcRt.cpp, WfcRtDefaults.h,
D3D12 bridge), plus a card slot of ~25 min for items 1-3 above.
