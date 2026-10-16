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
