# Rendering: package 7 polish items (Realistic)

Written 2026-10-16 15:24, after the package 6 eye pass (head = 18483783 + 0a299fe9 + a833c984 on integration 3ff0852b).
Off and Faithful are not affected by any item here; each one lands with its own WFC_ switch and the Off / Faithful
identity, as in package 6.

## 1. Exposure adaptation on extreme frames (new from the eye pass)

- **Seen:** Gorge (510) match step 1080 is a point-blank, full-screen explosion. After the white roll (a833c984) its hue
  is right: yellow-green 0.3 % (Off 5.6 %), warmth +13 (Off +8), a warm cream-white. It still covers more of the frame
  than the original: near-white 80 % of pixels against Off's 58 %.
  - Tool: Experimental's `tools/fidelity/frame_compare.py`, run as `frame_compare.py out.png 01080 510 <build dirs>`.
- **Cause:** the X1 exposure adapts to the mean log luminance (uExpo: key 0.02, strength 0.65) and the soft bloom adds
  on top (cap 16). Neither knows that one huge flash fills the screen, so the bright area spreads past the original's
  clip.
- **Bar:** the same frame at near-white 55-62 %, warmth +8 or more, yellow-green 5.6 % or less. Ordinary match frames
  and the authored views must not change.

### What the code does today (WfcPipeline.cpp)

- **Luminance pass:** kToneLumFS writes a centre-weighted log luminance into a 128 x 128 target. Its mip 7 is the
  frame's mean.
- **Adaptation:** kToneAdaptFS moves toward that mean at a rate of 1 - exp(-1.5 dt), the same in both directions. The
  time constant is about 0.67 s, so a flash that lasts under half a second is mostly over before exposure reacts.
- **Exposure:** e = gain x clamp((key / avg)^0.65, **0.7**, 1.8). The 0.7 floor is what holds the explosion frame up.
  At the floor, e = 0.7 x 1.3 = 0.91, about Off's exposure. The 1.15 contrast and the full bloom (mix 1, cap 16) then
  push the flash past the original's clip.

### Design: a flash term (only frames with a large super-bright area change)

1. **Luminance pass:** add a second statistic to the spare channels: the weighted share of samples brighter than a
   flash threshold. The threshold is a luminance of 4 after the previous frame's exposure, about 2 stops past the
   shoulder. Mip 7 then carries the frame's flash share F next to the log mean.
2. **Adaptation pass:** track F in its own channel, with a fast attack (time constant ~0.08 s, about the eye's
   response to a flash) and a slow release (~0.5 s). This leaves the mean's 0.67 s tracking alone, so ordinary
   exposure behaves exactly as it does now.
3. **Exposure:** multiply by mix(1, flashMin, smoothstep(F0, F1, F)), with flashMin 0.55, F0 0.15 and F1 0.6. In the
   same pass, cut the bloom mix by the same smoothstep, from 1 to 0.5.
   - Ordinary match frames, muzzle flashes and lamps keep F below 0.15. Their output is unchanged: multiplying by
     exactly 1 is bit-exact.
4. **Switches:** WFC_TONE_FLASH (0 = off), WFC_TONE_FLASHMIN, WFC_TONE_FLASHF0 and WFC_TONE_FLASHF1, read by rtdef
   accessors. The defaults line gains flash=on flashmin=0.55 flashf0=0.15 flashf1=0.60 (Systems pairs the table).
5. **Cost:** two extra channel writes in the existing 128 x 128 pass, and one more value in the 1 x 1 pass.

### Verification

- **Offline model first:** extend the scratchpad tone model with e and bloom per F. Read F for Gorge step 1080 from a
  WFC_TONEDIAG log line, added next to the flash term.
- **Gorge 510:** a Gorge-only rerun, with frame_compare.py at step 1080 against the bar above.
- **No-flash frames:** the eye pass's authored views and pick frames, minus the flash frames, must be byte-identical
  to the head. This is checked with the same idpair scripts, but on Realistic arms.
- **Identity:** the usual Off / Faithful identity.

## 2. X3 volumetric light, froxel version

v1 to v3 (screen-space ray march, uncommitted) added a grey veil: contrast 0.79-0.95 and no visible shafts. They were
set aside for package 6. Plan and findings: RENDERING_X3_VOLUMETRICS.md.

## 3. Carried over from the package 6 scope (option C)

