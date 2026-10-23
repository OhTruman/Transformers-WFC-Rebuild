# FaceFX: master-bone M and face-graph node evaluation / clamping (RE, 2026-10-23)

Closes CAMPAIGN_FACEFX_BONE_BLEND_RE's two open items: "M = ref⁻¹" (was HIGH) and "node value clamping upstream of the bone pass"
(was PARTIAL).
Labels: C = WFC binary (headless Ghidra, FaceFX 1.7 library range 0x832E6000–0x83300000).

## 1. Master bone M = conjugate(ref rotation) (C)
Master-bone construction **0x832F64D8**:
- copies the bone (position +0x08, rotation +0x14 (w, x, y, z), scale);
- then sets **+0x30 = (w, −x, −y, −z) of the ref rotation** (0x832F6570 builds the quaternion from (q.w, −q.x, −q.y, −q.z));
- bone index +0x40 = 0x7FFFFFFF (unmapped), and two 1.0 weights at +0x44 / +0x48.

**So M is the conjugate, which is the inverse for a unit ref rotation.** The bone-blend formula is confirmed:
`rot = ref · Π_i (ref⁻¹ · slerp(ref, pose_i, v_i))`.

## 2. Face-graph node value (C, node evaluate inside 0x832EE1F0)
For a generic (combiner-style) node, each frame:
1. **Start value** by input operation: **sum → 0**, **multiply → 1** (constant table 0x8238D218 indexed by the node's input op).
2. **For each input link:** `x = LinkFn(input.value)`; `acc = acc + x` (sum) or `acc · x` (multiply).
   - **Corrective links** (function 9) instead build
     `corr = min(1, corr + max(input.value·input.max, −input.value·input.min) · param0)`.
3. **+ the track (animation curve) value** for this node, then cleared.
4. **User value** by the node's override mode: **0 add**, **1 multiply**, **2 replace**; then cleared.
5. **× (1 − corr).**
6. **Clamp to [node.Min, node.Max]** (0x832EFC98 = max(min(v, Max), Min)). **This is the clamp.**
7. Store as the node's value. Morph / material-parameter nodes and the bone pass read this clamped value.

Delta-type nodes (type 1): `value = LinkFn(input)`, and with the delta flag the stored output is the change from the previous
value. Type 2: the user value applied by the same add / multiply / replace modes.

**Link functions (0x832EECC8, `x` = input value, `p` = link params):**

| id | function |
|---|---|
| 0 | 0 |
| 1 | linear: p0·x + p1 (2 params), p0·x (1 param), else x |
| 2 | signed quadratic: sign(x)·x²·(p0 or 1) |
| 3 | cubic: x³·(p0 or 1) |
| 4 | signed square root: sign(x)·√\|x\|·(p0 or 1); 0 at x = 0 |
| 5 | negate: −x |
| 6 | inverse: 1/x (0 if x = 0) |
| 7 | one-clamp: x > 1 ? 1/x : 1 |
| 8 | constant: p0 or 1 |
| 9 | corrective: p0 or 0 (the special path above) |
| 10 | clamped linear: with 4 params [m, clampX, clampY, dir], m·x − m·clampX + clampY beyond clampX in the dir sense, else clampY; with 1 param, m·x for x > 0, else 0 |

**For the rebuild:** evaluate nodes in graph order with the steps above. The only clamp is the node's own [Min, Max] at the end
(FaceFX default 0..1 unless authored). The bone pass then uses that value unclamped (CAMPAIGN_FACEFX_BONE_BLEND_RE §3).
