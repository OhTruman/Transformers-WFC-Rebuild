# Rendering hand-over (2026-10-24)

Branch agents/rendering-rt2. Integration shas are the merges on Rebuild; "patch" shas are the rt2 commits that carried them.

## User playtest items (2026-10-23b) owned by Rendering

| item | what the user asked | status | sha |
|---|---|---|---|
| 1 / 6 | Menus at 72 FPS: lock the frontend to 60, the player's limit in a match | **Fixed.** Every menu measured at 60.0 FPS (FG rows ~53-54 real frames). A match or a running load uses the player's limit. | MENU_CAP2 944f8eb9, merged 74e41849 (take 1: QC_SETTINGS 9b75575e / 41a3bcac) |
| 22 | Chassis colours too subtle | **Fixed.** matc lifts ColorBrightness < 1 to 1.0 while chosen colours are set (a user-requested deviation from the base game). Applies at the next materials regen. | CUST_BRIGHTNESS 0f151a63, merged 4dd12194 |
| 35 / 9 | Brightness calibration logos don't respond; campaign brightness calibration | **Fixed.** The UI layer takes the display gamma (GfxRendererGL::setGammaExponent). The Frontend applies 2.2 / DisplayGamma to every movie, as the original does (RE). The bright logos in shot 09 are the phone's exposure: the image draws as authored. | GFX_GAMMA de8fc081, merged 24b150c8 |
| 33 | Load times: optimize or show real progress | **Rendering's share fixed.** core::loadProgress() counts the loader's stages, with per-map totals in wfc_loadprofile.ini, and the Frontend draws the bar (7f0efe32). The load time itself remains open: the data size is AssetTools' share. The dev HD pack makes a Streets load 256 s on a busy CPU. | LOAD_PROGRESS c21a1739, merged 4dd12194 |
| 5 | Preview panel images for the newer features | **Done.** 12 stills, 640x360, one fixed Streets camera (like-for-like): RayTracing_0/1/2, HDTextures_0/1/2, Upscaling_0..3, FrameGeneration_0/1, in data/frontend/previews. The panel is the Frontend's (35be2b30). | see the previews commit |

Other user reports fixed in the same pass:
- **Local pawn flicker while stuck:** the local pawn is never occlusion-culled. LOCAL_PAWN_OCC bed58e45, merged da9b3e48.
- **Missing subtitles font:** the original SubtitleFont is baked into the render data. SUBTITLE_FONT a309cbf9, merged fd58fde1.

## Settings audit
docs/handoff/QC_RENDER_SETTINGS_AUDIT.md (944f8eb9) covers 4 measured runs.
- **Every row applies:** the self-test passed 12 / 12 per arm, and every row logged its apply line.
- **Ray tracing and FG:** the RT scene was built in the Realistic match, and FG presented frames.
- **Fixed during the audit:** Texture Quality was a no-op; it is now a LOD bias of 2 / 1 / 0. VSync was ignored on the D3D12 path; it now follows the swap interval.

## Performance (docs/handoff/PERF_PASS_RENDERING.md, 2560x1440 uncapped)

| row | match fps | GPU p50 ms | CPU p50 ms |
|---|---|---|---|
| TDM Streets Off | 773 | 0.46 | 0.90 |
| TDM Faithful | 279 | 3.23 | 3.20 |
| TDM Realistic | 138 | 6.84 | 6.75 |
| TDM HD Faithful / Enhanced | 770 / 584 | 0.47 / 0.59 | 0.91 / 1.37 |
| TDM FSR 1 Quality | 582 | 0.56 | 1.37 |
| TDM FG Off / Realistic | 327 / 97 real frames (+ generated) | 0.98 / 9.54 | 1.41 / 1.50 |
| Escalation Remnant Off / Realistic | 120 / 77 | 1.30 / 8.16 | 4.22 / 8.00 |
| Campaign D1 Crash Off / Realistic, before | 12.7 / 12.4 | 3.92 / 43.05 | 48.1 / 49.2 |
| **Campaign D1 Crash Off / Realistic, D1_GPUSKIN** | **56.1 / 50.0** | 1.27 / 13.95 | **10.4 / 11.5** |

