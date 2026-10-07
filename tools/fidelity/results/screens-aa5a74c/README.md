# Remnant monitor screens / Streets energon sign on 09c aa5a74c (prop-capture -Material, four views per placement)

**Remnant: 7 of the 20 `fbook_` placements (ENV_NEU_Variation_p, TextureFlipBook diffuse).**
- Every screen shows its green waveform / readout texture from the side that faces it. NOT black (they drew black before).
- No material fallbacks.
- Animation is NOT verified: each view is one frame.
- The large flat red surfaces in some views are the capture camera placed inside wall geometry, not a defect.

**Streets: the one placement with AnimSign3** (mesh Dome_Corridor_WindowScreen_STAT, which also carries AnimSign4 / 5 /
AnimTes). View p00b shows the screen as a bright pale strip with faint detail, not black, and no fallback.
Whether this is the correct energon-sign look or an overexposed / wrong frame is a HUMAN / Rendering check.

## Follow-up (Rendering's request)
- remnant_anim_pairs.jpg: p00d and p02d at t and t + 0.53 s. The waveform shapes / glyph lines differ, so **the fbook_ screens
  ANIMATE** (mean abs frame diff 14 / 7 / 14 for p00d / p01d / p02d).
- sign_isolation.jpg, Streets p00b, left to right:
  - normal t, normal t + 0.53 s: a pale strip with faint pink glyphs; small change in the strip region (mean diff 0.31);
  - WFC_SKIPMAT=AnimSign (signs hidden): a flat pale grey / white bar = the base AnimTes_staticLong;
  - WFC_SKIPMAT=AnimTes_staticLong (base hidden): the sign glyphs alone read **pinkish-white, not cyan** (Rendering expects
    AnimTest3 avg RGB 14, 61, 86 -> cyan).
  => both layers are off: the base is pale, and the sign layer's colour is wrong (Rendering to check the AnimSign3 graph /
  flipbook source / the 3x energon-noise path).

## Closed: the Streets sign matches
streets_sign_noclut.jpg: with WFC_NOCLUT=1 (Streets' authored CLUT ENV_MPCLUT_p.MP_Streets_CLUT off, diagnostics only) the
strip shows CYAN / BLUE animated glyphs and a cyan ring. The pale-pink look is the authored colour grade acting on a correct
cyan emissive (Rendering's graph analysis), so it MATCHES - not a texture / channel bug.
