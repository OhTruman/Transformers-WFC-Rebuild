# Package 7 gate (design, Experimental 2026-10-17)

Package 6 (af4d0731) is the internal checkpoint; package 7 is the hand-over. This is the gate design, using what the package 6
gate taught us on 2026-10-17.

## Per merged head: identity + eye sheet only

These checks are untimed and run in gaps, with one windowed exe at a time.

- **Identity.** Run the owning lane's identity check for the row: same output with the switch off, and the mode gate. Every
  Realistic switch must also check `rtMode_ == 2`. Identity cannot see same-output cost, which is why the combined regress below
  exists.
- **Eye sheet.**
  - Visible items get `eyepass.sh` on the merged exe: match sheets, authored views, and the light-facing extras if the row lights
    something.
  - Each item's sheet goes to the user's beauty-first review.
  - Floors are printed against the checkpoint af4d0731:
    - match all-map Realistic 1.27, + Enhanced 1.2748;
    - views 1.47 / 1.50.
- **No timed rows per head.** A per-head perf number is only a hint. The combined gate is what counts.

## Sim changes inside package 7: identity and the regress base

First sim change: cfca1717 (2026-10-17).

- **What changed.** The hover four-probe springs, Classic on the original's AI righting, Smart's steadier righting, and the squad
  slot fix. Classic and Smart SIMHASH both diverge from af4d0731 from step 601-602, on 96 / 96 runs. This is a deliberate
  re-baseline. The new reference tables are in docs/handoff/hover/: CLASSIC_SIMHASH_old_vs_new.txt and
  *_NAVQ_REFERENCE_new.txt.
- **Renderer identity is only meaningful on the same sim.**
  - Merges before cfca1717 keep base11 / af4d0731 as their identity base. Rendering's Set A / B identity stays valid.
  - For any renderer merge from cfca1717 on, the identity base is the same sim without the renderer change: either the merged
    head minus that commit, or "cfca1717 vs cfca1717 + the renderer change". Off / Faithful identity against af4d0731 differs
    everywhere after step 601, because the bots move differently. Such a diff is never a renderer finding.
  - The identity run checks SIMHASH equality first. A diverging SIMHASH means a wrong base, so the row is UNKNOWN, not FAIL.
  - Per-head identity runs are built at that head's own sha, not a later one. For example, 1e11edc2's Off / Faithful pair is built
    at 1e11edc2 vs af4d0731, because 1e11edc2 predates cfca1717.
