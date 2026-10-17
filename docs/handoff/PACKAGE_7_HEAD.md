# Package 7: integration <HEAD> (<DATE>)

**The hand-over build** (user 2026-10-17: package 6 af4d0731 is the internal checkpoint; package 7 is what the user
playtests).

<!-- SKELETON (Systems, opened 2026-10-17 from PACKAGE_6_af4d0731.md's shape). Kept current as items merge; Integration
     merges it by file at each step. At build time: <HEAD>, <DATE>, sizes, the [PLACEHOLDER] rows and the gate table;
     rename to PACKAGE_7_<head>.md. Owner = the lane that ships the item; status = open / in progress / merged <sha>. -->

## Status (kept current; newest first)

- 2026-10-17 06:13: the user rebooted (the kernel-stuck presenter exe released the card; nonpaged pool 1.1 GB after the reboot);
  the freeze is lifted. Card queue today, in order (one exe at a time, Integration's GO each):
  1. Rendering: Streets match run (X3 Streets haze step).
  2. Gameplay: death-presentation patch 2 crops (agents/gameplay a208d3b0; headless 48 / 48, SIMHASH identical; Systems'
     FX-count row PASS, 0 mismatches over 230 lines).
  3. Systems (one exe: af4d0731 + SYSTEMS_LOCAL_DEATH_SOUND + SYSTEMS_FX_INACTIVE, work/ldeath/tree): the local death cue
     paced check (tools/systems/death_cue_check.ps1 -PacedOnly), then the SecondsBeforeInactive rows - counters (Streets +
     Molten, WFC_FXINACTIVE 0 vs 1), identity (switch 0 byte-identical to af4d0731; switch 1 scored by crop against the
     logged-freeze list), eye crops turning away > 1 s and back. Default on only if all three agree.

## How to play this build

[PLACEHOLDER: carried from package 6's list (reboot first, Play.bat, Settings > Graphics: 2560x1440, Ray Tracing =
Realistic, HD Textures = Enhanced, F9 cycles Off / Faithful / Realistic), plus where the package 7 items show: reflective /
damp surfaces, the 2x lightmap detail, Streets' haze, a point-blank mine, death bodies (scorch, gibs, death camera, killer
cam).]

Launch with Play.bat. Package: work/playtest_slim (<SIZE>; promoted <DATE>). Fallback work/playtest_slim_af4d0731
(package 6). Tag: playtest/09c-package7-<HEAD>.

## New since package 6 (af4d0731)

The user's package 7 scope (2026-10-17), in the order given; visual items first.

