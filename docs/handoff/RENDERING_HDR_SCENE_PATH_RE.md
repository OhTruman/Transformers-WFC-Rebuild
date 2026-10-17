# 360 HDR scene path: 7e3 target, bias, resolve, clamps (native half)

WFC RE lane, 2026-10-17. The shader half is Systems' `Rebuild-Systems docs/handoff/SYSTEMS_HDR_SHADER_DECODE.md` (agents/systems),
which decodes the ShaderCache gather and uber-post microcode. This doc gives the native C++ side and joins the two in §5.

Sources:
- headless Ghidra on the default.xex image (queries `work/q/hdr*.txt`, not committed);
- `xeximg` reads of static data.

## 1. Scene colour format (CONFIRMED)

- `FSceneRenderTargets::InitDynamicRHI` (0x83037390) creates target 1 **"DefaultColor"** with PF_FloatRGB (9), plus an alias
  "DefaultColorRaw" (PF 18), via `0x8302AEB8`.
  - That function creates the resolve texture with `0x829E51F8` and the EDRAM surface with `0x829C7A40`.
- **`GetPlatformSurfaceFormat` (0x829B6F30):** PF 9 / 10 (FloatRGB / FloatRGBA) map to **0x1A2201BF**, the 2_10_10_10_FLOAT
  (7e3) EDRAM colour format.
  - PF 0x13 (A16B16G16R16) maps to 0x1A20AB55.
  - The surface constructor treats GPU formats 0x15 / 0x20 / 0x25 as 8 bytes per pixel, which identifies 0x15 as the 64-bit
    16_16_16_16 EDRAM format.
- **Resolve-target texture formats** (GPixelFormats PlatformFormat, filled at runtime by `0x829E9338`):
  - PF 9: **0x1A22AB60 = 16_16_16_16_FLOAT** (FP16 texture).
  - PF 10 / 19: 0x1A22AB5D (16_16_16_16_EXPAND; GPU format 29).
- **Bloom buffer:** target 0 is **"FilterColor"**, PF_A16B16G16R16 (EDRAM 16_16_16_16 fixed-point, texture 16_16_16_16_EXPAND), at
  the downsampled size (SceneRT +0x34 / +0x38).
  - H: the Xenos 16_16_16_16 EDRAM range is [−32, 32).

## 2. Per-surface exponent bias and the resolve (CONFIRMED)

1. **Surface constructor** (`0x829C7A40` → `0x829C2C20`): `surface[5] = (D3D format == 0x1A2201BF) ? 3 : 0`. Only 7e3 surfaces get
   an exponent bias, and it is 3.
2. **`RHICopyToResolveTarget`** (0x829E4E28, every colour resolve, ~40 callers) calls `D3DDevice_Resolve` (0x824941D8) with
   **Flags = (−surface[5] & 0x3F) << 26**, i.e. D3DRESOLVE_EXPONENTBIAS(−3).

   **Scene colour is divided by 8 on resolve:** `texture = EDRAM / 8`. Depth resolves pass flag 4.

   There is no other scale on the resolve, and none on the texture fetch: the PF 9 texture format is plain FP16.

## 3. SceneColorBiasFactor value (CONFIRMED; was UNKNOWN in pass 5 addendum 32)

- **`RHISetRenderTarget`** (0x829E2E00) runs `GSceneColorBias (0x83712318) = pow(2.0, surface[5])`. It then writes
  **(bias, 0, 0, 0) into the device's pixel-shader constant shadow at +0x1780** and sets dirty bit 63.
  - H: that is PS constant **c0**. This matches Systems' CTABs, where gather and uber post declare the bias at f0 / c0.
- **Values:**
  - **8.0** while drawing into the 7e3 scene colour (also the static initial value, and the value `0x829D2B30` sets);
  - **1.0** for any other target: back buffer (`0x829BDF00` resets it to 1), FilterColor, UI, etc.;
  - `0x829C80E8` saves the bias, sets it for a temporary target, then restores it.
- **Shader parameter setters read the same global:**
  - the screen-copy PS (`0x82CEFDB0`): `SceneColorBiasFactor = (GSceneColorBias, flag 0x8382DD68 ? GSceneColorBias : 1, 0, 0)`;
  - callers at 0x82CCE900, 0x83043478, 0x83043510, 0x82EB6C30, and `0x82ED8188`, which forces 1.0 for mode 4;
  - other readers: 0x82E9DD10, 0x82EC4C18, 0x82FABF38, 0x82FAC628 (post-process).

