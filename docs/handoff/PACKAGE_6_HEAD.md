# Package 6: integration <HEAD> (<DATE>)

<!-- SKELETON (Systems; refreshed 2026-10-16 13:05 from docs/MILESTONES_09c.md up to the 12:58 entry). Integration fills
     <HEAD>, <DATE>, sizes, the Rendering rows marked [PLACEHOLDER] and the gate table at build time; rename the file to
     PACKAGE_6_<head>.md. Items marked (verify) were read from the log, not re-checked on the final head. -->

## How to play this build

1. **Reboot first.** The AMD driver's memory pool grows under GPU load and only a reboot clears it.
2. **Start with Play.bat** (not the exe directly).
3. **Settings > Graphics:** resolution 2560x1440; **Ray Tracing = Realistic**; **HD Textures = Enhanced** (HD textures +
   modern materials). Both apply from the next map, so set them in the menus before you start a match. **FPS Counter = On**
   shows the cost in the top-right corner. Expected at 1440p with 31 v 32: Realistic [PLACEHOLDER: p50 / p90 fps],
   Realistic + Enhanced [PLACEHOLDER: p50 / p90 fps] (the gate's step 1b row; the floor is ~60 fps).
4. **Where the difference shows best:** reflective floors (robots and gun flashes in them), streets under the tall
   buildings (sun and shadow, coloured bounce light), firefights (explosions, impacts and boost now light the scene),
   Gorge's interiors (dark corners lit by the lamps and bounce), Molten's lava halls (the lava now glows onto the rock).
5. **Side by side with the original:** press **F9** in a match to cycle Ray Tracing Off -> Faithful -> Realistic in the same
   view (works on a map that loaded with ray tracing on). For the full original look also set HD Textures = Off (from the
   next map). Off / Faithful are the original's image, byte for byte.

**Already known (no need to report):** vehicle muzzle flashes saturate a nearby wall (authored values, as the original);
several explosions right at the camera fill the screen flat yellow (authored colours); no volumetric light shafts or lamp halos (package 7); dead
robots do not leave decals / skeletal gibs yet and the death animation choice is approximate; Smart bots' squads can
reverse direction around a walking leader. Anything else is news.

Launch with Play.bat (maps the package to W: on the dev PC). Package: work/playtest_slim (<SIZE> GB: <N> files kept +
<N> .xpr twins; HD pack D b0-31 3,506 files + M6 roughness 498 maps = 4,005 files / 21.70 GB; glTF overlay <OVERLAY>, <N> files; promoted <DATE>). This is the
hand-over build the user playtests. Fallbacks: work/playtest_slim_398b27ef (package 5, internal checkpoint),
work/playtest_slim_30b2fa74 (package 4). Tag: playtest/09c-package6-<HEAD>.

Reboot before a long session: the AMD driver's nonpaged pool grows with GPU load and only a reboot clears it.

## New since package 5 (398b27ef)

### Realistic lighting (ray-tracing plan v3 and the extra X rows; Realistic only, Off / Faithful unchanged)

Eye-pass sheet on int_cand3 (Experimental, Release with the Enhanced fix, 5 user maps, same frames; all checks pass incl.
the 498 modern maps). Realistic | Realistic + Enhanced vs Off (mean / brighter / contrast / % of pixels):

| map | Realistic | Realistic + Enhanced contrast |
|---|---|---|
| Streets | 27.4 / +18.2 / 1.17 / 43 % | 1.15 |
| Gorge | 22.8 / +17.6 / 1.44 | 1.47 |
| Debris | 25.6 / +8.2 / 1.15 | 1.13 |
| Rust | 26.5 / +19.5 / 1.29 | 1.29 |
| Molten | 23.8 / +20.1 / 1.50 | 1.48 |
| all maps | contrast 1.31 | 1.30 |

(The proof sheet on 0a9c497f read 0.99 - Realistic indistinguishable from Off; batch 4 read 1.27 | 1.05.) Dark Gorge views
read night-and-day (far wall and door lit where Off is black).

Authored views on int_cand4 (25 views x Off / Realistic / Enhanced at 1440p, WFC_BOOT_PROFILE; all checks pass), mean
contrast vs Off, Realistic | Enhanced:

| view type | Realistic | Enhanced |
|---|---|---|
| firefight | 1.26 | 1.29 |
| street under buildings | 1.25 | 1.27 |
| dark interior | 1.44 | 1.48 |
| reflective floor | 1.88 | 1.85 |
| vehicle lane | 1.37 | 1.46 |

Enhanced adds on top in 4 of 5 view types. The final eye pass runs on int_eye = integration 7cd22027 (every package 6 row
merged) [PLACEHOLDER: its sheet / views numbers].

- **Characters take the RT sun** (row 1, bd749603, WFC_RT_CHARSUN): robots and movers in sunlight get the traced sun
  and its shadow, matching the world they stand in.
- **Lamp calibration on characters** (row 8, 5ab0a45c, WFC_RT_CHARLAMPCAL).
- **Sun-share shadows** (row 2, 8b29ff13, WFC_RT_SUNSHARE).
- **Transient lights in reflections** (row 6, 9007d194, WFC_RT_REFLTL): gun blasts and death glows show on reflective
  surfaces.
- **Probe fill** (row 5, 70310b02, WFC_RT_PROBEFILL).
- **Transient light gain 2** (row 4, 03aa1294, WFC_RT_TLGAIN).
- **Row 7** [PLACEHOLDER: Rendering - what it is, commit, switch].
- **Tiled transient lights** (1c94d62e, WFC_TLTILED=0 off): up to 64 dynamic lights per frame in Realistic in 32-px
  tiles (was 8); the first 4 keep RT shadows. Effect lights no longer push muzzle lights out of the cap
  [PLACEHOLDER: confirm the package 5 "transient lights cost at RT off" on the final regress].
- **Tiled lights gated to Realistic** ([PLACEHOLDER: respin commit]): the tiled path's switch ignored the render mode,
  so in Off / Faithful any burning muzzle / impact / death light sent the original's 8 lights through the tiled path
  (tile masks, extra binds, per-pixel fetches) with identical output. Off / Faithful are back on the original 8-light
  uniform path; Realistic unchanged.
- **X1 tone curve / grade** (round 3, f486c10a; WFC_RT_TONE=0 off, WFC_TONE_ADAPT / _BLOOM / _CONTRAST / _GAIN tune it).
- **X2 specular gain 2.5** (a6f5c878, WFC_RT_LMSPECGAIN; WFC_RT_LMSPEC=0 off): a Realistic gain on UE3's
  directional-lightmap specular fed by the RT lightmap (x1.0 in Off / Faithful; the shader text is Realistic-only).
- **X4 coloured bounce** (6acb65ad, WFC_RT_GISAT 2.5 incl. probes) + emissive bounce hook (WFC_RT_GIEMISSIVE).
- **X5-1 character AO 3 m** (cabb416b, WFC_RT_CHARAORADIUS): robots darker under cover.
- **Soft particles** for every effect in Realistic (70315129, WFC_SOFTPARTICLES=0 off): smoke and flames fade into the
  surfaces they touch instead of cutting hard lines.
- **RT surface colours fixed** (Rendering a81f7291, [PLACEHOLDER: integration commit]): the RT hit-data pass culled back
  faces, so every surface with a mirrored UV layout got a grey albedo and no emissive. Bounce light, reflections and
  emissive lighting now see the real colours. Surfaces without hit data, before -> after:

| map | before | after |
|---|---|---|
| Molten | 1,918 | 129 (of 3,799) |
| Remnant | 2,373 | 126 |
| Seed | 1,442 | 91 |
| Complex | 1,537 | 59 |
| BrokenHope | 1,331 | 109 |
| Rust | 1,129 | 28 |
| Gorge | 874 | 52 |
| Streets | 655 | 88 |
| Berth | 602 | 25 |
| Debris | 559 | 30 |

- **X3 volumetrics: not in package 6.** v1 (flat grey haze), v2 (faint haze) and v3 (on the 25 authored views: a grey
  veil that lowered contrast, no readable shafts or halos) were all rejected by eye. The froxel / per-lamp version moves
  to package 7 (RENDERING_X3_VOLUMETRICS.md).
- **Molten's lava lights the scene** (584c873b, Rendering e233a641; WFC_RT_GIEMISSIVE = 4 in Realistic): with the
  surface-colour fix the lava's emissive reaches the bounce light; on lava-facing views rock over a lava basin reads
  contrast 2.13 (gain 1) -> 2.29 (gain 4, a heat glow; 8 reads as a red lamp) vs Off.
- **Effect lights on by default in Realistic** (Systems 709df0f5 + 5f423157): explosions, impacts, vehicle boost and
  pickups give off light, calibrated per template from the decoded fade curves (SYSTEMS_RT_LIGHT_HOOKS); WFC_FXLIGHTS=0
  off. A deliberate Realistic addition (the original has no such lights); Off / Faithful never add one (the live-list fix
  keeps the original muzzle / death lights' order there). The boot log says "RT-era lights (first Realistic frame):
  effect lights on, headlights on".
- **Vehicle headlights on by default in Realistic** (Systems 709df0f5, WFC_HEADLIGHTS=0 off): an RT-shadowed spot per
  vehicle at invented positions (SYSTEMS_VEHICLE_BEAMS; the original has no vehicle light components).
- **Enhanced materials (plan item 6)**: 56 compiled materials added to every MP map (existing entries byte-identical on
  all 10 maps); the sentry turret draws its authored hologram (package 5 known issue closed); the two vending-machine
  materials compile (matc AppendVector fix, Systems regen: exactly those 2 entries changed per map). Modern energy now in
  range (Rust +45.7 -> ~+3 %, Molten +41.5 -> ~-3 %: the Modern GGX lobe only where the original had a specular input);
  materials_pbr.json regenerated on all 10 maps with the energy marker (oracle 0 differ; item 6 merged 02c743cc, data
  switched).
- **Enhanced keeps the original specular** (eaed3192): the Modern shading had replaced the game's directional-lightmap
  specular (large on its metal and rock) with a fixed-F0 GGX lobe, so Enhanced looked as dark as Off; it now takes the
  original's term (WFC_PBR_ORIGSPEC=0 restores the lobe). Gorge: Off + Enhanced = Off + HD (1.00), Realistic + Enhanced
  1.04 -> 1.24 (= Realistic + HD).
- **The Enhanced roughness maps ship** (65dddbe4): the 498 reviewed M6 roughness maps (push_m6) sit beside the D pack;
  package 5 shipped neither the maps nor their index, so its Enhanced row derived every roughness value.
- **Item 7 polish** [PLACEHOLDER: Rendering - temporal reflections / lightmap super-sample, if any shipped].

### Realistic defaults (all on in Realistic only; set the switch to turn one off - harness / diagnostics, never a package default)

| feature | switch | default |
|---|---|---|
| effect lights / headlights | WFC_FXLIGHTS=0 / WFC_HEADLIGHTS=0 | on |
| row 4 transient light gain | WFC_RT_TLGAIN | 2.0 |
| X2 specular gain | WFC_RT_LMSPECGAIN (WFC_RT_LMSPEC=0 off) | 2.5 |
| X1 tone curve | WFC_RT_TONE=0; WFC_TONE_ADAPT / _BLOOM / _CONTRAST / _GAIN | on (gain 1.3) |
| X4 coloured bounce / emissive bounce | WFC_RT_GISAT / WFC_RT_GIEMISSIVE | 2.5 / 4.0 |
| X5-1 character AO radius | WFC_RT_CHARAORADIUS | 3.0 m |
| soft particles | WFC_SOFTPARTICLES=0 | on |
| tiled transient lights | WFC_TLTILED=0 | on |
| rows 1 / 8 / 6 / 2 / 5 | WFC_RT_CHARSUN / _CHARLAMPCAL / _REFLTL / _SUNSHARE / _PROBEFILL | on |
| Enhanced: original specular | WFC_PBR_ORIGSPEC=0 (restores the GGX lobe) | on |
| X3 volumetrics | - | not in this build (package 7) |

All are environment-only (no new menu setting; SETTINGSTEST stays 7). The Realistic boot log names them in one line
("RT Realistic defaults (first Realistic frame): ...") and verify_slim checks it (rtcheck.defaults, SYSTEMS_VERIFY_RT_DEFAULTS).
Off / Faithful stay byte-identical to the original (identity rows).

### Gameplay

- **Death bodies** (Gameplay fcc567dd / a6e9ea67): a killed robot plays its picked death animation from its last pose
  (melee sweep, explosion, headshot, fall / crawl) then gibs; vehicles leave a hull wreck with chunks and smoke that
  dissolves. Package 5 showed only the kill-feed icon. Presentation only.
- **Stranded / fell-out-of-the-world deaths** are presented too (d817d599: stranded / KillZ deaths get the death
  presentation).
- **Kamikaze mines drawn** (Gameplay 050a7796, closed dc5f5a61): mines and their trails render; the detonation effect
  renders (staged check, Systems fd447694).
- **Vehicles turn at the authored rate** (022e5e34: the original's AiMaxAngularSpeed; robot facing snaps 133 -> 0,
  vehicle heading snaps 1,016 -> 173).
- **Cars no longer pin on floor lips** (56a9beca: the driving hull probes at or above the 0.45 m wheel clearance; all 7
  cars sat below it and a boosted car could stop dead on a Debris floor lip, players too).
- **Bots steer correctly in vehicle form** (3de18d73: both AIs steered away from their heading error until boost dropped
  them to hover; every map better or equal; the Debris Smart cost closed). Two permanent DRIVE guard rows in BOTTEST.
- **Robots stand only on walkable ground** (f19e0052): ground support needs a surface under ~45 deg (|n.y| >= 0.7, the
  original's WalkableFloorZ); robots no longer snap onto steep faces or get trapped in the two known spots. 2 of ~460
  traverse routes that climbed steep faces are now blocked - the faithful behaviour.
- **Jumps no longer pop robots onto barrier tops or into wall pockets** (63368d05): while rising, a robot lands only on a
  surface its previous bottom was above (UE3's physFalling rule); NAVQ every |z| < 1.5, traverse routes identical.
- Classic was re-baselined five times by design (yaw rate, drive probe, steer sign, walkable floor, rise snap; reference
  tables in docs/handoff/vehyaw/, driveprobe/, drivesteer/, walkable/, risesnap/).
- **BOTTEST gate robustness** (d817d599): seed-variance tolerant rows, no re-baseline.

### Audio and effects

- **Virtual voices** (Systems 9711d354): the original's FMOD rule that a sound quieter than -64 dB at the listener holds
  no channel (virtual, resumes when audible).
- **128 real voices in 16 v 16 / 32 v 32** (ExtendedPlayers, our extension; Systems b9e6f3d8): the original's 96
  otherwise. 63 bots, paced: refused sounds Streets 1,700 -> 529, Gorge 2,726 -> 779; the player's own sounds are never
  refused.
- **Impact effects with a degenerate surface normal** now draw (one per map was dropped; Systems 9711d354).
- **Effect data decode** (AssetTools 7b1908a, switched 07:59): homing-rocket explosion embers fly (they sat still);
  16 Orbit LODs complete; VelocityOverLife modules carry their Absolute flag. 2,007 LODs identical, 0 unexpected
  changes (docs/handoff/fx_regen/).
- **Particle LOD data matches the original's authoring**: per-LOD values were checked against each cooked
  ParticleLODLevel's own PeakActiveParticles (the editor's count). The original's LOD 1 (15-350 m on most death
  effects) is genuinely heavier than LOD 0 (TF_Death_GibsTrail 26 -> 78 peak particles, Exp_Death Sparks 46 -> 92), and
  the runtime picks LODs by camera distance exactly as UE3's Automatic method. No data change.

### Everything else

- **HD textures actually load.** Packages 4 and 5 silently ignored the pack (the loader read a row's byte count as a
  float; 89,478,612 rounded). Fixed 66eb7d73; the exe logs "HD texture pack available: 3505 textures"; verify_slim has
  an HD runtime row that requires that line (dry run on 64771c71: PASS).
- Harness-only: WFC_BOOT_PROFILE=1 (e22321e8: direct map boots apply the profile's display / HD / Modern rows like the
  frontend, so authored views run at 1440p with the Enhanced column), WFC_NOINTERACT exact client size (7d9edf78, 2560x1440 verified), voice_ab.ps1 -Paced (audio counts
  from lockstep + 60 fps), eye-pass pipeline (27ab8332) + authored views (1a49b965), package-gate.sh + gate-base.sh,
  verify_slim rtcheck.rtlights, tools/systems/off_identity.ps1.

## Known issues

- (CLOSED) Realistic + Enhanced darkness: the Modern shading dropped the original's specular (not the data); fixed eaed3192,
  int_cand3 sheet Realistic + Enhanced 1.30 vs Realistic 1.31.
- (CLOSED pending the eye pass) Molten's lava did not light the scene: fixed by the RT surface-colour fix + emissive gain 4
  (584c873b) [PLACEHOLDER: confirmed by the final eye pass].
- **Bright emissive + muzzle flash can wash out yellow** (X1 soft bloom on very bright frames, Molten step 1200)
  [PLACEHOLDER: the bloom-input cap in item 7 polish shipped or not].
- **Idle 20 s BOTTEST row** at seeds 1 / 5 (47 / 48 before): the walkable-floor fix f19e0052 addresses the trap
  [PLACEHOLDER: 48 / 48 at seeds 1 / 5 on the head, or still open].
- **No volumetric lighting in this build** (sun shafts, lamp halos, lit fog): three versions rejected by eye; package 7
  (RENDERING_X3_VOLUMETRICS.md).
- **Death bodies, patch 2 deferred**: decals, skeletal gibs, the authored death camera and the full momentum picker come
  after package 6; the death-type choice is still an approximation.
- **X6 / X7 = package 7** unless pulled in: more reflective surfaces + damp sheen (X6), 2x RT lightmap (X7).
- **Explosion close-ups saturate**: Exp_2500cb_FX (mine) is authored HDR yellow (modulate glow x60/60/20); several at the
  camera fill the screen flat yellow-green. Data, not a bug; the HDR clamp point vs the 360's 7e3 scene target is a
  Rendering RE item [PLACEHOLDER: shipped or not].
- **Vehicle muzzle lights saturate nearby walls** (authored brightness 25 / 5 m, as the original) - carried from
  package 5; WFC_TLIGHTSCALE is the lever.
- **Off-path cost in extended (> 10-player) matches - reduced in the respin, the rest is package 7**: at 63 bots RT off,
  the death bodies are the whole regression vs package 5 (about 0.2 ms per live body; the mines cost nothing): 1.87 ms
  p90 [PLACEHOLDER: confirm on the respun regress]. The respin's static-pose fix (Gameplay [PLACEHOLDER: commit]: a body
  whose pose went static is no longer re-skinned on the CPU and re-uploaded every frame) recovers 0.39 ms of it; the rest
  is the faithful body presentation at 63 bots. GPU-skinned corpses with the shadow pass are package 7 item 1. 5 v 5 (the
  original's player count): [PLACEHOLDER: measured cost with its few live bodies]. The gib trails' particle load is the original's own LOD authoring (see the
  particle LOD note).
- **Smart ride height / hover**: the remaining Smart vehicle items (hover / jet slow, the ride-height model, audit items
  5 / 6) come after package 6 [PLACEHOLDER: what landed].
- **Squad follow reversals** (Smart) - carried from package 5.
- **Driver nonpaged pool** grows with GPU load (AMD); reboot before a long session.
- [PLACEHOLDER: anything the final gate or eye pass adds]

## Runbook: build and verify package 6 (Systems; mechanical once Integration names <HEAD>)

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

       [ -e playtest_slim_398b27ef ] && echo "playtest_slim_398b27ef exists: stop" || mv playtest_slim playtest_slim_398b27ef
       mv playtest_slim_<HEAD> playtest_slim && grep -o '"head": *"[^"]*"' playtest_slim/build_slim.json

   Play.bat inside the package starts its own exe (verify checks it). Integration copies / announces; never rebuild a package exe
   or touch build/release while the user plays.

## Gates (final head <HEAD>)

<!-- package-gate.sh table (rules final 08:37, Experimental 101b3970 / 78854ddd / d3a6e1e1). Base = 398b27ef + yaw 022e5e34 +
     drive-probe 56a9beca + the one-line steer + walkable f19e0052 + rise snap 63368d05 (all five sim lines; gate-base.sh). Regress bars are report rows at
     +0.35 ms p90 per map RT off (BISECT above, not a block); blocking = contamination, client rect, different match, final
     vs lights-off parity. GPU block ~2.8-3.1 h. -->

| gate | result | notes |
|---|---|---|
| regress-8map (3 arms x 8 maps, RT off, Classic, lockstep) vs the gate base | [PLACEHOLDER] | report rows +0.35 ms p90 per map |
| Off / Faithful identity (Rust, Classic): SIMHASH + frames | [PLACEHOLDER] | Faithful identity after Rendering's 7 commits |
| Realistic + HD Enhanced perf, 1440p 31 v 32, 5 maps (step 1b) | [PLACEHOLDER] | FLAG p50 > 16.7 ms; floor ~60 fps at 1440p |
| R4 leak rows (GPU dedicated over 5 map changes) | [PLACEHOLDER] | |
| Eye pass: same-frame Off / Realistic / Realistic+HD sheets, 5 user maps + authored views | [PLACEHOLDER] | running on int_eye = 7cd22027 (every row merged); int_cand3 sheet 1.31 / 1.30; int_cand4 authored views 1.25-1.88 |
| verify_slim -SubstDrive X: (maps, HD static incl. M6 rows + HD runtime at level 2, bin vs build_slim.json, overlay, Settings, RT gate incl. rtcheck.rtlights / rtcheck.defaults) | [PLACEHOLDER] | dry run 64771c71 + matfix: all rows PASS; build plan a5db23da clean (~27.5 GB) |
| FAST gate | [PLACEHOLDER] | |
| BOTTEST / NAVQ / SIMHASH (both AIs) | [PLACEHOLDER] | 48 / 48 incl. DRIVE rows; idle 20 s row at seeds 1 / 5; the "AI cost per step" row moves to average + p99 (wall-clock flake; Gameplay, pending) |
| Settings survive map change (WFC_SETTINGSTEST) | [PLACEHOLDER] | new renderer switches in release()'s keep list |