| item | what | owner | status |
|---|---|---|---|
| X6 sheen | more reflective surfaces + a damp sheen (Realistic) | Rendering | open |
| X7 2x lightmap | the RT lightmap at twice the resolution (Realistic) | Rendering | open |
| Streets haze | Streets' volumetric haze (per-map X3 medium) | Rendering | open |
| Mine saturation, Realistic | the point-blank explosion's exposure / flash term (RENDERING_PACKAGE7_POLISH.md item 1); Faithful unchanged by evidence (SYSTEMS_HDR_SHADER_DECODE.md 6b90296c + RE HDR_SCENE_PATH_RE.md 43d7bc1) | Rendering (Systems evidence done) | open |
| Hover + squad polish | Smart ride height / hover (ride-height probe model, ledge guard v3) and squad follow reversals | Gameplay | merged cfca1717 |
| Online multiplayer, design | ONLINE_MP_ARCHITECTURE.md (with proximity voice, 6045a7a9), ISession alignment (202cef15), online B2 (f1ae9baa); code after package 7 | Systems / Frontend / Experimental | design merged |
| Death bodies in full | scorch decals, ragdoll / skeletal gibs, the faithful death picker, the authored death camera, killer cam | Gameplay (decals with Rendering) | merged 1e11edc2 (+ Camera.h roll be0cac77); scorch gated off until the DeathDecal_MAT regen (Systems); gib materials = Systems' compile (pending) |
| Volumetric Lighting setting | the X3 row in Settings > Graphics (pc_strings.int) and its setter | Frontend / Rendering | merged 72eec90c (row); setter merging |
| Online session boundary | ISession / FakeSession (src/frontend/Session.h; no behaviour; GameFlow::setSession unset = offline) | Frontend | merged 3a19b47a |
| Authored FX / sound wiring | every authored weapon / character / vehicle anim notify (tools/systems/gen_anim_notifies.py: 3,554 rows) through one dispatcher: shell / magazine / reload FX for all guns on every visible pawn (distance-gated), melee / ability / transform body trails and hover / jump FX (Gameplay hook World::onAnimNotifyWindow), physmat impact overrides (AssetTools triangle -> physmat in the collision export), R-only muzzles, projectile flight loops, the no-hit ability sounds; data-driven cues checked by a paced CUELOG run first | Systems (+ Gameplay hooks, AssetTools collision field) | in progress (data done) |
| Vector-channel materials (B) | 110 materials honour their VectorParameter alpha as the shipped PS does (spot check PASS on 3); light shafts, force fields, plasma / lava, rockets, decals, vending, crystals | Systems | candidate ready; eye pass (Experimental) |
| D3D12 presenter exit hang | 1 in 32 exits stuck in the kernel (dump in Experimental work/leak/armB_d3d12present/run-32) | Rendering | merged 76da5484 (separate GL -> D3D12 fence; 64 / 64 clean exits incl. frame generation, resize, map switch) + the X3 GPU timer line |
| Online, phase 1 frontend | online group F: the phase-1 frontend flows complete (44b65357) | Frontend | merged |
| Other merges | audit-3 HUD item (4953261b / a36b2ee1), objectives loader (5cb9886f), movies manifest (aa5a30ea), weapon-collision physmat export (0f3d9b89, AssetTools) | various | merged |
| Death-body cost | one draw per settled body, TLAS / BLAS per body (package 6 item 1: +0.67..+1.94 ms p90 at 63 bots RT off, +0.17..+0.49 ms p50 RT on) | Gameplay + Rendering | open |

### Details (filled as items merge)

- **The local player's death sound** (Systems SYSTEMS_LOCAL_DEATH_SOUND.patch, integration a2ee795b): the original's
  TnPlayerController.DeathSound (BL_LVL_HUD_INTERFACE.HUD_DEATH_POWER_DOWN) now plays, 2D, at every local death (killed,
  suicide, KillZ / fall); our data had it but nothing played it. Audio only: SIMHASH identical (18,000 steps); paced Rust
  check 2 / 2 local deaths with the cue started in the mixer, no channel refused.

- **Death presentation, patch 2** (Gameplay, integration 1e11edc2; Camera.h roll be0cac77): the faithful death picker
  (first authored entry whose types are active; momentum by pawn-space quadrant), one ragdoll skeletal gib per robot death
  (GIB_Body / GIB_LowBody2 in the reference pose, at rest), the authored death camera and the killer cam after 3 s; the
  original's per-entry death effects at the authored sockets (Systems DeathFx.inc: Poke / Energy / TractorBeam / SingleShot
  differ from package 6; vehicle deaths have no skeletal gib or scorch, as the original) and the gibbed camera shake; the
  death light ends with the pawn. Systems' FX-count row: 0 mismatches over 230 deaths, 209 / 215 effects at the authored
  socket. Pending: the scorch decal (gated off until DeathDecal_MAT is regenerated with its lerp mask fixed) and the gib
  materials (untextured until compiled).
- **Off-screen effects freeze as the original's** (Systems SYSTEMS_FX_INACTIVE, integration 24c6618c, default on;
  WFC_FXINACTIVE=0 off): placed emitters and pickup highlights stop updating after 1 s unrendered (their authored
  ParticleSystemComponent.SecondsBeforeInactive) and resume where they froze. Streets 35 / 45 and Molten 62 / 75 effects
  eligible, frozen 45 % / 38 % of their ticks; switch-off byte-identical (Molten within its own 1-2 level run noise); the
  switch-on differences are resumed particles at another phase (crops in docs/handoff/fxinactive/). No cost change.

