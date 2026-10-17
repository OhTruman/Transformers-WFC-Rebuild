# FaceFX bone pose blending, as compiled into WFC (native)

WFC RE lane, 2026-10-17. Integration copies this to `docs/handoff/CAMPAIGN_FACEFX_BONE_BLEND_RE.md`.

**Inputs:** AssetTools' archive / face-graph RE (`CAMPAIGN_FACEFX_RE.md`).

**Source:** headless Ghidra on default.xex. Queries are in `work/q/ffx*.txt` (not committed). Constants were read with `xeximg`.

## 0. Where it lives

- **There is no separate `GetBlendedBone` function.** The FaceFX 1.7 master-bone blend is **inlined** into UE3's skeletal FaceFX
  update, **`0x82D89B40`**. That is USkeletalMeshComponent's FaceFX bone pass, called from the skeletal pose update. It holds:
  - the FaceFX actor instance at component +0x318;
  - LastRenderTime at +0x18C;
  - the blend-mode byte at +0x315.
- FaceFX class registrations, used to anchor the library range: `FxMasterBoneList` serializer 0x832F6DC8, `FxBonePoseNode`
  0x832FC5A8 / 0x832FC8A8, `FxActorInstance` 0x832E8858, `FxFaceGraphNode` 0x832FDE18.
- UE3 quaternion multiply: **`0x827972E0(out, A, B)` = UE3 `FQuat A * B`** (the stock 8-multiply form, using the 0.5 constant at
  0x8209CD30).

## 1. Per-frame flow (CONFIRMED)

1. **Gate:** skip the whole pass unless `WorldTime − Component.LastRenderTime (+0x18C) ≤ 0.33 s` (0x822DA0FC) and an actor
   instance exists.
