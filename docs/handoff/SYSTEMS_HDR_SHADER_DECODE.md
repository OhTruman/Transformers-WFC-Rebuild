# 360 HDR path: bloom-gather / uber-post shader decode (Systems, 2026-10-17; read-only)

Source: Engine.xxx ShaderCache exports extracted by the RE lane (RE-Workspace/work/q/eng_ShaderCache_10303.bin; the same
shaders recur in the other eng_ShaderCache_* exports), disassembled with the project's tools/render/xenos_dis.py plus a
literal-pool dump (work/hdrclamp/dump.py). Literal pool: 4 vec4 per shader, mapped to c252..c255 (checked by use: c255.x = 0
in max(x, 0); the 0.25 average). Shader index = position in that export (xenos_dis order).

## Bloom gather, 4-tap (#313; consts BloomScale f5, BloomThreshold f6, PackedParameters f3, MinMaxBlurClamp f4; NO bias)
- literals c254 = (0.25, 0, 0, 0), c255 = (0, 0.0001, 8, 4)
- each scene tap: max(c, 0) then **min(c, 4)** (c255.w): the port's [0, 4] gather clamp is the original's literal. CONFIRMED.
- bloom = sum of taps with any channel > BloomThreshold (sgt / max4 / cndge), DOF weight a = min(MaxBlur, pow(sat(|z -
  focus| * invFalloff), exp)) - as the port.
- output rgb = (scene * 0.25 * a + bloom * 0.25 * BloomScale) * **8** (c255.z); alpha = a.

## Bloom gather, 16-tap (#64; adds HalfWidthHalfHeight f5; reads SCENE_COLOR_BIAS_FACTOR f0)
- literals c254 = (0.0001, 1, 0, 0), c255 = (0.5, 0.0625, 2, -1)
- **no clamp on the scene taps** (no min); per-tap weight = BloomScale x (any channel > threshold) + DOF weight.
- output rgb = sum x SCENE_COLOR_BIAS_FACTOR.x x 0.5; alpha = 1/16 x the summed DOF weight. Which variant the game uses
  per platform / quality is OPEN (the 4-tap one matches the port's decode).

## Uber post (#234; the variant without colour correction; #189 / #202 add the CLUT textures)
consts: SceneColorTexture s1, BlurredImage s2, SceneDepthTexture s0, SCENE_COLOR_BIAS_FACTOR f0, PackedParameters f3,
MinMaxBlurClamp f4, SceneShadowsAndDesaturation f5, SceneInverseHighLights f6, SceneMidTones f7,
SceneScaledLuminanceWeights f8, GammaColorScaleAndInverse f9, GammaOverlayColor f10; literals c255 = (0.125, 1, 0.0001, 0).
1. a = DOF weight from depth (as the gather); unblurred weight w = sat(1 - a).
2. composite = (scene x w + Blurred.rgb x **0.125**) / (Blurred.a + w): the 0.125 undoes the gather's x 8, so the blurred
   buffer stores 8x its value (its format sets that buffer's own ceiling: 7e3 -> 31.875 / 8 = 3.98, A8R8G8B8 -> 1 / 8).
   The bloom buffer's format is OPEN (native side).
3. **x = sat(composite - Shadows)** (mad_sat): the scene clips per channel at 1 + Shadows HERE, in the units the scene
   texture is read in (biased; the shader applies no 1 / bias to the read). This is the original's clip point for a
   point-blank explosion; the gather's 4.0 only limits bloom.
4. x *= InverseHighLights; pow(max(x, 0.0001), MidTones); desaturation lerp with SceneScaledLuminanceWeights and
   GammaOverlayColor; **sat(x * GammaColorScale)**; pow(max(x, 0.0001), GammaColorScaleAndInverse.w).
5. oC0.rgb = result x **SCENE_COLOR_BIAS_FACTOR.x** (no other scale constant).
- No register holds 1 / bias in either shader; the gather (16-tap) and the uber post both MULTIPLY by bias.x.

## What decides the Faithful question (open)
- The SceneColorBiasFactor value bound for post passes (and for materials): with bias b, materials store colour x b, the
  uber post clips at biased 1, i.e. unbiased 1 / b, and then multiplies the output by b again. b = 1 makes today's port
  exact; b != 1 moves the clip point (and needs the post's own c0 value, which may differ from the material pass's).
- The 7e3 ceiling (31.875 biased) only matters above 31.875 / b unbiased.
- So for Exp_2500cb_FX (glow x 60 / 60 / 20): per channel it clips at 1 + Shadows unless blue's x 20 term stays under
  that, independent of the gather clamp. Yellow vs white at point blank = whether blue x b exceeds 1 + Shadows.