- **Hover + squad polish** (Gameplay, integration cfca1717): faithful four-probe springs for every vehicle; Classic bots
  right themselves with the original's AI righting (Streets' longest tipped time 214 -> 9 s); Smart keeps its steadier
  righting as a deliberate improvement; squad follow fidget -30..-45 %. Classic and Smart SIMHASH are re-baselined
  deliberately (tables in docs/handoff/hover/).

[PLACEHOLDER: one bullet per merged item - what the user sees, commit, switch (WFC_*=0 off), Realistic-only / identity
note, measured cost.]

### Realistic defaults

[PLACEHOLDER: package 6's table (39 keys on the defaults line) plus the package 7 keys; the verify table patch is paired
with each Rendering defaults change (SYSTEMS_VERIFY_RT_DEFAULTS_*).]

## Known issues

Carried from package 6 (open):
- **Vehicle muzzle lights saturate nearby walls** (authored brightness 25 / 5 m, as the original); WFC_TLIGHTSCALE is the lever.
- **Extended-match body cost** until the death-body cost item merges (numbers above).
- **Squad follow reversals** (Smart) until the squad polish merges.
- **Nonpaged pool growth: a user-side setting, not the game or the driver** (leak arm B, 2026-10-17): the growth is
  Malwarebytes EDR's FlightRecorder.sys pool tags, +63 MB/h in play and +205 MB/h under builds; no AMD or dxgkrnl tag
  moved. Reboot before a long session, or exclude the game / build folders from the EDR's flight recorder (the user's
  choice).
- (CLOSED) D3D12 presenter exit hang: fixed by a separate GL -> D3D12 fence (Rendering, integration 76da5484); 64 / 64 clean exits
  incl. frame generation, resize and map switch.
- **Faithful blend-time HDR residual** (documented, not changed): the 360 scene target saturates at 3.984 during
  blending, our FP16 target does not; visible only if a later translucent layer darkens an over-bright pixel, not seen in
  any capture (RENDERING_PACKAGE7_POLISH.md section 4).
- **Two campaign-only materials fall back** (PROP_NEU_BatteryOS_p.OmegaBattery_MATINST, OmegaBatterySpikes_MATINST: their
  normal-map function is cooked as a stub; SYSTEMS_MATC58_AUDIT.md); no multiplayer effect.
- [PLACEHOLDER: anything the package 7 gate or eye pass adds]

Closed in package 6 (for reference): the sentry turret / 56 weapon and prop materials (item 6, d0a3f988 + 02c743cc), the
point-blank yellow-green in Faithful = the original's look (6b90296c), volumetric light (X3).

## Runbook: build and verify package 7 (Systems; mechanical once Integration names <HEAD>)

Run from F:\Transformers Rebuild\Rebuild-Systems (agents/systems). Everything lands in this worktree's work\; nothing is written
to Rebuild\, AssetTools\ or ExtractedAssets\. The package build is CPU / IO only (no exe); it may run beside Experimental's gate
builds. Exes run only through verify_slim (one at a time, WFC_MUTE / NOINTERACT set by the script); the RT gate waits for a slot.

0. **Pre-flight** (Git Bash):
   - `df -h /f` - need >= 40 GB free (package ~27.5 GB + build tree ~0.4 GB + verify runs ~1.5 GB).
   - `git -C "F:/Transformers Rebuild/Rebuild" log --oneline -1` shows <HEAD>; `ls work/playtest_slim_<HEAD>` must NOT exist.
   - `ls "F:/Transformers Rebuild/AssetTools/out/hd/push_full_snap_20261016_b0-31/pack/hd_index.json" "F:/Transformers Rebuild/AssetTools/out/hd/push_m6/pack/hd_pbr_index.json" "F:/Transformers Rebuild/AssetTools/work/gltf_wfcmat/FROZEN.json"`.
