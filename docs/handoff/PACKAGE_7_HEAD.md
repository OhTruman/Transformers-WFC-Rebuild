# Package 7: integration <HEAD> (<DATE>)

**The hand-over build** (user 2026-10-17: package 6 af4d0731 is the internal checkpoint; package 7 is what the user
playtests).

<!-- SKELETON (Systems, opened 2026-10-17 from PACKAGE_6_af4d0731.md's shape). Kept current as items merge; Integration
     merges it by file at each step. At build time: <HEAD>, <DATE>, sizes, the [PLACEHOLDER] rows and the gate table;
     rename to PACKAGE_7_<head>.md. Owner = the lane that ships the item; status = open / in progress / merged <sha>. -->

## Status (kept current; newest first)

- 2026-10-17 14:36: today's merges recorded below (rows + details) for the head candidate Integration names ~15:30 after
  Rendering's verdicts. Systems' card rows of the six-patch FX stack all PASS (body notify 13:56, reverb switch 14:05, Streets
  physmat 14:19). Open from Systems: the transforming-actor door sounds (patch in progress, follows this doc). The online
  phase 1 code is inert offline (offline headless SIMHASH unchanged at every online merge); NET.identity is not yet clean
  (lan / typical / burst splits being fixed by Gameplay; bad 6001 ticks PASS).
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
| Death bodies in full | scorch decals, ragdoll / skeletal gibs, the faithful death picker, the authored death camera, killer cam | Gameplay (decals with Rendering) | merged 1e11edc2 (+ Camera.h roll be0cac77); scorch on and gibs textured since the (A) data switch (integration 9c17be40) |
| Volumetric Lighting setting | the X3 row in Settings > Graphics (pc_strings.int) and its setter | Frontend / Rendering | merged 72eec90c (row); setter merging |
| Online session boundary | ISession / FakeSession (src/frontend/Session.h; no behaviour; GameFlow::setSession unset = offline) | Frontend | merged 3a19b47a |
| Authored FX / sound wiring | every authored weapon / character / vehicle anim notify (tools/systems/gen_anim_notifies.py: 3,554 rows) through one dispatcher: shell / magazine / reload FX for all guns on every visible pawn (distance-gated), melee / ability / transform body trails and hover / jump FX (Gameplay hook World::onAnimNotifyWindow), physmat impact overrides (AssetTools triangle -> physmat in the collision export), R-only muzzles, projectile flight loops, the no-hit ability sounds; data-driven cues checked by a paced CUELOG run first | Systems (+ Gameplay hooks, AssetTools collision field) | merged: weapon notify FX + energon muzzle f27f94f5, FX throttle 5c68f60e, hook d96b21a0, physmat lookup 5fc2e552 + node overrides 93d67979, the six-patch body / ability FX stack f1ce1c84, the FX always-include map_fx_runtime regen (live 13:47); card rows PASS. Door / bridge sounds in progress |
| Vector-channel materials (B) | 110 materials honour their VectorParameter alpha as the shipped PS does (spot check PASS on 3); light shafts, force fields, plasma / lava, rockets, decals, vending, crystals | Systems | merged as a data switch with (A) (Integration) |
| D3D12 presenter exit hang | 1 in 32 exits stuck in the kernel (dump in Experimental work/leak/armB_d3d12present/run-32) | Rendering | merged 76da5484 (separate GL -> D3D12 fence; 64 / 64 clean exits incl. frame generation, resize, map switch) + the X3 GPU timer line |
| Online, phase 1 frontend | online group F: the phase-1 frontend flows complete (44b65357) | Frontend | merged |
| Other merges | audit-3 HUD item (4953261b / a36b2ee1), objectives loader (5cb9886f), movies manifest (aa5a30ea), weapon-collision physmat export (0f3d9b89, AssetTools) | various | merged |
| Iconic Mode | GRI _bIconicMode, the iconic wheel's data, team claims (across all players, RE CONFIRMED); versus "Iconic Characters" row; the iconic lobby drops the Escalation Info row | Frontend | merged 3363f1d0 / a745320b |
| Campaign PROVISIONALs, part 2 | objective Description / UseSubtitles / runtime characters per chapter (RE 2d2a231) | Frontend / RE | merged 95c199f2 |
| Decoy Trap | the ability per RE MP_DECOY_TRAP_RE: Skill_Barrier, a 0.5 s spawn delay and the toss, team-stamped, posing as the owner's current weapon | Gameplay | merged 104b31cc |
| Height-fog layers (Set D) | every enabled HeightFogComponent (up to 4) with per-layer in-scattering as the RE-decoded 360 base-pass VS, all modes; the 'fogs' data | Rendering | merged c73fd924 |
| Set E + door draw | the flash term (Gorge explosion near-white 64.9 -> 60.8 %), the Realistic dark-frame exposure floor, the Remnant light, fog x1.443; the transforming actors drawn by their clip | Rendering | merged dd28a723 |
| Transforming actors | the 32 MP doors / bridges as collision posed by their clip, per-game-type initial state (versus BrokenHope / Remnant: doors open); dying-vehicle collision; the door / light data | Gameplay (+ AssetTools data) | merged 814e7cf5 |
| Weapon + melee authored values | per-weapon spine / hand recoil from each weapon mesh's authored values, the melee batch | Gameplay | merged 9864b4cd |
| Camera recoil | per-shot orbit pitch step, FOV kick (RecoilDelay / RecoilTime), fire shake on the shared camera shaker (1f1d4e17); follow-up: TnBuffReduceRecoil x 0.35, no kick while driving wheeled vehicles (deeb135b) | Gameplay | merged 1f1d4e17 / deeb135b |
| Online, phase 1 code | ISession boundary (3a19b47a); UDP lockstep transport, seat launch, record / replay (.. 68b4a3e6 voice lane .. 3daab0b5 proximity voice in the cue mix + star relay .. e1fc0c6c / df4d62aa / c1b0afb4 identity fixes .. ea4ca7cd per-seat guided missiles .. c069952c); bin/licenses/opus | Systems / Gameplay / Frontend | merged; INERT offline (offline SIMHASH unchanged at every merge); not playable online until NET.identity passes |
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
  socket. Since the (A) material switch (integration 9c17be40; Systems SYSTEMS_MATFIX_DECAL_GIBS, manifest
  docs/handoff/matfix_decal_gibs/MATFIX_MANIFEST.json, every sha256_after verified) the scorch decal is on (DeathDecal_MAT's lerp
  weight takes Color.A as the shipped PS: m max 2.24, was 25.7) and the gibs are textured (DeathSkelGib / DeathGib_Mat_INST
  compiled); Gameplay's follow-up (scorch default on, gib materials, the PET_Random vehicle effect) merged in the same step.
- **Off-screen effects are throttled as the original's** (Systems SYSTEMS_FX_INACTIVE, integration 24c6618c, default on;
  WFC_FXINACTIVE=0 off). CORRECTION in progress: the merged rule freezes placed emitters / pickup highlights 1 s after they
  leave the view; the original throttles instead (RE PARTICLE_MODULE_AUDIT_RE (d), UParticleSystemComponent::Tick decoded):
  every component unrendered > 0.3 s ticks every 0.1 s, past its SecondsBeforeInactive every 1.0 s, each tick applying the
  skipped time in one step - nothing freezes. SYSTEMS_FX_THROTTLE.patch (128b4846) replaces the rule; its three card rows
  (counters, Off / Faithful identity, eye crops) PASS 2026-10-17: Streets 29.0 % / Molten 26.4 % of instance-ticks throttled (784 /
  1127 one-second ticks), particle build cost unchanged; switch 0 equals base within the same-exe run noise; switch-on crops =
  emitters at another phase, density unchanged, nothing frozen (work fxinactive_check4).

