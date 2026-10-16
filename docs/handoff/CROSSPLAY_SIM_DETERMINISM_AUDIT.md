# Cross-play: simulation determinism audit (design and findings, no code)

Gameplay, 2026-10-16, for Systems' netcode design. The audit is against integration 038bfee9.

**User decision:** cross-play code comes after package 6, PC-to-PC and PC-to-browser first, PS3 and Xbox 360 towards the end.

**Question:** what does the lockstep simulation need for identical results across x64 (clang / llvm-mingw today), WebAssembly (browser) and PowerPC (Xenon / Cell PPU)?

## 1. What guarantees identical results today (x64, one build)

- **Build:**
  - clang (llvm-mingw), `-O2`-class Release, ThinLTO, PGO (`tools/pgo/wfc.profdata`);
  - **no** `-ffast-math`, no `-march` / `-mfma`: x86-64 baseline, scalar SSE2 float math, no x87.
  - Clang's default `-ffp-contract=on` can only fuse to FMA when the target has FMA. Baseline x86-64 does not, so no fused ops are emitted today.
- **Fixed step:** 1/60 s, as `float dt = 1.0f / 60.0f`, the same everywhere. Rendering interpolates; the sim never sees the frame time. Lockstep (`WFC_LOCKSTEP`) runs exactly one step per tick.
- **RNG (no hidden state):**
  - Shared draws use `core::SimRandom`, an xorshift32 seeded from `WFC_SEED` / the match seed (`World::launchMatch` → `simRandSeed`), with `simRand01` = (u32 >> 8) × 1/16777215.
  - Each bot has its own LCG (`BotBrain::rng`, Numerical Recipes constants), seeded per bot.
  - Presentation and audio use `std::rand` (`PresRand`, `AmbientAudio`), which never feeds the sim. `PresRand` switches to the sim stream in lockstep.
  - So the draw **order** is fixed by the serial bot / pawn update order.
- **Threading:** `WorkerPool` runs only per-participant independent work, and its results are applied serially in participant order:
  - bot steering (`botSteer` / `smartSteerPost`);
  - weapon pose ticks (presentation).
  - Shared counters touched on workers are order-independent sums (atomics, e.g. the ledge-guard counts).
  - `WFC_SIMTHREADS=0` vs threaded gives the same `WFC_SIMHASH`, which is the documented check.
- **Proof today:** `WFC_SIMHASH` (FNV-1a over every pawn's exact state and every bot's steering state per step) is byte-identical across runs and thread counts of the **same exe** (PGO / no-PGO and other build variants are not yet verified). **It proves same-build determinism, not cross-compiler or cross-ISA determinism.**

## 2. Where the sim depends on floating point (the hazard surface)

Counts are calls in the sim translation units at 038bfee9.

| Unit | sin / cos | sqrt / length / normalize | pow / exp | atan / atan2 / asin / acos | remainder / fmod | float lines |
|---|---|---|---|---|---|---|
| `CharacterMovement.cpp` (robot, hover, Driving, jet) | 11 | 41 | 6 | 2 | 7 | 156 |
| `World.cpp` (match, damage, abilities, projectiles, separation) | 23 | 164 | 1 | 35 | 6 | 360 |
| `WorldBots.cpp` (Classic + Smart steering / aim / think) | 18 | 92 | 0 | 8 | 1 | 253 |
| `BotNav.cpp` (cells, A*, string-pulling) | 0 | 15 | 0 | 0 | 0 | 37 |
| `Collision.cpp` (ground, segment walk) | 0 | 2 | 0 | 0 | 0 | 84 |
| `Match.cpp` | 2 | 7 | 0 | 1 | 0 | 19 |
| `core/Math.h` (`forwardFromYawPitch`, matrices) | 10 | 1 | 0 | 0 | 0 | 26 |