1. **Build + package** (~45 min: Release + PGO build at 2 jobs, then ~3-4 min copy), Git Bash:

       NOPROMOTE=1 JOBS=2 bash work/refresh.sh <HEAD> "F:/Transformers Rebuild/AssetTools/out/hd/push_full_snap_20261016_b0-31/pack" D > work/refresh_<HEAD>.log 2>&1; echo rc $?

   (refresh.sh defaults: OVERLAY = AssetTools/work/gltf_wfcmat, HDPBR = AssetTools/out/hd/push_m6/pack, vbake = work/vbake;
   it waits for nothing, so start it outside a PERF_HOLD.) rc 0 and, in work/refresh_<HEAD>.log / work/refresh_<HEAD>/build_slim.log:
   - `BUILD OK: .../work/refresh_<HEAD>/build` (the script also refuses a build without `WFC_RT_SHADERS=1` or the PGO profile)
   - `overlay push_gltf_wfcmat_20261016 (...): 85 of 85 files replace package rows`
   - `R1 vbake from .../work/vbake: 10 maps: ...`
   - `HD: pack D (3506 files) chosen over the slim_files.tsv HD rows (...)`
   - `HD M6 modern material maps: 498 of 498 rows from ...push_m6\pack (FROZEN.json push_m6_20261016: bytes + sha256 checked)`
   - `slim_files.tsv: ... HD: 4005 files (21.70 GB); M5 runtime: yes`
   - `wrote ...\work\refresh_<HEAD>\work\playtest_slim: keep ..., xpr 7161, ~27.5 GB, ... failures 0`
   - `REFRESH READY (not promoted): .../work/playtest_slim_<HEAD> (head <HEAD>, HD D)`
   - no `PROBLEM` line (build_slim.log); the script exits non-zero on any PROBLEM, a missed overlay or fewer than 498 M6 rows.