- **The combined regress needs the same match on base and head.** The package 6 base (fast_398b27e_rise: 398b27ef plus the
  Classic sim commits) plays the old match, so against the package 7 head the regress would read "different match", a blocking
  row.
  - The package 7 regress base is **af4d0731 + every sim-change commit since** (cfca1717 and any later ones), built with
    gate-base.sh. Commits are taken as `sha` or `sha@path` items limited to the sim files.
  - The base is verified by a same-match lockstep check against the head before the timed regress, with a negative control: a
    base without cfca1717 must diverge at about step 601.
  - The regress then reads package 7's frame cost against package 6 at the new sim: `GATE_BASE=work/ab/<p7 base>
    GATE_BASE_NAME="af4d0731 + sim <shas>"`.
  - The 2b bodies / 5v5 and the 1b arms run the head (plus its own X3 = 0 / NOCORPSE arms), so they are same-match by
    construction.
- **Camera changes on the player view (1f1d4e17).** Recoil / FOV kick / fire shake and the jet blend keep SIMHASH identical, but
  they move every eye-pass and identity frame after the PlayerBot's first shot, because those frames are its own camera. Renderer
  identity and eye passes from 1f1d4e17 on use a base cut from integration at or after 1f1d4e17. Rendering's base11 pairs
  (cand26 / cand27) predate it and stay valid (Integration). The combined regress / 1b don't need it: both arms run the head, and
  the regress base only needs the sim.
- **Windowed identity is judged against the same-exe floor** (Integration 2026-10-17, from Systems' FX throttle 5c68f60e).
  - **The floor.** Two windowed lockstep runs of one exe with identical switches already differ: Streets 2 / 28 and Molten 4 / 28
    frames, at max 1-3 levels, GPU-side.
  - **The bar.** A windowed identity row passes when its differing frames and levels stay within that floor, except for the
    listed expected changes. Byte identity is the bar only for headless SIMHASH and for the Off / Faithful eye-pass shots, which
    are already treated that way.
  - **SIMHASH is always checked.** The sim side stays provable, because the windowed lockstep and eye-pass runs log SIMHASH every
    step: lockstep-ab's "same match" and the eye pass's simhash.txt (6,001 lines per run). Any identity row checks it first.
- **Each later sim change re-baselines the same way.** Its owner records old vs new SIMHASH / NAVQ references, the commit joins
  the regress base list, and the head ledger marks it "SIM CHANGE".

## The package 7 head: one combined gate

Run:

```
RERUN_1B=1 EYE_FROM=<the head's eye pass> GATE_REGRESS_NOTE="<attribution>" bash tools/fidelity/package-respin.sh <p7 head> af4d0731
```

| step | what | timed |
|---|---|---|
| 0 build | Debug + Release of the head (build-target -j 4) + link-render | no (CPU) |
| 1 regress | 8 maps, 3 arms (base / head / head lights-off), RT off, 4K, same match, 2 reps | yes |
| 2 FAST | delta FAST vs af4d0731 (changed-path rows; the rest reused) | no (shared card) |
| 2b bodies + 5v5 | Berth 63 bots base / head / NOCORPSE; 5v5 base / head | yes |
| 1b Realistic | 5 user maps, Realistic + HD 2, 1440p, 31 v 32; head / X3 = 0 / NOCORPSE in the same run | yes (same hold as 2b) |
| 3 report | PACKAGE-GATE.md: leak carried unless the head touches map change / interop; eye pass from EYE_FROM | no |

- **Same-hold comparisons.** The 1b X3-off and NOCORPSE arms ride in the same run as the head. The package 6 gate showed a 1b row
  measured in a different hold can read about 1.7 ms off.
- **Package doc numbers.** The X3 cost and the bodies' RT-on share are always taken in the same hold.
- **Bars.** Unchanged: REPORT rows at +0.35 ms p90. Realistic is report-only, with a floor of 60 fps p50 at 1440p.
- **Lossless perf levers.** A perf lever merges only when it is visually lossless: zero visible difference side by side in its
  eye sheet. The gate reports its gain, but never trades look for frames.

## Hold rules

These are also written into package-respin.sh's header.

1. **Integration grants and announces each window to every session.**
   - Forbidden: interpreters of any kind (python / node / sqlite3, even read-only, even sub-second), compiles, and game exes.
   - Integration collects an ack from each session before the hold file is set.
   - On the package 6 gate, five sub-second pythons from three sessions cost two whole-map reruns.
2. **AssetTools' chain checks `work/PERF_HOLD` before every stage** and pauses. A stage already running when the hold is set is
   waited out by the clear-card check.
3. **Clear-card wait (busy).** Before the first rep of each timed step, the respin waits until no interpreter, compiler or game exe
   from another tree is running.
4. **Contamination.**
   - The busy watchdog (lib/Run.ps1) marks a rep CONTAMINATED when another process passes 25 % CPU or the disk is busy for 3 s.
   - Any map with a CONTAMINATED rep is rerun whole, all arms, once, at the end of its step in the same hold.
   - The first run is kept as `map-<m>_contam`.
5. **Pool guard.**
   - At every step boundary and before each regress map, nonpaged pool above 9.5 GB stops the gate cleanly: hold removed,
     results on disk.
   - Resume after the reboot with SKIP_BUILD=1.
   - Pool context: about 4 GB of the pool on 2026-10-17 was Malwarebytes EDR (FlightRecorder.sys), growing with process and file
     activity. See results/NPP_LEAK_2026-10-17.txt.
6. **One windowed exe on the machine at a time.**
   - WFC_MUTE=1 and WFC_NOINTERACT=1 on every exe.
   - Never edit a running gate script: run a copy.
   - Never kill anything by name: kill by PID with the parent chain checked.
7. **The D3D12 presenter exit hang.** A 1-in-32 exit hang leaves an unkillable exe stuck in the kernel. Until Rendering's fix is
   verified, no gate arm uses WFC_D3D12PRESENT / FG, except the presenter-fix verification itself.

## Messages to Integration

Send each of these:

- regress TIMED DONE (hold removed)
- 2b TIMED DONE
- any CONTAMINATED rerun, with the foreign process
- the verdict with the report path