- X6.
- X7: the RT lightmap super-sample (item 7's deferred part).
- The FX VelocityOverLife decode (AssetTools 7b1908a queue), so effect particles move as authored.

## 4. Mine / explosion saturation (user item, 2026-10-17): scope after the 360 evidence

Evidence: integration docs/handoff/RENDERING_HDR_SCENE_PATH_RE.md + SYSTEMS_HDR_SHADER_DECODE.md.
- **360 path:** 7e3 scene colour with bias 3, resolved /8, so the real ceiling is 3.984. The gather's min(c, 4) is a no-op
  above it. The visible clip is that ceiling, then the uber post's saturate.
- **Faithful:** stays exact, unchanged.

What Realistic already has: the GL scene target is FP16. The soft-bloom gather clamps at uBloomMax 16, not 4, and the
tone curve rolls off instead of clipping (wfcFilmShoulder + the X1 white roll to a warm white of the pixel's own hue).
So the core already has headroom above 3.98 in Realistic.

What remains is how much of the frame the flash covers (item 1's bar: Gorge step 1080 near-white 55-62 %). The treatment:
1. **Flash term:** item 1's design (flash share F, fast attack / slow release, exposure and bloom mix scaled by
   smoothstep(F0, F1, F)).
2. **Core path audit:** check that no Realistic-path pass before the tone curve still clamps at 4 or saturates. Candidates:
   - the additive particle blend into the FP16 target;
   - any min(c, 4) left in the composite;
   - the TAA / FSR input range.
   A clamp found there is lifted for Realistic only.
3. **Roll-off check:** the white roll's last stage (to pure white by 8 x whiteHi) is the remaining "saturation" lever for
   the core. If the eye sheet still reads as a flat white disc, lower whiteMix for F > F0 only.

Eye sheet: Gorge step 1080 and one Molten mine close-up, against the bar above. Off / Faithful identity unchanged.
Estimate 4-5 h (not a two-hour item): queued after X6, X7 and the Streets haze.

Known residual (low priority, Faithful): our RGBA16F scene target does not saturate at 3.984 during additive blending. A
pixel pushed far above 4 by additive layers and then darkened by a later translucent layer can end brighter than on the
360. The fix would be a per-draw clamp in the Faithful path. Do it only after a measured A/B on explosion frames shows a
visible difference; no capture has shown one so far. Realistic keeps the bloom input cap at 16 (headroom), not at the
faithful 3.984.

RE's per-draw rule for that residual (RE-Workspace work/handoff/HDR_BLEND_CLAMP_RULE_RE.md, 8394f38):
- For every draw into scene colour, out = blend(src unclamped, dst), then clamp rgb per channel to [0, 3.984375] and alpha
  to [0, 1] on store.
- No material PS saturates: 4,272 PS were checked.
- A final min() before post does not reproduce "stack above 4, then darken". That needs per-draw granularity where blended
  layers overlap (ROV / programmable blend, or a clamp pass between overlapping translucent draws).
- Scope: the 7e3 scene target only.
- Still low priority: first a measured A/B on explosion frames that shows a visible Faithful difference.

## 5. X6 (more reflective surfaces + damp sheen): PARKED, unmerged (2026-10-17)

Five rounds, each judged on side-by-side eye sheets (work/wobble/ on rt2's worktree). None read as a damp floor:

| Round | Sheet | Design | What the sheet showed |
|---|---|---|---|
| 1 (x6v) | — | damp = amount x noise mask, roughness half-lowered | darker blotches |
| 2 (x6bv) | — | + sharp directional-lightmap lobe | nothing: the 3 lobes only align with steep views |
| 3 (x6c_eyesheet) | x6c | whole films, reflection share >= 0.35 (replace) | darker, flatter floors; matte wall fine |
| 4 (x6d_eyesheet) | x6d | additive only (water Fresnel), transient lobe | no sheen; exposure dims the pickup glow |
| 5 (x6e_eyesheet) | x6e | patches placed by the lamps in view, the lamps' highlights | visually unchanged |

**Cause:**
- These interiors' floors reflect little bright content: dark metal halls, lamps above or behind the view.
- A replacement term pulls a lit floor towards its darker reflection.
- An additive term adds almost nothing.
- Lamp highlights on a floor need a lamp in the mirror direction, which the authored views and match frames rarely have.

The code stays on rt2 (66689c92, 1b12d378, 0b6b0fd2, 82d44dc2, 80a0cd4b) and out of every merge set. A future attempt would need wet geometry or
puddle decals with authored reflections, not a shading term.