**Specific patterns that matter:**
- **Transcendentals in the step:**
  - `forwardFromYawPitch` (sin / cos) is on every facing and aim;
  - `std::atan2` builds yaws (`yawOf`) in steering and aim;
  - `std::pow(1 − rate, dt × 30)` makes frame-rate-independent servos (vehicle turn and pitch, `CharacterMovement` ≈ L239);
  - `std::exp(−10 dt)` settles the slope (Driving);
  - `std::acos` gives the corpse / steering angles, `std::asin` the slope pitch / roll.
  
  **None of these are correctly rounded by IEEE 754:** results differ in the last ulp between libm implementations (MSVC UCRT / mingw, glibc, musl / emscripten, the PS3 / 360 SDK math libraries).
- **`sqrt` and `std::remainder`** are exact (correctly rounded / exact by IEEE): safe if the inputs are identical.
- **Accumulations:** velocity / position integration (`v += a dt; p += v dt`), damage sums, the NAVQ / hash sums. Plain float adds in a fixed order are deterministic on any IEEE-754 scalar FPU, **if** the compiler doesn't reassociate (no fast-math) and doesn't contract into FMA.
- **Mixed float / double:** a few expressions (time accumulators, some `double` locals in World / harness code) promote. That's deterministic per ISA, but x87-style excess precision must never be allowed (see §3).
- **Thresholds on floats:** step-up, `kWalkableFloorZ`, ledge tests, `dot > 0.866`, `< 1.5 m/s`. A 1-ulp difference can flip a branch and diverge a match forever: lockstep amplifies, it never heals.
- **Ordering:**
  - **Sorts:** 8 calls to `std::sort` / `nth_element` / `partial_sort` in game code, 1 `std::stable_sort`. Each one sorting floats with possible **ties** (target scoring, cover candidates, distance orders) is implementation-defined, because `std::sort` isn't stable. libstdc++, libc++ and the console STLs can order equal keys differently.
  - **`std::unordered_map`:** one in World's sim state, `squibAccum_` (keyed accumulator, looked up by key and not iterated: safe). The others are audio / level-audio only. Any **iteration** over an unordered container in the sim would be hash-implementation dependent.
  - **`std::map` / `std::set` iteration** is ordered by key: safe.
- **Serialised state / hash:** `WFC_SIMHASH` hashes raw float / int bytes **in host byte order**. Across endianness (PowerPC is big-endian) the same values hash differently, so any cross-platform state check, snapshot or replay format must serialise canonically (little-endian, fixed widths).

## 3. What breaks on each target

| Hazard | x64 clang today | x64 MSVC / another libm | WebAssembly (emscripten / clang) | PowerPC (Xenon / Cell PPU, SDK compilers) |
|---|---|---|---|---|
| libm transcendentals (sin / cos / atan2 / pow / exp / acos / asin) | reference | **differ** (UCRT vs mingw libm) | **differ** (musl-derived) | **differ** (SDK libm) |
| FMA contraction | none (no FMA ISA at baseline) | MSVC `/fp:precise` contracts only with `/arch:AVX2` | none in scalar wasm (relaxed-simd FMA is opt-in) | **yes**: `fmadd` is native and GCC / SN contract by default (`-ffp-contract=fast`) |
| Excess precision (x87) | none (SSE2) | none on x64 | none (wasm f32 / f64 are exact IEEE) | none (PPC FPU is IEEE double, but **float ops may be done in double** without explicit rounding: `frsp` placement) |
| Denormals | preserved (no FTZ / DAZ set) | preserved | preserved | **VMX / AltiVec flush to zero**; the scalar FPU keeps them unless the SDK sets NI mode |
| `std::sort` ties | libc++ / libstdc++ order | MSVC STL order | libc++ | console STL order |
| Endianness of serialised state | little | little | little | **big** |
| Threads | `WorkerPool` (order-independent) | same | wasm threads need SharedArrayBuffer (COOP / COEP); single-thread fallback must give the same result (it does: `WFC_SIMTHREADS=0` is identical) | same pattern |