**Net result:**
- Lit materials write `colour × 8` into 7e3 (the material PS ends with `oC0.rgb *= SceneColorBiasFactor.x`).
- The resolve divides by 8.
- **The scene-colour texture therefore holds unbiased linear values.**
- The ×8 only moves dark values into the better-precision part of 7e3.

## 4. Clamps and exposure

- **The 7e3 ceiling is reached easily (CONFIRMED arithmetic).** In unbiased units the EDRAM max is 31.875 / 8 = **3.984**.
  - Lit material output has no saturate before the bias multiply (MILESTONE03 LIGHTVIS notes).
  - Additive and translucent blends accumulate in EDRAM.
  - So any pixel above about 3.98 real units, such as an explosion or mine core, **clips at 3.984 in EDRAM**. That is the first and
    effective clamp. There is no earlier clamp in the material or blend path that we found.
- **Gather:** Systems' 4-tap gather `min(tap, 4)` sits just above that ceiling, so it is a no-op on legal data. **Our 4.0 clamp is
  faithful.** The effective ceiling is 3.984.
- **Per-frame exposure: none found (PARTIAL).**
  - No "Exposure", "EyeAdaptation" or "SceneColorScale" strings exist in the image.
  - Only the static bias above is set from code.
  - The post-process constants (BloomScale, BloomThreshold, SceneShadows / HighLights / MidTones / Desaturation, Gamma) are bound
    by name:
    - bloom gather `0x82FF4040`: DOF params via `0x82FB9488` (incl. MinMaxBlurClamp, HalfWidthHalfHeight), plus BloomScale and
      BloomThreshold;
    - uber post `0x82FF2E60` / `0x82FF2F08`.
  - Their values come from PostProcessSettings (map / volume data). They were not traced per frame here.

## 5. Joined with Systems' decode

| Pass | Render target | Bias c0.x at draw | Clamp |
|---|---|---|---|
| Materials (lit / emissive / additive) | DefaultColor, 7e3 | **8** | 7e3 ceiling: 31.875 biased = **3.984 real** |
| Resolve → scene texture | FP16 | ÷8 (EXPONENTBIAS −3) | — |
| 4-tap gather (#313) | FilterColor, 16_16_16_16 | 1 (not declared) | taps max(c, 0), min(c, 4); output ×8 (≤ 32 = the 16-bit EDRAM range, H) |
| 16-tap gather (#64) | FilterColor | **1** | no tap clamp; ×bias.x (1) × 0.5 |
| Uber post (#234 / #189 / #202) | back buffer / UIWithPostProcess, A8R8G8B8 | **1** | Blurred × 0.125 undoes the ×8; `sat(composite − Shadows)` clips at 1 real (before HighLights); final × bias.x = ×1 |

**Answers to Systems' questions:**
1. Bias = 8 for materials into scene colour; **1 for the gather and uber passes**, since they target non-7e3 surfaces.
2. The bloom buffer is FilterColor, PF_A16B16G16R16: 16_16_16_16 fixed-point EDRAM, read as EXPAND. H: ceiling about 32, so
   ×8 of ≤ 4 fits.
3. Which gather variant is bound: **PARTIAL.** One C++ shader class (binder `0x82FF4040`, single construct site `0x82FF66F8`)
   binds the names of both variants (BloomScale, BloomThreshold, HalfWidthHalfHeight). Name binding can't tell which microcode is
   used.
4. EXPONENTBIAS on the scene resolve: **yes, −3**, applied to every 7e3 surface resolve.

**Verdict, using Systems' rule "b = 1 and 4-tap means today's port is exact":** b = 1 in the post passes is now confirmed, and
the scene texture is unbiased with a ceiling of 3.984. If the 4-tap variant is the one bound, the port is exact. Either way, the
saturation point for point-blank explosions is set by:
1. the 7e3 ceiling at about 3.98 real units, in the scene buffer;
2. then the uber `sat()` at 1 after the Shadows subtract.

The gather clamp of 4 adds nothing beyond that.
