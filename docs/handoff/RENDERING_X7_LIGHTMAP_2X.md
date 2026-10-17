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