- **Hover + squad polish** (Gameplay, integration cfca1717): faithful four-probe springs for every vehicle; Classic bots
  right themselves with the original's AI righting (Streets' longest tipped time 214 -> 9 s); Smart keeps its steadier
  righting as a deliberate improvement; squad follow fidget -30..-45 %. Classic and Smart SIMHASH are re-baselined
  deliberately (tables in docs/handoff/hover/).

- **Authored weapon / body / ability effects** (Systems, f27f94f5 + f1ce1c84; switches WFC_BODYNOTIFYFX=0, WFC_PHYSMATIMPACT=0,
  WFC_MUZZLEENERGON=0): every gun's shell / magazine / reload notifies at the weapon sockets, the TR_* body notifies (trails,
  hover / jump FX) at the body sockets, ability FX (Drain beam / blast, WarCry, Decoy Trap, Hammer trail), surface-specific
  impacts by the hit's PhysicalMaterial. Card evidence 2026-10-17 (rerun_bin, paced lockstep, muted): body notify on Streets
  120 s with Dodge,Drain - SIMHASH equal 7801 steps, 180 NOTIFYFX lines, ABILITYFX Drain x2, 0 new-arm-only cues; physmat on
  Streets with every surface forced to ForceField - SIMHASH equal 4201 steps, 16 / 24 captures differ (the squibs), 0
  new-arm-only cues; MP room reverb (local pawn Touch / UnTouched) on Streets / Rust / Debris - 7 / 14 / 8 switches, every
  preset in RE's per-map list. Presentation only (SIMHASH identical).
- **Online multiplayer, phase 1** (inert offline): the transport, the seat launch, record / replay, the voice lane (Opus,
  radio 2D + proximity as a 3D voice in the cue mix with the listener's room reverb and occlusion, star relay). Not reachable
  from the frontend for the user yet; the playtest is offline.

Package data new in package 7 (verify_slim / slim_files.refresh.tsv, re-measured at the named head by AssetTools): the 10
collision_weapon_physmat.json files (with the frontend manifests), data/frontend/pc_strings.int, bin/licenses/opus, and the
render-data files AssetTools' slim_files.refresh.tsv covers (map_fx_runtime always-include regen, fogs, the door / light data).

[PLACEHOLDER: one bullet per remaining merged item - what the user sees, commit, switch (WFC_*=0 off), Realistic-only /
identity note, measured cost.]

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
   - the 10 weapon-collision physmat files are in the package (Gameplay's hit-surface lookup, 5fc2e552; AssetTools 83b669d rows
     in slim_files.tsv): `ls "work/playtest_slim_<HEAD>/Transformers Rebuild/AssetTools/manifests/maps/"MP_*/collision_weapon_physmat.json | wc -l` = 10
   - the frontend PC texts ship (Catalog.cpp reads them; AssetTools slim_files.tsv row): `ls "work/playtest_slim_<HEAD>/Transformers Rebuild/Rebuild/data/frontend/pc_strings.int"`
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
