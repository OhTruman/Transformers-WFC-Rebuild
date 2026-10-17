# FaceFX pose hook: Gameplay implementation plan (package 7)

Gameplay, 2026-10-17. Docs only (written during the package 6 gate freeze). The spec is AssetTools' `docs/handoff/CAMPAIGN_FACEFX_RE.md`
(integration 181c9d27), section "Pose hook Gameplay needs".

## Where it sits

The body pose is a `assets::LocalPose` (parent-relative t / r / s per node, `src/assets/SkinnedModel.h`). Layering (crossfade,
masks, additive, aim, skel controls) all happens in it before `poseGlobals` / `skinPose` / `buildPalette`.

The hook is one call at the end of that chain, per talking pawn per frame, and needs nothing from the renderer:

```
void applyFaceFx(const SkinnedModel& model, const FaceFxActor& fx, const FaceFxBinding& bind, int anim, float t, LocalPose& pose);
```

- **It replaces** the face bones' t / r / s (FaceFX overrides the face, it doesn't add to it).
- It runs after the body layering and before `poseGlobals`, so the GPU-skinned and CPU paths both get it with no extra work.
- Pawns not talking skip it (no cost).

## Data

- **`FaceFxActor`, loaded once per `RB_*_FaceFX.fxa`:**
  - master bones {name, rest t / r / s, links {node, dPos, rot, dScale}};
  - nodes {min, max, input links {node, fn, params}};
  - animations {name, start, end, b, extra, curves {node, keys (t, v, slopeIn, slopeOut)}}.
  - The C++ reader follows `fxa_parse.py` byte for byte (big-endian; str = u32 length + bytes). It's checked by walking all 22 files
    to EOF and matching the 3,616-animation count.
- **`FaceFxBinding`, per (model, actor):** master bone → skeleton node index.
  - Bind by **bone name** against `model.nodes` names; the file's skeletonBoneIndex is 0x7FFFFFFF on Trypticon / Omega, and names
    are the reliable key.
  - Plus the space conversion below.

## Space conversion (the main risk)

- FaceFX rest / delta transforms are UE3 bone space, and quaternions are stored (w, x, y, z). `core::Quat` is (x, y, z, w).
- Our world conversion is glTF = (X, Z, Y) / 100, a handedness swap. Whether the exporter also converted bone-local transforms, or only
  the root, is **not known**.
- **Decide it from the data, not by assumption.** For each bound face bone, compare the FaceFX rest (t, r) with the .glb bind local
  (t, r) under the candidate mappings:
  1. identity;
  2. (x, z, y) / 100 with the quaternion axes swapped and negated (handedness);
  3. ×1/100 only.

  The one that matches all 14 bones to about 1e-3 is the mapping. Apply it to dPos and to the rotation delta conj(rest)·rot (a
  similarity transform for rotations), never to the result.
- **Fallback if none matches** (the exporter re-oriented bones): apply the delta in FaceFX space,
  local = glbBind ∘ convert(rest⁻¹ ∘ faceFxPose). That's only correct if the per-bone basis change is constant; check on the jaw.

## Per frame (spec steps, linear mode first)

1. **Curves:** sample each of the animation's curves at t (keys t = i16 × end / 32767, v = i8 / 127 × b).
   - Linear between keys; hold the first / last value outside [start, end].
   - Cinematic curves use Hermite with the stored slopes (i8 / 127 × extra).
2. **Nodes:** value = curve, plus Σ fn(input) for linked nodes (fn 1: m·x + b; fn 10 unknown, so log once and treat as 0). Then clamp to
   [min, max]; nodes without a curve are 0. Evaluate in node order; links point backwards (to be asserted on load).
3. **Bones:**
   - t = rest.t + Σ v·dPos;
   - s = rest.s + Σ v·dScale;
   - r = rest.r · normalize(1 + Σ v·(conj(rest.r)·rot − 1));

   then convert and write into `pose`.

## Test (the spec's pass line)

- **Unit test, no exe (`tests/`):** Optimus `DX_OPRIME_LK002649` at t = 1.0 s, in UE3 space before conversion, must match the row
  of `AssetTools/work/fxa/proof_DX_OPRIME_LK002649_linear.csv` within 1e-4 (positions) / 1e-3 (quaternion components).
  - The test reads the .fxa from ExtractedAssets read-only and the CSV from AssetTools read-only; skip with a message if they're
    absent.
- **Eye check (a slot):** a staged close-up of a talking Optimus, comparing linear against derived Hermite, plus the neutral face
  during the lead-in and after `end`.
  - The blend choice (nlerp sum vs product of slerps) is decided here; it is the spec's open hypothesis.
- **Identity:** pawns not talking are byte-identical (the hook is skipped). SIMHASH and BOTTEST are unchanged (presentation only).

## Interfaces needed from other lanes

- **Systems:** the voice playback position per speaking pawn (the curve clock; t starts at the animation's `start`, negative lead-in)
  and the animation name (dialog line / InterpTrackFaceFX `FaceFXSeqName`). Until then a test env var (`WFC_FACEFXTEST=<actor>,<anim>`)
  drives it from the game clock.

## Estimate

| Part | Cost |
|---|---|
| Reader + walk check | 2-3 h |
| Binding + space probe | 1-2 h |
| Evaluation + unit test | 2 h |
| Eye check | one slot |
