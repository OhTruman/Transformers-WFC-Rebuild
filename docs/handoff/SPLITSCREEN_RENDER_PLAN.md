# Split screen: renderer plan (Rendering, 2026-10-23)

Interface: `render/SplitScreen.h` + `IRenderer::setSplitLayout / beginView / setLocalOwner` (RENDERING_SPLIT_IF). Decisions
(DLC_PLAN.md): every mode, horizontal default + vertical option, full quality per view (per-view post + RT), reductions only
if visually lossless. The original has no split screen (SPLITSCREEN_RE.md), so there is no reference to match.

## Shape: one frame, N views, each view a full scene pass into its window rect
- **Frame-global, once per frame** (stays in `beginFrame` / `endFrame`): `++frameNo_`, `time_`, movers, `tickMapFx`,
  evictions / texture touch, transient-light selection (it swaps `tlights_`: run once, cull per view), the RT character
  BLAS / TLAS feed, pawn-occlusion result readback, the window clear, the HUD, present. Frame generation: OFF while split
  (one swap chain, whole-window capture; a composited depth / motion feed is possible later, not needed for launch).
- **Per view** (`beginView(v, cam)`): camera, jitter, view-projection (and previous), frustum, post-process volume / campaign
  post state / cinematic + fade (Gameplay issues them inside the block), occlusion state, every temporal history, the
  RT pass, the post chain, written into the view's window rect (post / FSR output viewport offset).
- Mechanism: a `ViewCtx` struct holding the per-view members, `views_[2]`, swapped in at `beginView` (members stay as they
  are in the code, so most passes need no edit). The view-0 context is the existing state, so one view = today's frame,
  byte for byte.

## Per-view state (survey, file:line in the commit message of each phase)
- Targets: GL scene / depth / distortion / copies / bloom / shadow mask / velocity / reactive / fog / R1 sun / Hi-Z / FSR1
  (~80 B/px) + the D3D12 bridge (~60 B/px) + denoiser (~20 B/px). Two half views = the same pixels as one full view, so
  memory ~= today's plus the duplicated fixed parts (FSR3 context, froxel volume). One target set per view (no realloc
  churn when the views differ by 1 px at odd window sizes).
- History: previous view-projection + camera-cut state, exposure (`toneAdaptTex_`, `hazePbo_`, the function-static dt),
  reflections temporal (`rtReflPrev_` + the function-static scratch), RT prev colour, denoiser moments, X3 froxel history,
  FSR3 context, MDI Hi-Z visible set, pawn-occlusion query ring, lens-flare queries, post-volume fades.
- Once-per-frame guards to key on (frame, view): `rtFrame_`, `rtVolFrame_`, `r1ClearedFrame_`, `maskClearedFrame_` /
  `maskDrawnFrame_`, `velValidFrame_`, `si.motionFrame` (skinned previous palette: rotate once per frame, read per view),
  pawn occlusion slots, `occCntFrame_`, `chapterFrame_`.

## RT bridge
`g_bridge` holds one `Shared` set + one denoiser + one froxel history. Plan: a view index on `prepare / roundTrip`, one
`Shared` + `dn_` + `volH_` per view, the per-frame rings (`alloc_`, `dyn_` TLAS, light buffer, readback) indexed by view.
The world TLAS and character BLAS are built once per frame and traced by both views.

## Phases (each its own patch, broad-stroke tested)
1. Frame / view split + `ViewCtx` + per-view targets + post / FSR1 into the view rect + guards keyed per view, RT Off.
   Test: Off TDM split horizontal + vertical (two cameras = two seats; a debug switch WFC_SPLITTEST=1|2 renders view 2
   from a second camera before Gameplay's seat 2 exists).
2. RT per view (bridge per-view sets, rings by view, guards). Test: Realistic split, both views RT-lit, no shared history.
3. FSR3 per view (two contexts), frame generation off while split, the HUD rect hook for the Frontend
   (`GfxRendererGL` stage matrix + scissor per seat).
4. Perf row with Experimental (two views ~ one full view of pixels + per-view fixed costs: shadow cascades / RT passes /
   draw submission twice); lossless reductions only if needed.

## Risks
- Hidden per-view state not in the survey (function-statics, caches keyed on frameNo_): the one-view identity check
  (view 0 = today byte for byte) and a split run with two different cameras catch most.
- RT bridge sizes: two views of different size must not reallocate each frame (per-view sets fix it).
- GPU memory at 4K: two 3840x1080 views ~= one 4K view (~1.3 GB of view targets), within today's budget.