**PC-to-PC with the same exe:** already deterministic, since the binaries are identical.
**PC-to-browser:** that's the first real cross-compiler pair: x64 clang vs wasm clang (same LLVM, a different libm and ISA).

## 4. Options and cost

**A. Strict-float rules + flags + a shared libm.** This is the recommended first step, for PC ↔ browser.
- **Flags:** `-ffp-contract=off` on every target (no FMA fusion), no fast-math, no `-march` on the sim units, `-fno-associative-math`, plus explicit float rounding where PPC computes in double (`-ffloat-store`-style care, or keep everything `float` and check the PPC codegen).
- **A deterministic libm** compiled into the game for the sim: one source for sin / cos / atan2 / acos / asin / pow / exp, e.g. a correctly-rounded or fixed-algorithm implementation in the CORE-MATH / fdlibm-float style, called as `sim::sin` etc. Used by `core/Math.h` and the sim units. Presentation keeps `std::`.
- **Code:** replace `std::sort` on float keys with `std::stable_sort` plus a total-order tiebreak (the player / cell index). Ban unordered-container iteration in the sim. Serialise state canonically (LE, fixed width) for hashes and snapshots.
- **Cost:**
  - about 2-4 days for the libm swap and call-site sweep (about 400 call sites in the 7 units);
  - a 1-2 day sort / tiebreak sweep;
  - CI: run `WFC_SIMHASH` on x64 and the wasm build from the same seeds, and require byte-identical hashes. That's the cross-platform gate the current same-build gate doesn't give.
- **Perf:** a software libm is slower than the hardware / CRT one. The sim calls are a few thousand per step at 64 bots: negligible next to rendering.
- **Risk:** low for x64 ↔ wasm, which share LLVM. The PowerPC SDK compilers need their own audit of contraction and float-in-double behaviour; the flags may not exist and codegen must be verified.

**B. Fixed-point for the hot paths.**
- **What:** positions, velocities, yaw and the movement / collision math in Q-format integers (e.g. 32.32 or 16.16 per axis), with integer trig tables; floats only for presentation.
- **Gain:** bit-identical on every ISA and endianness-safe (still serialise canonically), and it removes the libm / FMA / denormal classes entirely.
- **Cost:** very high. It's a rewrite of CharacterMovement, Collision queries, BotNav geometry, bot steering and the authored-constant pipeline (every `[CONF]` value converted), and every Classic reference would be re-baselined. It also risks fidelity (rounding vs the original's float behaviour).
- **Estimate:** weeks, not days.

**C. Server-authoritative / snapshot netcode instead of lockstep.** Mentioned for Systems' design:
- Determinism then matters only for client prediction / replay, and divergence is corrected by the server state.
- It sidesteps most of §3 at the cost of bandwidth and a different netcode architecture.
- The original is UE3, whose replication is server-authoritative (listen / dedicated server), not lockstep.

**Recommendation:**
1. Option A for PC ↔ PC and PC ↔ browser, with the cross-build SIMHASH CI gate.
2. Defer the PowerPC decision until a console toolchain is set up: test A's flags there first, and fall back to C, server-authoritative with the console as a client, rather than B.

## 5. Rule for every sim change from now on

Any code that affects simulation state (movement, collision, bots, weapons, damage, match rules, RNG draws) must follow these rules:
- Use only `float` / `double` arithmetic with **no** compiler-dependent transcendentals added outside `core/Math.h` (route new sin / cos / atan2 / pow / exp / acos / asin through the `core` helpers, so one swap covers them later).
- Never iterate an unordered container.
- Never `std::sort` float keys without a total-order integer tiebreak.
- Draw randomness only from `core::SimRandom` or the bot's own `rng`, in a fixed serial order, and never from threads.
- Keep worker-pool work per-participant and order-independent.
- Never branch on a value computed differently per frame rate (use the fixed `dt`).
- Never serialise raw host-endian bytes as a cross-machine format.
- Prove it with `WFC_SIMHASH` serial vs threaded, plus the NAVQ / BOTTEST gates as today.