2. **Verify** (~15 min RT-off + ~5 min RT gate), PowerShell, with the RT gate held until Integration's GO:

       New-Item -ItemType File -Force 'F:\Transformers Rebuild\Rebuild-Systems\work\RT_SLOT_HOLD' | Out-Null
       $T = 'F:\Transformers Rebuild\Rebuild-Systems\work\refresh_<HEAD>'
       & powershell -NoProfile -ExecutionPolicy Bypass -File "$T\tools\package\verify_slim.ps1" -Package 'F:\Transformers Rebuild\Rebuild-Systems\work\playtest_slim_<HEAD>' -WorkDir 'F:\Transformers Rebuild\Rebuild-Systems\work\verify_<HEAD>' -SubstDrive X: -SkipIntro -SettingsExpected 7 -ForeignCwd -RtBuildDir "$T\build" -HoldFile 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD' -RtHoldFile 'F:\Transformers Rebuild\Rebuild-Systems\work\RT_SLOT_HOLD' *> 'F:\Transformers Rebuild\Rebuild-Systems\work\verify_<HEAD>.log'; "exit $LASTEXITCODE"

   (the head's own verify_slim + rt-build-check come with the refresh tree, tools\fidelity\lib included; X: must be free.)
   On "RT hold file present" in the log, ask Integration for the RT slot; on GO: `Remove-Item 'F:\...\work\RT_SLOT_HOLD'`.
   Expected lines (work\verify_<HEAD>.log; any other outcome = report, do not promote):
   - `HD pack: AssetTools\out\hd (loader default): 3505 rows + 498 M6 modern material rows, 4005 files, 21.7 GB, 0 problems`
   - `package bin: 20 files vs build_slim.json 20; M5 runtime shipped; 0 problems`
   - `overlay: PASS overlay.barrier ...` and `overlay: PASS overlay.frozen: 85 of 85 files ... byte-identical`
   - `render data: materials_pbr.json in 15 map folder(s)`
   - `[Streets] PASS`, `[Gorge] PASS`, `[Debris] PASS` (0 outside reads, 0 failing opens, 282 localized lines, 0 [error])
   - `[Settings] PASS: 7 pass, 0 fail (expected 7 checks)`
   - `[Ray Tracing] PASS: PASS rtcheck.build ... PASS rtcheck.context ... PASS rtcheck.runtime ... PASS rtcheck.r4 ...
     PASS rtcheck.rtlights: ... effect lights on, headlights on; PASS rtcheck.defaults: <n> of <n> as expected`
     (the table must be the one paired with the head's defaults line: giemissive 4, refltemporal, and the X1 / bloommax keys if
     Rendering's later commit is in)
   - `HD runtime row: re-running 508:Streets with HD forced on (level 2)` ... `HD runtime row: PASS` - its log has
     `HD texture pack available: 3505 textures`, `HD modern material maps: 498 (hd_pbr_index.json)`, `Modern materials on for MP_IAC_Streets`
   - `PASS: report ...\work\verify_<HEAD>\REPORT.md`, exit 0; then `Get-Process wfc_rebuild | ? Path -like '*Rebuild-Systems*'` = none.
3. **Report** to Integration: the verify REPORT.md path, the package size / file counts, any non-PASS row verbatim. Fill this
   doc's <HEAD>, <SIZE>, <N>, <OVERLAY>, <DATE> from build_slim.json / build_slim.log.
4. **Promote only on Integration's word after the gate PASS** (the hand-over is the user's), Git Bash, in work/:

       grep -o '"head": *"[^"]*"' playtest_slim/build_slim.json      # must print af4d0731 (package 6)
       [ -e playtest_slim_af4d0731 ] && echo "playtest_slim_af4d0731 exists: stop" || mv playtest_slim playtest_slim_af4d0731
       mv playtest_slim_<HEAD> playtest_slim && grep -o '"head": *"[^"]*"' playtest_slim/build_slim.json

   Fallbacks after it: playtest_slim_af4d0731 (package 6), playtest_slim_9350f19b (checkpoint), playtest_slim_398b27ef
   (package 5) and playtest_slim_30b2fa74 (package 4) stay as they are (delete older ones only on Integration's word; F: free
   space >= 40 GB before the build).

   Play.bat inside the package starts its own exe (verify checks it). Integration copies / announces; never rebuild a package exe
   or touch build/release while the user plays.

## Gates (final head <HEAD>)

| gate | result | notes |
|---|---|---|
| regress-8map (3 arms x 8 maps, RT off, Classic, lockstep) vs af4d0731 | [PLACEHOLDER] | report rows +0.35 ms p90 per map |
| Off / Faithful identity (Rust, Classic): SIMHASH + frames | [PLACEHOLDER] | vs af4d0731; byte-identical except documented fidelity corrections with a re-baseline (hover 2026-10-17: Classic + Smart SIMHASH; FXINACTIVE resumed particles; the decal / gib / vector-channel materials) - each named per draw |
| Realistic + HD Enhanced perf, 1440p 31 v 32, 5 maps (step 1b) | [PLACEHOLDER] | floor ~60 fps at 1440p (report, not a gate) |
| R4 leak rows (GPU dedicated over 5 map changes) | [PLACEHOLDER] | |
| Eye pass: Off / Realistic / Realistic+HD sheets, 5 user maps + authored views + match frames | [PLACEHOLDER] | floors as package 6 (match 1.26 / 1.28, views 1.42 / 1.45) unless Integration resets them |
| verify_slim -SubstDrive X: (maps, HD static + runtime level 2, bin, overlay, Settings, RT gate incl. rtcheck.defaults) | [PLACEHOLDER] | |
| FAST gate | [PLACEHOLDER] | delta vs af4d0731 |
| BOTTEST / NAVQ / SIMHASH (both AIs) | [PLACEHOLDER] | re-baselines named per Gameplay patch |
| Settings survive map change (WFC_SETTINGSTEST) | [PLACEHOLDER] | 7 + any new settings |
