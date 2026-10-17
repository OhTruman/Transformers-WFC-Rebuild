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
- **WFC_NORECOIL=1 on every gate arm** (package 7 note, Integration 2026-10-17).
  - 1f1d4e17's camera recoil steps the local orbit pitch on each local shot, and for the local PlayerBot that pitch is its aim. With
    the recoil, the bot's burst lands on a different pawn (p61 vs p44) than on the base, so base and head split at step 5215 (31 v
    32).
  - There is no port into the base. Both arms run with WFC_NORECOIL=1, the diagnostic switch 1f1d4e17 added in onLocalShot. The base
    ignores it, so both arms play one match.
  - The kick is camera-only in cost, so it stays out of the measurement.
  - package-respin.sh and p7-gate.sh export it, and p7-gate.sh stops if the head exe lacks the string.
  - The same mechanism means a player's recoil really does move their aim in play. That is the authored feel, and it isn't a gate
    finding.
- **Each later sim change re-baselines the same way.** Its owner records old vs new SIMHASH / NAVQ references, the commit joins
  the regress base list, and the head ledger marks it "SIM CHANGE".

## Gate state (2026-10-17 midday; from results/PACKAGE7_HEADS.md)

### 1. Sim-changing merges since af4d0731, and the regress base each needs

The base is af4d0731 plus these sim changes only, built with gate-base.sh `BASE_REF=af4d0731`. Each one gets a same-match check
with a negative control.

| merge | sim effect | in the base |
|---|---|---|
| cfca1717 hover / squad / Classic re-baseline | Classic + Smart diverge at step 601-603 | **yes**: gatebase/hover_cfca1717_srcgame.patch. Verified same match, negative control at 603 (results/P7_REGRESS_BASE.md) |
| 1f1d4e17 camera recoil / FOV kick / jet blend | none (SIMHASH identical); the player-view frames move | no (not sim). Matters only for identity / eye bases |
| 104b31cc Decoy Trap | none in bot matches (bots never select it) | no, unless bots or the PlayerBot loadout pick it up |
| pending: Gameplay's per-weapon recoil + melee values batch | sim (its owner records the new NAVQ references) | **yes**, as a further item when it lands |
| pending: transforming actors + dying-vehicle collision | sim (expected) | **yes**, if merged before the head is named |
| pending: the door head (Gameplay's transact 1b894dc7 + Rendering's door draw + (b) lighting + materials) | sim (Integration) | **yes**: only the sim part (the transact side) goes into the base; the draw / lighting / materials stay head-only |
| 8454c173 / f46bdfe4 remote-seat paths | sim only with remote seats / a host seed | no: offline identical (seed 7 48 / 48) |

The base is rebuilt once the head is named. Today's fast_af4d073_hover_hl2 carries cfca1717 and the headless runner.

**How later sim changes enter the base** (Integration, option (a)):

- Each sim change's owner supplies a minimal sim-only diff against the current base (af4d0731 + hover + runner). Gameplay does this
  for 814e7cf5 now, and for the weapon + melee batch when it merges.
- A commit's own src/game no longer applies cleanly: for example, 814e7cf5's World.cpp hunk 7 fails because of intermediate
  integration changes.
- Each diff is verified by the same-match row (HEADLESS=1) against the head, with a negative control.
- The regress then reads "package 7 renderer vs package 6 renderer at the same sim". That is the number the user's rule needs: no
  fps cost without a fidelity gain.

### 2. Render-data switches in Rebuild/work/render today

| switch | time | effect |
|---|---|---|
| (A) decals / gibs | 09:12 | |
| map_fx_runtime.json regen | 10:16 | the Set C particle module's data |
| 'fogs' | 11:18 | |
| (B) materials | 11:29 | |
| FX always-include regen | pending | |

How the combined gate's arms pick them up:

- **Both arms read the same render data.** link-render points base and head at Rebuild/work/render, so data never separates them.
  The regress reads the head's renderer cost against the base renderer on today's data, at the same sim.
- **Freeze the data at the hold start.** The gate takes one snapshot of Rebuild/work/render (as matb_root.ps1 -NoCandidate does:
  map files copied, sub-folders junctioned read-only), and both arms read that snapshot. Then a mid-gate data switch can't split
  the arms or the reps. Integration only needs to avoid the 30-60 s of the snapshot build itself.