The Faithful / Realistic rows are the denoiser-on reruns. The first set ran without bin/D3D12, the Agility runtime the denoiser's SM 6.6 PSO needs; the release build ships D3D12/.

**D1 fixed (RENDERING_D1_GPUSKIN.patch).** The CPU profile put the 48 ms in World::cpDrawSkeletal: 244 matinee skeletal set-dressing actors CPU-skinned every frame. They now take the characters' GPU skin path. CPU 48 -> 10.4 ms, 12.9 -> 56 fps, visually identical (perf_pass_shots/d1_cpu_vs_gpuskin_p05.png).
- **Invalid first split:** WFC_SKIPMAT=TR_ only filters the static world list, so that test measured nothing. WFC_OLDCHARENV saved 3.4 ms (lights are not the cost).
- **Escalation's ~8 % of intervals over 16.7 ms** come from the sim, not the renderer (renderer CPU under 9.6 ms p99). This goes to Gameplay / Systems.

**FG eye check:** docs/handoff/perf_pass_shots/fg_gen_301_match.png + fg_gen_301_pawn.png: an FSR 3 generated frame ~36 s into a TDM match, the pawn walking. One pawn, no doubling or ghost edges.

## Denoiser fallback
RENDERING_RAW_SHADOW.patch handles a machine where the denoiser cannot load (no Agility runtime). Before it, Faithful composited the never-written denoised texture and drew 87-98 % black. With the patch, the raw sun visibility stands in, and soft shadows stay off.

## DLC Escalation mechanism visuals (RENDERING_ESC_MECH.patch)
The patch adds IRenderer::setMatineePosition(action, t), a sim-driven matinee.
- **Movers:** the matinee's movers take Gameplay's position instead of the map clock. Before, every non-looping matinee played itself out at map load: Pulse's doors opened ~2 s in while their collision stayed shut, and Tranquillity drained at load.
- **Toggle tracks:** the matinee's InterpTrackToggle keys switch their bound emitters.

game/WorldSurvivalMechPresent.cpp pushes Gameplay's SvMechState every frame:

| map | visual | how |
|---|---|---|
| District | laser grids | 9 emitters each, on while bought; the button matinee forward / reversed |
| District | sentries | the turret mesh (LightSentry -> WEP_SentryBlaster_SKEL, RepairSentry -> WEP_SentryRepair_SKEL). Activate / DeActivate clips, the sim's yaw; emitters on while awake |
| District | power outage | PostProcessVolume_4388 on / 3052 off while out; the screens' matinee position |
| Tranquillity | floodgates | waterfall / splash emitters off (toggle tracks) + the material matinee per bought gate |
| Tranquillity | drain | Interp_5361 (Energon surface, its pain volume, the height fog) at the sim's drainT; the destructibles unhidden at Drain Completed |
| Tranquillity | base doors | held at doorHold until OpenBase; pistons hidden at "Opened" |
| Pulse | 10 Zone_Managers | door / ramp matinees from the firing (managerT); shared side doors resolved (the played matinee wins) |
| Static | fire-jet trap | Interp_8584 while active, held at its end while cooling |

**Open (data):**
- **No matinee_tracks.json for Pulse and Static** (or Metropolis / Sector). Without it, Static's fire-jet emitters can't toggle; AssetTools runs dlc_matinee_tracks.py for them.
- **Pulse door matinees with no movers:** Interp_11165 / 11287 / 12613 / 4544 / 54 / 8917 have no movers.json entries, probably prefab-bound groups (Zone_Manager instances).
- **Skeletal actors never drawn:** the laser switch, Tranquillity's pistons and the sentry bodies are not in world.glb. AnimControl tracks need them exported first.
- **Material-parameter tracks not driven:** FloatMaterialParam tracks (the outage screens Interp_6320, the floodgate materials) need a material-instance parameter path.
- **Base-door "Wall" groups unbound:** they have no Move target in the data.

## Seen in passing (Frontend)
On head 62911b5c the brightness-calibration panel (6 grey bars + the two logos) is drawn over gameplay: it shows in a TDM match and
at D1's start, and stays up through the run (perf_pass_shots/fg_gen_301_match.png). It looks like the calibration screen is not
closed or is not gated to its menu.
