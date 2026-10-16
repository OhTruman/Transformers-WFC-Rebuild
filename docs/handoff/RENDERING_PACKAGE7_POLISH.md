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
- **Directions:**
  - Adapt on a centre-weighted or percentile luminance instead of the plain mean.
  - Cut the bloom mix when the frame mean is already high.
  - Let the adaptation catch up faster for sudden brightening, so a brief flash darkens the frame the way the eye
    does.
- **Bar:** the same frame at near-white 55-62 %, warmth +8 or more, yellow-green 5.6 % or less. Ordinary match frames
  and the authored views must not change.

## 2. X3 volumetric light, froxel version

v1 to v3 (screen-space ray march, uncommitted) added a grey veil: contrast 0.79-0.95 and no visible shafts. They were
set aside for package 6. Plan and findings: RENDERING_X3_VOLUMETRICS.md.

## 3. Carried over from the package 6 scope (option C)

- X6.
- X7: the RT lightmap super-sample (item 7's deferred part).
- The FX VelocityOverLife decode (AssetTools 7b1908a queue), so effect particles move as authored.