2. **Tick:** if the instance is playing, it ticks the animation (`0x832E8BA0`, with a time from the owner or WorldInfo).
3. **Bone mapping (once, while the actor's dirty flag `actor+8` is set):** each FaceFX master bone (stride 0x58) is matched by name
   against the SkeletalMesh RefSkeleton (stride 0x40). It stores the UE3 bone index at **master +0x40**, or 0x7FFFFFFF when the
   name is not found.
4. **Before the bones:** a vtable call on the instance at +0x10 (H: BeginFrame / face-graph evaluation).
   - Face-graph nodes have type 6 or 7 (morph target / material parameter). They are pushed to UE3 with **node value at node +0x20**
     (node stride 0x48, at actor +0x3C).
5. **Bone loop** (§2–§4) over every master bone whose index is not 0x7FFFFFFF.
6. **After:** a vtable call at +0x14 (H: EndFrame).

## 2. Data the loop reads

| Struct | Offset | Field |
|---|---|---|
| Master bone (stride 0x58) | +0x08 | ref position (x, y, z) |
| | +0x14 | ref rotation **(w, x, y, z)** |
| | +0x30 | quaternion M (w, x, y, z): H, the inverse of the ref rotation |
| | +0x40 | UE3 bone index |
| | +0x4C / +0x54 | link array / link count |
| Link (stride 0x34) | +0x00 | **face-graph node index** |
| | +0x0C | position delta (x, y, z) |
| | +0x18 | pose rotation **(w, x, y, z)** |
| Node (stride 0x48) | +0x20 | current value `v` (float) |

- The master bone's scale (H: +0x24) and any link scale are **never read** by this loop.
- **Stored quaternion order is (w, x, y, z): CONFIRMED.** The degenerate-normalisation fallback writes 1.0 into the first
  component, and the slerp pairs component +0x18 of the link with +0x14 of the ref.
- **M is HIGH:** for a single link the formula only reduces to `slerp(ref, pose, v)` if M is the inverse of the ref rotation.

## 3. Bone math (CONFIRMED from the decompile)

```
P   = refPos                    // master +0x08
Acc = refRot                    // master +0x14, (w,x,y,z)
R   = refRot                    // working copy used as the slerp start
for each link in stored order:
    v = node[link.nodeIndex].value
    if -1.1920929e-7 < v < 1.1920929e-7: continue          // only |v| < FLT_EPSILON is skipped
    P += v * link.dPos                                     // linear, unclamped
    // hemisphere: if |R + Q|² < |R - Q|²  then R = -R   (Q = link.poseRot; R stays flipped for later links)
    d = dot(R, Q)
    if d <= 0.55:                                          // 0x822B3D1C
        θ = acos(d); s = 1/sin θ
        S = R * (sin((1-v)θ) * s) + Q * (sin(vθ) * s)      // true slerp, extrapolates for v<0 or v>1
    else:
        S = R * (1-v) + Q * v                               // plain LERP, no per-link normalise
    D   = M ⊗ S                                            // Hamilton product, M first (local delta ref⁻¹·S, H on M)
    Acc = Acc ⊗ D                                          // right-multiply, in link order
Acc = normalize(Acc)    // skipped if |Acc|²-1 is within ±FLT_EPSILON; identity (1,0,0,0) if |Acc| < FLT_EPSILON
```

So the blend is **a product of per-link slerps in link order**, not a normalised weighted sum. With M = ref⁻¹:

**`rot = ref · Π_i ( ref⁻¹ · slerp(ref, pose_i, v_i) )`**

- **Position:** `refPos + Σ v_i·Δp_i`.
- **Scale:** not applied.
- **Normalisation:** only at the end, on the product.
- **Exactness detail:** when the ref and the pose are within acos(0.55) ≈ 56.6°, the per-link term is a **linear** blend of the
  two quaternions, not a slerp. That is the common case for face bones.

## 4. Negative and large values (CONFIRMED in this pass)

- **No clamp in the bone pass.** Only |v| < 1.19e-7 is skipped.
- **Negative v inverts:** the position delta is subtracted, and the slerp / lerp extrapolates past the ref, away from the pose.
- **v > 1 overshoots** the pose the same way.
- Any [0, 1] or min/max clamping must happen upstream, in the face-graph node evaluation (node min / max / clamp flags in
  AssetTools' doc). That was not traced here.

## 5. Space and application (CONFIRMED)

**Handedness conversion** FaceFX → UE3, applied to both the blended and the ref transform:
- `FQuat(X, Y, Z, W) = (x, −y, z, w)`, then normalised.
- `FVector = (x, −y, z)`.

**Blend mode** = component byte +0x315 = **`FaceFXBlendMode`**:
- `Engine.Default__SkeletalMeshComponent` authors **FXBM_Additive**.
- No instance in authored.db overrides it, so **every WFC face is Additive.**

**Additive** (the branch taken):
```
Qd = Q_ffx(converted) * inverse(Qref_ffx(converted))     // UE3 FQuat order: Qd = A*B
Atom.Rotation    = normalize( Qd * Atom.Rotation )       // the animated local rotation, pre-multiplied by the delta
Atom.Translation = Atom.Translation + (P_ffx - Pref_ffx) // both converted
Atom.Scale       = unchanged
```

- This is applied to the **local-space bone atoms**: the array of 0x20-byte FBoneAtoms passed in, indexed by UE3 bone index. It
  runs on top of the already-animated pose. The deltas are relative to the **FaceFX ref pose** (master +0x08 / +0x14), not the UE3
  ref pose.
- **Overwrite** (FXBM_Overwrite, unused in WFC): the atom is replaced with `(Q_ffx, P_ffx)` and Scale is set to 1.0.

## 6. For the rebuild

Per frame, while the face was rendered in the last 0.33 s:
1. Take the face-graph node values unclamped (apply only the graph's own clamps).
2. For each mapped bone, run §3 in stored link order: product of the lerp/slerp terms with the 0.55 switch, a single final
   normalise, position as a linear sum.
3. Convert with the Y flip.
4. Apply additively to the animated local transform as in §5. Scale is untouched.

On AssetTools' three hypotheses:
- **"Product of slerps in link order": yes**, with the lerp fast path.
- "Normalised weighted sum": no.
- "Values clamped to [0, 1]": no, not here.
