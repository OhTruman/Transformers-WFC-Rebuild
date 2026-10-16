# RE item: Faithful's HDR clamp points vs the 360's scene target (open, evidence first)

Status: open. Faithful keeps today's behaviour for package 6; it changes only on evidence (Integration 2026-10-16).

## Today (the port)
- Scene target: GL RGBA16F (max 65504): nothing clamps translucent / additive particles before blending; sprite and mesh
  particle colours are float vertex data.
- Bloom / DOF gather (kBloomGatherFS): each tap clamped to [0, 4.0], the bloom threshold test on that (Faithful).
- Uber post (kPostFS): composite, then sat(c - Scene_Shadows) * InvHighLights -> pow(MidTones) -> desaturation ->
  clamp [0, 1] -> pow(1 / DisplayGamma) -> CLUT (the decoded FUberPostProcess order).
- Realistic (X1): gather clamp 64 with a soft-knee threshold, filmic roll-off instead of the clamp at 1.

## The question
The 360 rendered the scene into a 7e3 float target (max ~31.9; Systems' effects audit). Where in the 360 chain is that
value read and clamped: the gather's own clamp (is it 4.0, or the 7e3 ceiling, or SceneColorBiasFactor-scaled), and the
uber post's input scale? A point-blank explosion peaks yellow-green in the port; a 360 capture may show white-hot.

## Evidence to gather
1. The shipped Xenon gather / uber pixel shaders (ShaderCache, the same decode path as lmspec_verify.py): the literal
   clamp constant(s) and any SceneColorBiasFactor multiply / divide around the scene-colour reads.
2. Where the 7e3 resolve happens (EDRAM -> texture) and whether a bias is applied when resolving.
3. A 360 reference capture of an explosion at point-blank range (if the reference set has one), compared against
   Faithful at the same frame.

## Outcome
A Faithful change (gather clamp / input scale) only with 1 + 2 decoded, ideally 3 confirming; otherwise 4.0 stays.