- **Check that the base exe reads the new files.** The base exe (the af4d0731 renderer) reads data written for the newer code:
  the 'fogs' key, the new map_fx format and the (B) GLSL. The gate checks each base run's log for map_fx / material load warnings.
  If the old exe can't parse the regenerated map_fx, the base arm gets the snapshot's pre-regen map_fx (the -MapFxFrom option).
  The difference then reads as "particle data + code", and the report says so.

### 3. Per-head identity rows still open

| head | status | owner |
|---|---|---|
| 1e11edc2 death presentation patch 2 | Off + Faithful lockstep pair (base af4d0731 vs 1e11edc2, Berth 63 bots), diffs classed against the death-frame list | Experimental, about 10 min of card |
| Systems' six-patch stack | held: body-notify key / units bug, ability FX templates missing from map_fx; re-run after the fix | Systems |
| Rendering cand30 / x14 / x15 | in Rendering's slots now | Rendering |
| the door head | not merged yet | owner tbd |
| 35811523 / 4953261b HUD jam / spectate display | needs Gameplay's buff / spectate state to exercise | Gameplay, then HUD shots |
| 3363f1d0 Iconic Mode | eye check when the row is offered (default Off) | Frontend |

### 4. What the combined timed gate needs

The run is `RERUN_1B=1 EYE_FROM=<the head's eye pass> GATE_BASE=work/ab/fast_af4d073_<p7 base> bash tools/fidelity/package-respin.sh
<p7 head> af4d0731`.

- **CPU before the window, about 25 min:** build the head and the base at -j 4, run the base's same-match check, and run the eye
  pass on the head. The eye pass is untimed, about 35 min of card.
- **Timed windows:**

  | window | content | length |
  |---|---|---|
  | 1 | the regress (8 maps x 3 arms x 2 reps, about 35 min) + clear-card waits + any whole-map reruns | about 45 min |
  | 2 | delta FAST in between: untimed, shared card | about 15 min |
  | 3 | 2b bodies + 5v5 (about 10 min) + 1b Realistic with the X3=0 / NOCORPSE arms (5 maps x 3 arms, about 35 min) | about 50 min |
  | total | | **about 1 h 50 min** of card, 1 h 35 of it under the hold |

- **Announce text, sent by Integration to every session with an ack collected before the hold file is set:**
  "PACKAGE 7 GATE HOLD from <time>, about 1 h 40, until Experimental's '2b TIMED DONE': no game exes, no compiles (cmake / ninja /
  cl / clang / link / dxc), no interpreters of any kind (python / node / sqlite3, even read-only, even sub-second), no file-heavy
  jobs. Editor tools and git only. AssetTools' chain stays paused. Reply 'ack' to Integration."
- **Experimental sends:** regress TIMED DONE (between the windows, when compiles may resume until the next hold), 2b TIMED DONE,
  any CONTAMINATED whole-map rerun with the foreign process, and the verdict with the report path.

## Eye list: items installed but not yet seen

- **(B) ENV_AllShader_p.Energon_River.Energon_Lava_Mat** (Systems' material translator fix): "unverified by eye, authored scalar
  installed".
  - It compiles only on Streets, in two placements 736 m below the playable streets, and on BrokenHope, which is Escalation-only
    with no versus boot. No versus frame can show it.
  - Expected look (Systems): ElectricityColor alpha 150 applied as a scalar instead of the vec4, so a much brighter orange
    electricity term on the power-pipe core strip.
  - Pair it at the first Escalation / BrokenHope boot.
  - AssetTools' independent review of the (B) after root: PASS 10 / 10.

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
7. **A new harness switch is checked before its first run** (Integration 2026-10-17, after a 13:04-13:06 slip).
   - It must already be in Application_Frontend.cpp's direct-boot list (wantsFrontendBoot), and it's tested with only the env a
     plain user would set.
   - A switch missing from the list made WFC_NULLRENDER refuse: "WFC_NULLRENDER: only for direct-boot harnesses". The exe booted
     the frontend windowed for about 2 min during Rendering's x14.
   - Such a refusal is never retried before the list is fixed. Nothing that can open a window runs while another lane holds the
     card.
8. **The D3D12 presenter exit hang.** A 1-in-32 exit hang leaves an unkillable exe stuck in the kernel. Until Rendering's fix is
   verified, no gate arm uses WFC_D3D12PRESENT / FG, except the presenter-fix verification itself.

## Messages to Integration

Send each of these:

- regress TIMED DONE (hold removed)
- 2b TIMED DONE
- any CONTAMINATED rerun, with the foreign process
- the verdict with the report path
