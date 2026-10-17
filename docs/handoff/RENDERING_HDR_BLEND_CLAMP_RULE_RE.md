# 7e3 scene colour: the per-draw blend clamp rule (Faithful path)

WFC RE lane, 2026-10-18. This answers Integration's question on the residual in SYSTEMS_HDR_SHADER_DECODE.md; it extends
HDR_SCENE_PATH_RE.md.

## Answer

**Yes: every blended draw into the 7e3 scene colour is clamped per draw, per channel, before the next draw can read it.**

- The scene colour lives in EDRAM as 2_10_10_10_FLOAT. Each channel is an unsigned 7e3 float (biased range **[0, 31.875]**), and
  alpha is 2-bit unorm.
- Blending happens in the EDRAM ROP. The destination it reads is the **stored 7e3 value**, so it is never above 31.875. The blend
  result is converted back to 7e3 when stored, which clamps it to [0, 31.875] and quantizes it.
- So no draw ever sees a destination above **31.875 biased = 3.984375 unbiased** (bias ×8, HDR_SCENE_PATH_RE.md §2–3).

**Evidence:**
- **Format and range, H:** Xenos format semantics. Xenia, the reference emulator, implements this in its ROV output-merger
  (`dxbc_shader_translator_om.cc`):
  - "The destination color never needs clamping because out-of-range values can't be loaded";
  - the blend result is clamped "before packing", with the 7e3 pack "assuming the color is already clamped to [0, 31.875]";
  - the pre-blend source clamp is applied only "if needed", i.e. for fixed-point formats, **not for 7e3**;
  - infinity / NaN in the source "shouldn't affect blending".
- **Shader side, CONFIRMED:** I disassembled every pixel shader that declares `SceneColorBiasFactor`, i.e. every material PS that
  draws into scene colour. That is **4,272 shaders** in the MP_IAC_Streets_BASE_m, A5_TRY_BASE_m, D5_OME_BASE_m and Engine
  ShaderCaches, lit and unlit (translucent, additive and particle variants included).
  - **None** has a saturate on its `oC0.rgb` write. Every one ends `MULv oC0.xyz, rN, cK.x` (colour × bias) with the clamp bit off.
  - Script: `tools/hdr_satscan.py <package>`, using `tools/xenosdis.py`.
  - **So the source colour reaches the ROP unclamped. The only clamp is the 7e3 store.**

## The rule for the Faithful path (RGBA16F scene target, unbiased units)

For every draw into scene colour, with blending enabled, per pixel:

```
src  = PS output                                    // NOT clamped (no shader saturate, no 7e3 source clamp)
dst  = current scene value                          // already within [0, 3.984375] by induction
out  = blend(src, dst)                              // the draw's blend op and factors, evaluated in full float
out.rgb = clamp(out.rgb, 0.0, 3.984375)             // per channel, independently (31.875 / 8)
out.a   = clamp(out.a, 0.0, 1.0)                    // alpha is 2-bit unorm on the 360 (optionally quantize to n/3)
store out
```

**Notes:**
1. **Per channel, independently.** There is no hue-preserving or max-channel scaling. A white-hot additive stack clips to
   (3.984, 3.984, 3.984); a coloured one clips only the channels that exceed it. This is the 360's flat yellow-white core.
2. **The lower bound 0 matters.** 7e3 is unsigned, so a reverse-subtract or a negative PS output stores 0, and a later additive
   draw starts from 0, not from a negative value.
3. **The source is not clamped.** An src > 3.984 in a modulate or blend-factor term (e.g. `dst × src`, `src × srcAlpha`) uses its
   full value; only the result is clamped. Don't pre-clamp the source.
4. **Opaque draws, blending off:** the store clamp applies the same way, `clamp(src, 0, 3.984375)`. Applying it to every scene draw
   is exact.
5. **Optional (precision, not range):** the 360 also quantizes each store to 7e3, which has 7 mantissa bits. Faithful can skip
   this; the visible effect is the range clamp above.
6. **NaN / Inf** from the shader: the hardware ignores them in blending (H via Xenia). In the port, treat them as 0 / the clamp
   ceiling rather than letting them propagate.
7. **Scope:** only the 7e3 scene colour (biased surfaces with ExpBias 3). The bloom FilterColor buffer is 16_16_16_16 fixed-point
   (range H [−32, 32)) with bias 1, and the back buffer and UI are A8R8G8B8. Those keep their own format clamps; the post passes are
   already faithful (Systems' decode).

**Implementation hint (for Rendering):** with RGBA16F and fixed-function blending, the per-draw clamp needs either:
- a programmable-blend / ROV-style path; or
- a ping-pong resolve with `min(max(scene, 0), 3.984375)` after each translucent batch.

The batch variant is exact only if no blended draw within a batch reads its own earlier results, which is true for overlapping
particles in one batch. So exactness needs per-draw granularity wherever layers overlap. A final `min(scene, 3.984)` before post is
**not** equivalent: it misses the "stack above 4, then darken" case.
