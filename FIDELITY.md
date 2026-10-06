# WFC Reconstruction — Fidelity Audit

**Principle:** the original Xbox 360 build is the specification. Recover what WFC actually
did; never let a provisional rebuild constant masquerade as original behaviour.

**Source-of-truth order:** (1) observed original behaviour · (2) `default.xex` code/constants ·
(3) authored cooked data (packages, `Coalesced_*` `.ini`) · (4) extracted metadata ·
(5) current rebuild · (6) guesswork.

**Evidence roots:**
- Cooked UE3 config (decompiled Coalesced): `ExtractedAssets/config/Coalesced_ini/TransGame/Config/Xenon/Cooked/*.ini`
- Per-sublevel map metadata: `ExtractedAssets/maps/*.json`
- Extracted asset metadata: `ExtractedAssets/VerticalSlice/**/{character,weapon,map}.json`
- Executable: `Game Dump/default.xex` (via Ghidra/ReVa) — for compiled class defaults.

Legend — CONFIDENCE: **CONF**(irmed from authored data/exe) · **HI** · **MED** · **LOW/GUESS**.

---

## PASS 9 — CHARACTER CUSTOMIZATION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Applier parameters | Cust_Color_A, Cust_COLOR_B, EnergonColor on robot, vehicle, arm, weapon | Default__TnCharacterApplier; native RE (user-supplied) | CONF | runtime uniforms in CHR + WEP MICs |
| Zero semantics | all-zero character colour skips the override; authored value stands | native RE | CONF | **APPLIED** (RGB all zero = skip) |
| Colour set | faction selects the set; no separate team override in this path | native RE | CONF | caller passes the faction's set |
| Optimus data | PrimaryColors/SecondaryColors (0,0,0,255) per faction, EnergonColor (0,0,0,1) | OptimusPrime_PCD_SP / Leader_PCD_MP | CONF | all skipped → MIC values (Cust_A/B per MIC, EnergonColor (1.25,0.05,0.05) compiled Autobot branch) |
| Parameter reads | robot+vehicle MICs read all three; WEP_IonBlaster_MATINST reads Cust_Color_A + EnergonColor; InteriorAlt reads none | compiled MIC uniform expressions | CONF | matches |
| Arm mesh | CP_OptimusArm_SKEL used when no weapon is drawn (early vehicle→robot transform, holster, melee); slots Cust_Mat_INST_B + InteriorAlt | native RE; materials_authored.json | CONF | renders with original MICs (gameplay drives visibility) |
| MP robot mesh | RB_OptimusWeaponArm_SKEL (forearm removed; slot order differs from the slice's campaign RB_Optimus_A_SKELMESH) | materials_authored.json | CONF | renderer supports it; mesh choice is gameplay/asset ownership |

## PASS 8 — VEHICLE MATERIAL + MAP COMPOSITION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Vehicle normal map | `RB_OptimusPrime_Cust2_Mat_INST` `UseReconstructedNormal`=True: X=A, Y=G of DXT5 `VH_Optimus_NORM`, both `*2-1`, Z=sqrt(1-x²-y²) | MIC static params; BASE ShaderCache character PS (`tfetch .yw; mad r.xy, r.zy, 2.0, -1.0`) | CONF | **FIXED** (alpha was left in [0,1]) |
| Vehicle diffuse/customization | CHR master with Cust_Color_A/B, ColorBrightness 0.6, etc. | MIC params | CONF | verified = AssetTools bake |
| EnergonColor (robot+vehicle) | 3 same-named VectorParameters; compiled permutation binds (1.25,0.05,0.05) | MIC compiled uniform expressions | CONF | as compiled (team override at runtime not modelled) |
| Persistent level | `MP_IAC_Streets_Base_m` streams ART + AUDIO only | TransLevels.ini MapFilename; LevelStreamingKismet | CONF | composition complete |
| Post-process | TnWorldInfo: Bloom_Scale 0.1, bEnableDOF, DOF_MaxFarBlurAmount 0.6, FocusFarFalloff 40000, ColorCorrectionTexture MP_Streets_CLUT | BASE TnWorldInfo over Default__WorldInfo | CONF | **APPLIED** (replaces PASS 7 defaults) |
| CLUT | 32³ A8R8G8B8 Texture3D, SRGB=False (Default__Texture3D), applied after gamma: tex3D(c*(N-1)/N+0.5/N) | Xbox 3D tiling (xenia Tiled3D); uber+CLUT PS | CONF | **APPLIED**; strong authored desaturation |
| DOF | gather a' from avg depth; resolve ((1-a)scene+blur.rgb)/((1-a)+blur.a); PackedParameters (Focus, 1/NearFalloff, Exponent, 1/FarFalloff) | decoded PS | CONF shader / MED parameter packing | **APPLIED** |
| Static decals | 25 DecalComponents with cooked receiver geometry (28-byte verts, u16 indices); BSP receivers clipped, static meshes keep whole triangles; UV = 0.5 - M·(P-L) + offset, rows HitTangent·TileX/Width, HitBinormal·TileY/Height | DecalComponent native; decal VS | CONF geometry+UV / MED box clip of static-mesh receivers | **APPLIED** |
| Section materials | umodel glTF section names → original MIC paths (15); 6 null (`dummy_material_N`) | StaticMesh sections | HI | **APPLIED** |
| Prefabs | 16 PrefabInstances → 34 placed StaticMeshActors (already composed) + 1 SteamVent emitter | ArchetypeToInstanceMap | CONF | complete |
| Emitters | 8 `FX_Level_Generic_p.FX.Steam_Sm_FX` + prefab steam vent | ART actors | CONF | **NOT RENDERED** (particle FX) |
| Power-tube "flat planes" | PowerTubeCircle emissive panels (mask R, lavender ×(1+pulse)) | MIC/mask | CONF | authored; renders as designed |

## PASS 7 — ORIGINAL RENDER PATH (2026-10-01, branch agents/rendering)
Specification sources: the ORIGINAL compiled Xenon shader microcode in the cooked `ShaderCache`
exports (disassembled with `tools/render/xenos_dis.py`, bit layouts per xenia's public `ucode.h`),
the cooked material expression graphs, and cooked lighting objects. No values tuned by eye.

| Stage | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Base-pass lighting policy | `FDirectionalTextureLightMapPolicy` (also Vertex/Interpolated variants) | ShaderCache type names, ART pkg | CONF | **APPLIED** |
| Directional lightmap decode | `L = Σ_i dot(N_t,B_i)² · tex_i.rgb · ScaleVector_i`; B0=(0,√(2/3),1/√3) B1=(−1/√2,−1/√6,1/√3) B2=(1/√2,−1/√6,1/√3); no-normal materials use ×1/3 (=(1/√3)²) | base-pass PS microcode + literal pools | CONF | **APPLIED** (3 coefficients) |
| Lightmap colour space | `LightMapTexture2D` inherits `SRGB=True` (Default__Texture); no instance override | Engine.xxx CDOs + texture tags | CONF | **APPLIED** (GL sRGB) |
| Lightmap resolution | atlases cooked at 256² (Xenon `MaxLODSize=512`, top mips absent in dump) | `_LM` pkg sizes, Xe-TransEngine.ini | CONF | as cooked |
| Base-pass output | `Emissive + Diffuse·L·ShadowMask`, ×SceneColorBiasFactor; **no lightmap specular** | base-pass PS | CONF | **APPLIED** (ShadowMask=1) |
| What is baked | Beast bake (`FBeastAreaLightPolicy`); dominant red directional (Brightness 6) LightmapGuid present in 1932 components; no ShadowMap2D textures | component native LightGuids | CONF | data used as-is |
| BSP lightmaps | per ModelComponent element `FLightMap2D`; per-vertex ShadowTexCoord in cooked `FModelVertexBuffer` (36 B verts, indexed by `FBspNode.iVertexIndex`, 889/889 nodes verified) | Model + ModelComponent native layout (decoded) | CONF | **APPLIED** (`bsp.glb`) |
| Materials | UE3 graphs + WFC MaterialFunctions (ENV_*_MF, CHR_*), static switches & TextureSets (Color_NormX/Masks_NormY, alpha UnpackMin −1) per MIC; WFC HLSL ShaderCode | cooked Material/MIC/TextureSet exports (Streets BASE+ART) | CONF | **APPLIED** 168/169 (offline GLSL translation) |
| Old extraction error | `materials.json` mapped decal `Rust_C_CLR` as base colour; real diffuse lived in TextureSets | MIC TextureSetParameterValues | CONF | **FIXED** |
| Cubemaps / flipbooks | cooked Xbox-tiled DXT TextureCube (6 faces) / TextureFlipBook | native tails (decoded) | CONF | **APPLIED** (`xbox_texture.py`) |
| Dynamic-object lighting | WFC UberLight: `Diff·AmbientCube(N)` + per light `(Diff·sat(N·L·0.6778+0.3333)² + Spec·pow(sat(R·L),SpecPower))·Color·sat(1−(d/R)²)^Falloff·spot²·S` | uber-light PS microcode | CONF | **APPLIED** |
| Light environment | WFC `LightEnvironmentComponent`; TnRobotForm/TnVehicleForm TotalLightCount **2**, UpdateDistanceThreshold **30 UU**; dynamic-only SkyLight (Brightness 0.1, LowerBrightness 0) + dynamic fill directional | TransGame.xxx CDOs, ART lights | CONF | **APPLIED**; composition of non-direct lights into the ambient cube = MED |
| Light visibility | `LightsVisibilitiesVolume` (745 KB precomputed per-cell light visibility, leaf records `(lightIdx u16, vis u16)`) | ART native | PARTIAL | **NOT USED**; collision raycasts instead (PROV) |
| Height fog | UE3 4-layer vertex fog: `exp2(FogDistanceScale·max(d−Start,0)·layerFrac)`, cut at ExtinctionDistance; authored Height −57273.8, Density 2e-5, Start 2048, LightColor (234,91,116) × **LightBrightness 0.1** (class default) | fog VS microcode + HeightFogComponent | CONF shader / MED constants (`−Density/ln2`) | **APPLIED** (replaces softened GL_EXP pink fog) |
| Post (superseded by PASS 8: map TnWorldInfo overrides) | bloom gather (4 taps clamp [0,4], tap kept if any channel > Threshold, ×0.25·Scale) + blur; UberPostProcess tone; no PPVolumes, persistent level stub ⇒ WorldInfo defaults (Bloom 1/1, Shadows 0, HighLights 1, MidTones 1, Desat 0, identity CLUT); DisplayGamma 2.2 | DOFAndBloomGather / UberPostProcess PS microcode, Default__WorldInfo, Xe-TransEngine.ini | CONF (blur kernel PROV) | **APPLIED** |
| Character materials | `CHR_Transformer_NormSpec_Cust_E_Mat` with MIC customization, spec-power S-curve, energon pulse, metal cubemap | Streets BASE | CONF | **APPLIED** (replaces provisional GL_LIGHT0) |

**Remaining gaps:** LightsVisibilitiesVolume decode; 24 `FLightMap1D` vertex-lightmapped props;
dynamic shadow mask (characters do not cast/receive dynamic shadows); DirectLightAmbientContribution;
`ENV_ORB_D1_Debris_p SideSupport_MATINST` (master not in Streets packages); `MotionBlurEffect` in
DefaultScenePostProcess not implemented; light-env transition blending (0.5 s) not implemented.

---

## PASS 24 — human playtest fidelity II (2026-10-05)

### Fast-turn stutter: body orientation snapping between 60 Hz steps [measured; HIGH CONFIDENCE cause]
- Playtest: fast left/right camera or steering motion looks stuttery, mostly in vehicle form, also on foot.
- Measured (WFC_HEADJIT: drawn body heading vs camera yaw per render frame, 6 rad/s flicks / full-lock boost steer):
  - 60 Hz render: ≤ 0.05°/frame.
  - 144 Hz: 4.5–4.7°/frame (max 7°).
  - 240 Hz: 2.8–4.6° (max 9°).
  The camera turned every render frame while the body yaw advanced only on simulation steps. The original ticks physics and camera
  in the same variable-length frame, so the body never lags its view.
- Not renderer pacing, not camera position (CAMSYNC unchanged), not input quantisation.
- Fix (presentation only; the simulation is untouched): a draw yaw added to the body mesh and its attachments between steps.
  - View-slaved headings (robot, car / truck hover, tank) add the view yaw change since the last step.
  - Physics-steered headings (boost Driving, jet servo TurnRate 0.5) extrapolate the last step's yaw rate.
  - The boost camera follows the drawn heading.
- After (mean °/frame at 144 / 240 Hz):

  | scenario | 144 Hz | 240 Hz |
  |---|---|---|
  | robot | 0.000 | 0.000 |
  | car hover | 0.010 | 0.004 |
  | truck hover | 0.012 | 0.012 |
  | boost | 0.35 | 0.07 |
  | jet | 0.70 | 0.19 |

  The jet residual also exists at 60 Hz (0.37°): the authentic TurnRate 0.5 servo lagging at flick reversals.

### Transform mesh handoff per chassis [CONFIRMED ORIGINAL: TnAnimNotify_ToggleHidden in each chassis' transform clips]
- Playtest: a short malformed / box-like stage in a Decepticon Scout robot→vehicle transform.
- Cause: every chassis used the Optimus ToggleHidden times (robot hide 0.880, vehicle show 0.396, robot show 0.098, vehicle
  hide 0.663). Barricade (Car4) authors 0.849 / 0.705 / 0.394 / 0.666, so its vehicle mesh appeared 0.31 s early, half-unfolded.
  Sideswipe (0.414 / 0.351 / 0.000 / 0.279), Starscream (0.789 / 0.694 / 0.336 / 0.411) and the tanks differ too.
- Now read per chassis from character.json (Option absent = Toggle_Unhide, Toggle_Hide explicit).
- WFC_XFORMVIS 16/16 (Car2, Car4, Truck, Truck4, Jet, Jet4, Tank3, Tank2; both directions): the target mesh appears and the
  source hides at the authored time (within a step), and no step draws neither mesh. Clip, pose, root, momentum and weapon
  restore (25%) are unchanged.
- The Scout transform has no authored particles (AssetTools: only Starscream authors Trails FX): a mesh swap plus sound, as
  now drawn.

### Tank 180 quick turn [CONFIRMED ORIGINAL: RE TARGETED_PASS5 §1]
- Playtest: the tank special move can cause a rapid 360° manoeuvre.
- Original: VehicleSpecialMove (Shift / RB) on press → CanUseSpecialMove (TimeBetween180s 1.2 s) → RecenterCamera.
  TnQuickTurnCameraBehavior lerps the camera yaw linearly to tank yaw + 180° over 0.3 s; the hull follows the camera through
  TnHoverTankSimulation.UpdateTurn, so it spins 180° in 0.3 s. Not 360°, no repeat while held.
- The rebuild had a PROVISIONAL instant +180° jump of the view yaw. An exact ±π step is ambiguous to the yaw smoothing and the
  hull's remainder() follow, a plausible source of the long-way spin. It is replaced by the original 0.3 s linear camera behaviour
  (+ a quickTurnSerial for the Systems "Tank 180" sound).
- The Soldier preset's Shift ability in ROBOT form is Whirlwind: a 5.9 s spinning melee attack, authentic, which may also be what
  was seen.
- VEHPHYS: 180° reached in 0.28 s; holding Shift 3 s = one turn; a press within 1.2 s is refused.

### Fine aim per weapon [CONFIRMED ORIGINAL: RE pass 5 §3, AssetTools weapon.json fine_aim_camera (OverTheShoulder FOVsByPCS)]
- PC right mouse = ToggleFineAim (toggle); pad LT hold. Ground speed ×0.5; blocked while meleeing / reloading / dodging (as before).
- Camera rows by the held weapon's WeaponPCS (previously every weapon used the generic row):

  | weapon | FOV | orbit | screen X | look yaw / pitch |
  |---|---|---|---|---|
  | Null Ray (SniperRifle) | 20 | 100 | 350 | 6.5 / 3.25 (×0.13) |
  | HeavyPistol / BurstRifle | 30 | 100 | 350 | 9.375 / 4.6875 (×0.1875) |
  | other | 45 | 800 | −50 | 25 / 12.5 (×0.5) |

- One zoom stage only ("10x" is marketing text). Look speed blends over SpeedTransitionTime 0.5 s.
- Magma Frag Launcher: fine aim remote-detonates instead of aiming. Here fine aim is refused; the launcher's grenades explode on
  contact, so there is nothing to detonate [PARTIAL].
- PARTIAL: scope sway wiggle (0.45° at 3 / 10 / 6 Hz), fine-aim orbit-distance smoothing (0.1 s assumed), HUD scope symbol
  (Frontend: showScope long / short / medium).
- WFC_FINEAIMTEST 3/3: Null Ray FOV 20 / look 0.130, HeavyPistol 30 / 0.187, IonBlaster 45 / 0.500, speed ×0.50, toggle off → 80.

### Energon Repair Ray [CONFIRMED ORIGINAL: TnWeaponRepair / TnWeaponBeam script, RepairBeam_WEPDATA, AssetTools 8297bdd]
- Before: the Repair Ray was not simulated (no beam, no heal). Now it is a beam that ticks every FireInterval (0.1 s) along the aim
  over WeaponRange 3500 UU:
  - TnWeaponRepair.ProcessBeamHit: a teammate is healed HealthPerSecond 60 × RepairRateModifier (no buffs: ×1) × Δt, TnHealTypeRepairTeam;
  - otherwise TnWeaponBeam.ProcessBeamHit: the hit actor takes DamagePerSecond 60 × Δt, TnDamageTypeRepairEnemy;
  - ammo 10 / s (clip 100), HeatProperties.HeatMax 0 → no overheat in practice [HIGH].
- HUD state: repairBeam / repairBeamHealing / start / end / target per frame, for the Beam2 ribbon (Rendering) and the
  WP event 1 (heal loop) / event 2 (damage loop) sounds (Systems).
- PARTIAL: PlayerTargeting.GetRepairTarget lock-on (the beam end is pulled to a picked teammate's TargetableLocation) is not
  recovered; the beam follows the crosshair. Heal type segments: healed across segments [HIGH].
- WFC_PARTICIPANTTEST: teammate +60 HP/s from the beam (+ the pawn's own regen when idle), enemy −54 / s, 9 ammo / s, HUD flag.

### Match start countdown [CONFIRMED ORIGINAL: RE pass 5, TnMultiplayerGame]
- Already present: PendingMatch, GRI.ResetCountdown(true, 10) → MatchAutoStartCountdown 10 s, a CountdownTick event each second
  (PreGameCountdown <CurrentGame:CurrentCountdown>), no pawns until InProgress; then everyone spawns and the announcer plays.
- Not restored here: GameCountdownPostProcess (active 6 s, ramp-out 3 s) is a rendering / frontend presentation effect.

### Decepticon Scout (Barricade) transform look [authored data; no fallback frame]
- The per-chassis ToggleHidden times are the authored Car4 notifies: robot hides at 0.849 s, vehicle shows at 0.705 s (R→V);
  robot shows at 0.394 s, vehicle hides at 0.666 s (V→R). Both meshes are drawn in the overlap, as authored.
- The graybox fallback cannot appear mid-transform: the form swap re-samples and re-skins the new mesh in the same tick
  (Character::updateAnimation), and the partner mesh is skinned on the first tick that it is visible. XFORMVIS 16/16.
- So the "box-like" intermediate is the authored fold of Barricade's clips, not a missing pose [HIGH; visual confirmation pending].

### Jet handling values [CONFIRMED ORIGINAL authored blueprints]
- Hover: HoverPlane_Physics accel 2500, max 1500, gravity cancelled (TnHoverPlaneSimulation always hovers: the "floaty" feel),
  Ascend / Descend = Dash ±Z at 1000. Flying: Plane_Physics MaxSpeed 4000, accel 3000, drag 600, return to hover above crash
  speed 3000. All read from character.json; they match RE pass 5 §4. The remaining jet heading jitter (0.4° / frame at 60 Hz)
  is the TurnRate (0.1, 0.5, 0.5) servo itself.

### Scout height [re-confirmed]
- WFC_HEIGHTTEST Car2 / Car4: capsule 1.550 constant, root 0, scale 1, mesh origin 0; idle hips 1.51-1.54 m → jog 1.94-2.32 m.
  This is the authored AnimSet posing; nothing changes the capsule, root or scale. Authentic, unchanged.

### DEV / QA TOOLING [NOT ORIGINAL — never part of a fidelity claim]
- World::qa* API, all no-ops unless the process starts with WFC_QA=1. It is driven by Frontend's separate Win32 QA window (F10).
- qaWeaponIds(vehicle), qaSetLoadout(ids) through the real applyLoadout (restrictions apply, refused ids returned), qaRespawn
  (suicide, no score → the normal respawn wave), qaTeleportToStart(i), qaSetNoclip, qaSetGodMode, qaStatus.
- Map / mode / class / lobby: Frontend drives the real lobby flow.
- WFC_QATEST 7/7 with the gate; without it, every call is refused.

### Projectile visuals [CONFIRMED ORIGINAL bindings: AssetTools weapon.json projectile_visual, RE projectile_effect_bindings]
- Was: every projectile drew as an orange box marker.
- Now each projectile carries its weapon's authored visual (projectiles[0].projectile_visual), resolved by class id, else provider
  folder, and accepted only when the file's class matches. Lifecycle:
  - spawn: spawnParticleEffect(FlightEffect, pos, forward = velocity, up);
  - each tick: setParticleEffectTransform;
  - impact / fuse: stopParticleEffect (trails finish), then the ExplosionEffect at the hit location, oriented by the hit normal
    (EmitterPool.SpawnEmitter(ExplosionEffect, HitLocation, rotator(HitNormal)));
  - LifeSpan expiry: no explosion.
- FlightEffect is the body (no static mesh) for all but the thrown grenades. Flak / Flashbang / Heal also draw their class-default
  WEP_Grenade_*_STAT mesh.
- Renderer API from agents/rendering (38c9ecf+), detected at compile time: on a tree without it, the FX calls compile out and the
  box marker stays as a non-original fallback. It also stays when a template is missing from the map's FX data.
- PARTIAL: PlasmaCannon uses Charge1 visuals (charge levels not simulated); grenade mesh orientation follows the velocity yaw
  (spin not recovered); the fuse explosion normal is assumed up.
- WFC_PROJFXTEST 2/2: 11/11 projectile weapons bind a FlightEffect (15 weapons with visuals incl. grenades, 3 body meshes), all
  projectiles end within 12 s. A standalone check (work/pass23/fxcheck) confirms the detection calls spawn / move / stop with
  Rendering's exact signatures.

### Vehicle weapon origin and muzzle alternation [CONFIRMED ORIGINAL: socket data + RE pass 5 §9g script]
- WFC_VSOCKET: WeaponSocket_Primary sits on each chassis' left gun bone (L_GunRobo01_XT) or the tank cannon (C_Cannon_XB),
  inside the vehicle hull; Starscream's is under the wing, 0.8 m below the physics box.
- Correction: firing only from the left was NOT original. It came from using WeaponSocket_Primary alone.
- Original: the vehicle weapon mesh's MuzzleFlashSockets = [Primary, Primary2] (Primary2 on R_GunRobo01_XT) for
  AssaultRifleVehicle, AssaultRiflePlane, RocketVehicle, RocketPlane and HomingRocketVehicle. HmWeapon.OnPlayFireEffects plays
  the flash at CurrentSocket, then HmWeaponMesh.ChangeSocket advances it ((i + 1) % N) once per shot.
  - Projectiles spawn at the shot's socket (Weapon.ProjectileFire RealStartLoc = GetMuzzleLoc() before the advance) and aim at
    the camera-trace hit point.
  - Instant-hit MG: the damage trace starts at the pawn's start-trace location (here actor + BaseEyeHeight: HIGH; vehicle eye
    height PROV); only the flash / tracer alternate.
- Chassis without Primary2 (the tanks) and weapons not in the list stay on Primary.
- HudState vehicleShotSerial / vehicleShotSocket / vehicleShotMuzzle let the flash / tracer glue follow the socket.
- WFC_MUZZLETEST 5/5. Sockets 0,1,0,1… with the muzzle alternating sides: Car2 ±0.5 m, Car4 ±0.71, Jet4 ±1.4, Truck3 ±1.21;
  Tank3 on Primary only.
- The integrated muzzle / tracer effects pick templates from the held ROBOT weapon class (Systems' weaponFx(weaponClass_)), not
  the vehicle weapon: reported to Systems / Integration.

## PASS 23 — human playtest fidelity (2026-10-05)

### Fresh match state [CONFIRMED ORIGINAL: RE pass 4 - PRI / Team Score 0, OldPRI.Reset, TnTeamInfo zeroed on seamless travel]
- WFC_SCORETEST 9/9, three consecutive TDM launches with kills / deaths / damage / a timed-out match in between. At launch and
  after the countdown every value is fresh: team 0-0, personal score / kills / deaths 0, all scoreboard rows 0, clock = TimeLimit,
  full clip, InProgress after PendingMatch. The Frontend route uses the same World::launchMatch reset.
- The visible "wrong score" in the playtest was the Hud_GFX GoalScore default (10, read once at load) filling the TDM bars before the
  match's PointsToWin arrived - fixed on the Frontend side (agents/frontend), not Gameplay state.
- GRI values for Hud_GFX: attackingTeamIndex (CTF / EXT, else -1) and currentObjectiveCountdown (EXT fuse, else -1) [CONF CDO
  defaults], competitiveScoreEnabled 0 [HIGH: not authored]. DOM / KOTH objective countdown [PARTIAL].

### Vehicle handling [RE TARGETED_PASS4 §A CONFIRMED ORIGINAL; measured with WFC_VEHPHYS]
Human playtest: jumps too high in some situations, violent wall bounces, teetering / rolling about an odd axis, not settling.
Each RE item was compared with the code and measured (Streets, flat run-up into a vertical wall; Sideswipe car, Optimus truck,
Warpath tank).

Defects found and fixed:
1. **UpdateTurn semantics** (cause of the teetering / rolling / not settling).
   - Original: each step the angular velocity is REPLACED by axisAngle(current → upright with view yaw) × mask / dt.
   - mask = (0.05, 0.05, 1) normally; (1, 1, 1) when ShouldUpright (no suspension contacts, or up.Z < 0.01).
   - The rebuild applied nothing on the ground (spring and contact torque accumulated step after step) and only 5% per tick
     in the air (the jump nose-up spin kept turning).
   - Now the original replacement for car / truck. The tank keeps its own rule: no pitch / roll correction while stable on
     the ground, else TurnRate 0.05 (RE C2).
   - Glancing (22°) wall hit, Sideswipe hovering: max tilt 69° → 6.3°, pitch / roll rate 218 → 9.7 °/s. Truck 1.8°.
2. **Boost (Driving) jump never fired.**
   - A provisional overhead-hull guard started its ray at the COM height. That is floor level while driving on the wheels,
     so it read the floor as a ceiling and zeroed v.y in the jump's own step.
   - The probe now starts ≥ 0.1 m above the root.
   - Boost jump apex: car 4.93 m, truck 4.91 m vs RE local (600, 0, 1400) → 1400² / (2 × 1940.4) = 5.05 UU-m.

Verified unchanged (already as the original):
- **Hover jump:** additive world Δv 1200 UU/s, RB gravity −2940 × 0.66, fresh press only, 0.3 s cooldown counted on the
  ground, IsOnTheGround = contacts > 0 and average normal Z > 0.707.
  - Apex 3.81–3.83 m (RE 3.71 + spring). A jump pressed right at landing peaks at 1.8–2.6 m. Holding Jump = one jump.
- **Suspension:** 4 diagonal probes, implicit spring with m / 4, push-only, no force without a hit.
- **Walls:** head-on rebound 0.00 m/s for car, truck and tank. Physmat restitution 0.05 and no script bounce: the rebuild
  removes the into-wall velocity (restitution 0) [HIGH: 0.05 vs 0, PhysX combine untraced].
- **Frontal boost crash (> 0.866 into the wall) drops Driving → Hovering.** Holding Boost re-enters Driving after the drift
  window (A6).

Remaining:
- **Tank glancing wall:** a diagonal probe losing the floor at a wall base tilts the tank up to its stability limit (~30°)
  before TurnRate 0.05 engages. That is the RE tank rule, but the PhysX hull-to-wall contact that might also support it is not
  modelled [PARTIAL].
- **Ramps / terrain:** the probes and springs follow the authored model, but ramp launches were not measured separately
  against a capture [PARTIAL].
- WFC_VEHPHYS 26/26: settle, jump, re-jump, boost jump, held jump, and walls (hover / boost, head-on / 22°) × 3 vehicles.

### Scout body height idle vs locomotion [HIGH CONFIDENCE authentic: RE pass 4 + WFC_HEIGHTTEST measurement]
- Playtest: the Scout looks crouched at rest and much taller while running.
- Measured per tick (WFC_HEIGHTTEST, heights above the feet): capsule centre, mesh origin and root bone (C_Root_Reference_XR) never
  move. Root and hip scale stay 1.000. Only the pose changes:

  | body | idle hips / head (m) | jog hips / head (m) |
  |---|---|---|
  | Car2 Sideswipe | 1.531–1.540 / 2.42 | 1.941–2.322 / 3.51 mean |
  | Car4 Barricade | 1.490–1.520 / 2.63 | 1.941–2.321 / 3.51 mean |
  | Truck Optimus | 1.971–1.988 / 3.37 | 1.865–2.180 / 3.46 mean |

- Cause (RE pass 4): Car2 / Car4 own AnimSets hold only idles and transforms. Jog / walk / sprint come from Shared_ROBO_ANIM, authored
  on Starscream, applied with bAnimRotationOnly = False (translations as authored). The original Scout's hips are about 154 UU idle
  and 194–232 UU jog, the same as measured here. Root bone Z = 0 in every clip, so no root motion.
- Not root translation, scaling, capsule coupling, retarget error or a pivot mismatch. Left as is. A shipped capture would move this
  to CONFIRMED.

### Input details [CONFIRMED ORIGINAL: shipped PC bindings]
- Melee is Q or the middle mouse button.
- A Fire click shorter than one simulation tick (144 / 240 Hz frames) is latched for the next step (one shot attempt); a release
  before the refire still clears it, as StopFire clears PendingFire.
- PlayerController::hudAimState().weaponClass = the held class (TnWeapon<id>, or TnWeaponFlag1Hand / TnWeaponBomb while carrying);
  it was hard-coded TnWeaponIonBlaster (Integration report).

### Weapon switching [CONFIRMED ORIGINAL: HmInventoryManager / HmWeapon / Engine.Weapon script; Xe-TransInput.ini; RE pass 4]
- Playtest symptom: the selected primary appeared, but the player could not switch to the secondary.
- Chain checked:
  - profile / class loadout → Frontend selection (PCD_MP WeaponTypes; slots 0–1 customisable) → applyLoadout;
  - no refusals in the playtest log; both guns are in the inventory (WFC_SWITCHTEST, four classes).
- Defects fixed:
  1. The mouse wheel did nothing. The shipped binding is NextWeapon on wheel up, wheel down, PageUp and PageDown.
     There is no PrevWeapon, so every input cycles forward.
  2. Switching was refused while reloading or while a switch was in progress. Original HmWeapon.TryPutDown:
     - Active: put down now;
     - WeaponReloading: put down now, the reload is abandoned (no RefillClip);
     - WeaponFiring: put down now if MinReloadPct 0.5 of the refire interval has passed, else at the next RefireCheckTimer;
     - WeaponPuttingDown: retarget the pending weapon;
     - WeaponEquipping: put down again once equipped.
- Switching stays blocked while transforming, in vehicle form, during a melee attack, and with DisallowWeaponSwitching
  (Poke). Heavy weapons are dropped when switched away.
- Test WFC_SWITCHTEST 32/32: Scout (Car2), Scientist (Jet4), Soldier (Tank3) and Leader (Truck3), each:
  - wheel / PgUp / PgDn ×4 idle;
  - while moving, jumping (airborne), firing and reloading (reload abandoned, clip unchanged);
  - transform to vehicle and back (active weapon kept), then switch again;
  - HUD weaponId = the active weapon.

## PASS 22 — SELECTED CHARACTERS, CLASSES, VEHICLE FORMS, WEAPONS, MULTI-MAP (2026-10-05, gameplay agent)
Inputs:
- AssetTools per-chassis export `VerticalSlice/Characters/<ChassisId>` (vs_roster_export) and `roster_package.json`;
- RE TARGETED_PASS3 §A (selection → pawn), §C (car / tank / jet simulations, script bytecode), §C6 (FindSpot);
- RE confirmations relayed this pass: specialty health, versus weapon data;
- decompiled TransGame script; authored.db (read-only).

### Selected character → pawn — CONFIRMED chain, no substitute body
- **Chain:** CharacterSelection (Frontend fills it from GameFlow::SelectedCharacter) → team faction (FFA = 1) →
  `resolveChassis` (CharacterData.ChassisTypes[faction], else the class preset) → `Match` chassis check →
  `World::applyChassisToLocalPawn` (TnPawn.ApplyTransformer) → ApplySpecialty → ApplyWeapons.
- **Chassis definition** (`ChassisDef`), read from `character.json` + roster collision:
  - robot / vehicle glb with skeleton and clips;
  - ArmBlueprint mesh + anims;
  - WeaponSocket_Primary / _Secondary and the vehicle weapon socket, via UE → glTF socket math (verified against the
    recovered Optimus matrix);
  - ROBODEF speeds, accel, air control, terminal velocity;
  - acrobatics JumpHeight;
  - momentum blueprint;
  - hover / car / suspension / wheel blueprints.
- **WFC_CHASSISTEST 13 / 13:** all 27 multiplayer chassis load. "Truck" reproduces every hand-entered Optimus
  constant. An unknown id fails.
- **No fallback** (the original has none either: FindChassis failing gives a body-less pawn + log):
  - an unavailable body refuses the spawn, retries every second, and sets `spawnError` (HUD) with a loud log;
  - `drawnChassis` is the spawned body;
  - `chassisFallback` is removed.
- **Visually verified** (screenshots): Sideswipe, Starscream, Warpath and Soundwave skinned, holding their own weapons.
- The direct boot and the harnesses use `Characters/Truck` (RB_OptimusWeaponArm_SKEL, the authored MP mesh).
  `WFC_CHASSIS=<id>` boots or selects any chassis.

### Class behaviour — CONFIRMED (script + authored; RE agrees)
- ApplyCharacter → TnPlayerPawn.ApplySpecialty → TnSpecialty.Apply, when the game's ApplySpecialtyBuffs is true
  (Default__TnGame true; only campaign / survival / campaign-lobby set it false):
  - **SetSpeedMultiplier(SpeedMultiplier):** a per-source factor (TnPawn.UpdateSpeeds multiplies them; fine aim stacks);
  - **InitializeSegmentedHealth(Health_<Class>):** replaces SharedHealth.

| class | segments | HealthMax | overshield | speed × |
|---|---|---|---|---|
| Leader | 5 × 60 | 300 | 200 | 0.95 |
| Scientist | 3 × 60 | 180 | 200 | 0.90 |
| Scout | 4 × 50 | 200 | 200 | 1.00 |
| Soldier | 6 × 55 | 330 | 200 | 0.90 |

- **Correction:** versus does **not** use SharedHealth 550 (Passes 19–21 did). The custom specialty comes from the slot;
  iconic characters use the chassis DefaultSpecialty [HIGH].
- **Iconic specialty:** the iconic preset's CharacterData.Specialty (CONFIRMED RE via AssetTools), not the chassis
  DefaultSpecialty (UI grouping only). Sideswipe = Soldier, Starscream = Soldier, Soundwave = Scientist, Optimus = Leader.

### Abilities — framework CONFIRMED script; Dodge implemented, the rest PARTIAL
- TnAbilityManager:
  - CharacterData.Abilities[0] on Ability0 (Shift), [1] on Ability1 (Ctrl);
  - SpamPreventionTime 1.0;
  - versus skill-data index 0 (TnMultiplayerGame.GetSkillDataIndex) → Cooldown[0], and no resource cost;
  - the cooldown starts at CanStartCooldown;
  - PlayerWalking.CanUseAbilities refuses while reloading or dodging.
- **Dodge** (TnAcrobaticsManager.Dodging):
  - direction from TnPlayerInput.Dodge: |up| ≥ |right| → forward / back, else right / left;
  - PHYS_Flying at Acrobatics DodgeSpeed 3000 for DodgeTime 0.5;
  - EndState clamps to MaxAir / MaxGroundSpeed; a wall hit ends it early;
  - CanDodge requires landing since the last dodge; cooldown 2.0 s.
  - Test: Sideswipe dodges right at 30 m/s, 10.9 m in 0.6 s, refused while cooling.
- **Warcry** (TnAbilityWarcry, CONF):
  - friendlies within AoeRange 3000 UU (FFA: the owner) get TnBuffWarcryIncreaseDamage ×1.1 / 1.2 / 1.3 and
    DecreaseDamageTaken ×0.5 / 0.4 / 0.3;
  - level = Clamp(friendlies − 1, 0, 1), +1 with the ImprovedWarcry skill (skills not applied);
  - BuffTime[0] 15 s; Cooldown[0] 60 s, started after the owner's buff ends (HadAndLostBuffCondition).
  - Test: Optimus with one friendly in range took 40 of 100; cooldown 59.5 s right after the buff.
- **Shockwave** (TnAbilityShockwave, CONF):
  - after Delay 0.25 s, Blueprint[0] Damage 65 within 2500 UU (bDoFullDamage) from the PositionSocket; Cooldown 60 s;
  - the owner is not hurt [HIGH]; the 700000 momentum knock-back is not applied [PARTIAL].
  - Test: 65 damage to an enemy 10 m away, nothing before the delay.
- **Cloaking:**
  - TnBuffCloak BuffTime[0] 20 s;
  - ExposeSelf removes it on firing (TnWeapon.OnPreServerFire) and on damage taken (TnPlayerPawn.TakeDamage);
  - the TDM name-tag label is hidden (TnObjectiveMarkerTypeTransformerVersus DisableLabel);
  - cooldown 15 s after it ends;
  - HUD `cloaked`; the cloak shader belongs to Rendering [CONF script].
- **Whirlwind** is a melee attack (TnMeleeService type 3), so it waits for the melee system [PARTIAL].
- **Hover:**
  - TnAbilityHover → TnAcrobaticsManager JumpingToHover (jump to HoverJumpHeight 500 UU), then Hovering when descending:
    PHYS_Flying, MaxAirSpeed HoverAirSpeed 500 UU/s, HoverDuration 7 s;
  - jumping or expiry → Falling; TnBuffIncreaseDamageDuringHover ×1.4 while hovering;
  - Cooldown[0] 35 s once not hovering.
  - Test: rose 4.9 m, held height, ≤ 5 m/s, fell back; cooldown 31.4 s, 3.6 s after the end.
- **Melee** [CONF RE TARGETED_PASS3 §H; TnMeleeSet authored]:
  - Q → MELEE_WeaponAttack (player pawns never stomp): Melee_EnergonSword_01 / _03 alternately, full body.
  - Assist target: an enemy within 20 m inside the picker cone clamp(4°, atan(3.5 m/d), atan(4.5 m/d)).
    The pawn lunges toward it, yaw only: 25 m/s for 0.25 s, then ×0.3. Ground speed ×0.75 while attacking.
  - Damage sweep at t 0.19 s for 0.35 s: box (250, 250, 350) UU at MeleeSocket_SmallRobot. Clear-trace check;
    one hit per actor per sweep; 150 TnDamageTypeMelee.
  - **Whirlwind** ability → MELEE_Whirlwind: Transform_Whirlwind_ROBO, upper body, ground speed ×1.2.
    Eight 0.4 s sweeps (0.9 … 5.19 s), box (450, 450, 200) at PositionSocket, 85 TnDamageTypeWhirlwind each.
    The trigger fails (no cooldown) unless the melee manager is idle in robot form;
    Cooldown 60 s starts once the whirlwind ends.
  - PARTIAL:
    - melee impulse / momentum and hit reactions are not applied;
    - flag / bomb carrier 9999 attacks, the type-2 melee weapons' own attacks, GunButt combo and per-chassis sweep times
      (the LightMedium shared set is used for all).
  - Tests (WFC_PARTICIPANTTEST 7/7): Q 150 with a 5.4 m lunge; Whirlwind refused during the swing, hits in both
    early windows, cooldown held until the end.
- **Homing lock-on** [CONF RE TARGETED_PASS3 §H2; authored WEPDATA / PROJDATA in WeaponTable.inc]:
  - TnWeaponHoming.Active.Tick for the active weapon (on foot or the vehicle weapon).
  - Target: an enemy within weapon range inside picker index 4 about the crosshair ray: 4°, clamped to cover
    600–700 UU (robots) or 500–700 UU (cars; other vehicle forms use the car picker [PROV]).
    Robots are skipped while CanLockOnToRobots is false (every MP homing weapon).
  - Lock: LockOnTimer reaches LockOnTime → locked; a target change resets it; HoldLockOnTime drops it
    with no target.
  - The shot carries the target only if locked. The projectile homes with HomingForce, switches to
    ClosingForce within ClosingDistance and explodes after ClosingTime; capped at MaxSpeed; stops homing if
    the target dies or becomes a robot.
  - HUD: lockTarget / lockProgress / locked.
  - Test: no lock on a robot; vehicle lock at 0.52 s (0.5 s + frame); the rocket aimed 4 m off at 50 m hits.
### Pass 22 multi-map stress (2026-10-05, build of 1fc54a3; logs work/pass22/logs/maps)

All ten MP maps load with their own KillZ (BASE WorldInfo), hazard volumes, pickups and objectives. WFC_MAPSUITE passes on every map.

| Map | Oracle (authored ReachSpecs arrived) | Tours robot / vehicle (fell) | Transforms under map / KillZ | Chaos (20 × 20 s) |
|---|---|---|---|---|
| Streets | 852/852 | 100/122, 98/122 (0) | 0/380 | 0 under, 0 KillZ |
| Gorge | 1473/1476 | 134/147, 126/147 (0) | 0/380 | 2 under (lower path under a deck), 1 KillZ (chasm) |
| Rust | 1672/1706 | 116/139, 118/139 (0) | 0/380 | 0 / 0, 1 stuck |
| Debris | 349/350 | 53/75, 55/75 (4) | 2/380, both KillZ | 7 KillZ |
| Berth | 1302/1302 | 128/143, 127/143 (0) | 0/380 | 0 / 0 |
| Seed | 972/972 | 109/136, 110/136 (0) | 0/380 | 0 / 0 |
| Remnant | 5600/5916 | 1305/1363, 1242/1363 (0) | 0/380 | 0 / 0, 2 stuck |
| BrokenHope | 5570/6074 | 1132/1196, 1112/1196 (0) | 0/380, 8 refused | 1 KillZ |
| Molten | 887/914 | 87/119, 87/119 (0) | 17/380: 16 KillZ from one start (lava pit edge), 1 under a deck 2.96 m | 1 KillZ |
| Complex | 1317/1322 | 109/130, 111/130 (1) | 0/380 | 2 KillZ |

Classification:
- **Debris:** KillZ 100.0 m sits just under the lowest walkable floor (100.4 m), so any fall off a platform edge dies:
  authentic for the space map. The transform KillZ cases are pawns carried off edges at 15–28 m/s.
- **Gorge / Molten under-floor cases:** a pawn on a lower path beneath a walkable deck, not inside geometry.
- **Remnant / BrokenHope oracle shortfalls:** long jump / air-path ReachSpecs on the two largest maps (6000 runs).
  No falls; not yet broken down [PARTIAL].

- **Splash falloff** now subtracts the victim's collision radius before scaling:
  Dist = max(d − ColRadius, 0), scale 1 − Dist/DamageRadius [HIGH stock UE3 Actor.TakeRadiusDamage].
- **Grenades** [CONF script TnGrenadeBag / TnGrenadeThrower / TnProjectileGrenadeBase + authored; RE §H4]:
  - G ("Throw Grenade") in robot form → TnGrenadeBag.TossGrenade. CanToss = ammo and no FireInterval timer (1.5 s);
    otherwise dry fire.
  - The bag is given without activation: not in the weapon-swap cycle; reserve = MaxAmmoCount (Flak 1, FlashBangs 2).
  - Target: view trace from 10 m to 100 m (hit or end point). GrenadeThrow upper-body clip; spawn after
    TossDelay 0.4 s at MeleeSocket_RightHand; ExposeSelf.
  - Velocity:
    - SuggestTossVelocity(TossStrength 110 m/s) [PROV: native; the exact lower ballistic arc under world gravity];
    - AdjustTossVelocity lerps toward LowPitchSpeed below LowPitchDegrees.Max;
    - Init scales by lerp(SpeedScaleAtMinPitch, AtMaxPitch, pct(pitch, MinPitch, MaxPitch)).
  - Flight: gravity × GravityScale. Impacts reflect × BounceDampening and rest when v² < 500 UU²/s².
    The fuse (RandomInRange(FuseTime)) starts on the first impact; ExplodeWhenHittingPawn grenades detonate on a pawn.
    Explosion = HurtRadius (Flak 325 / 20 m, TnDamageTypeFlakGrenade).
  - The surface normal at a world impact is estimated (floor or reversed travel) [PROV]. Flashbang blind, heal
    grenade healing and kamikaze-mine seeking are not simulated [PARTIAL].
  - HUD grenades (reserve, −1 without a bag).
  - Test (WFC_WEAPONTEST 17/17): spawned at 0.4 s, first impact 0.35 s later, exploded 2.00 s after it; the empty
    bag refused the next toss.
- **Tank cannon** [CONF script + authored VEH_Tank_ANIMTREE; RE §H3]:
  - WeaponPrimary = HmSkelControl_TurretConstrained on C_Cannon_XB, actor space, no constraints.
  - Player DesiredBoneRotation = (view pitch, hull yaw, 0): the cannon only pitches; the hull yaw is camera-slaved.
  - LagDegreesPerSecond 360 applied as a max turn rate [HIGH].
  - Applied as a mesh-space pitch over the animated pose (cannon level at rest) [PROV].
  - Test: 0.300 rad view → 0.300 rad hull-relative cannon, peak 360°/s.
- **Knockback** [CONF RE TARGETED_PASS3 §I]:
  - TnPawn bIgnoreForces: only RequestRespectForcesApplied damage types push: Melee / WeakMelee / Whirlwind,
    Shockwave, AOE*, HeavyTankShell*, ShieldPush*, OmegaAOE*, ExplodeWithForces …
    (* bExtraMomentumZ: Z = max(Z, 0.4|M|)). Weapon, projectile and grenade damage types give none.
  - Momentum / Mass 100. Robot: Pawn.AddVelocity (walking → falling; halve a rising Z above JumpZ).
    Vehicle: ×0.5 linear velocity.
  - Sources:
    - melee normal(victim − attacker) × Impulse: 30000; flag / bomb 80000; Whirlwind 2000;
    - Shockwave 700000 from the origin.
  - Test: melee 3.00 m/s, Shockwave 70.0 m/s, IonBlaster 0, vehicle 1.50 m/s.
- **Flag / bomb carrier** [CONF script TnWeaponFlagBase / TnInventoryManager / TnPawn.Transform; RE §I]:
  - The objective is the held WT_Heavy weapon (Code Of Power / Bomb, HUD heavyWeapon): no gun fire, and
    grenade toss refused (dry fire).
  - Q = the MWT_Flag / MWT_Bomb attack: Melee_Mace _01/_02/_03 sweeps, 9999 damage, impulse 80000, no lunge.
  - Dropped (a pickup at the carrier) on Transform to vehicle (DropHeavyWeapons), on a weapon swap (ChangedWeapon
    TossWeapon) and on death.
  - Pickup is contextual, not on touch (TnWeapon.PickupWhenTouched needs the class already held):
    E ("Interact / Pick Up") → TryPickup → ServerPickup → TnPickupManager.Pickup on a touching factory or dropped
    flag / bomb that passes ValidTouch. HUD pickupPrompt. [CONF RE §J]
  - TnPlayerPawn.CanPickupInventory rejects vehicle form, meleeing (and downed) pawns; defenders are rejected for the flag.
    The pickup line-of-sight recheck is not run [PARTIAL]. Diagnostic participants hold the pickup button (no AI).
  - DropFrom FindSpot box (450, 450, 100) is not run; the drop is at the carrier [PARTIAL].
  - Tests (WFC_CTFTEST 12/12, Streets + Gorge): transform drop, no vehicle re-pick, robot re-pick on a new touch;
    the local carrier's gun is blocked and a swap tosses the flag.
- **Barrier** [CONF TnAbilityBarrier / TnBarrierSpawnable script + authored; RE §I3]:
  - Skill_Barrier anim, then SpawnDelay 0.5 s: wall at Location + (1000, 0, −200) rotated by the pawn, facing it.
  - Collision: the PHYSSYS box (167 × 1736 × 823.5 UU at (−59, 0, 91) about C_Robo01_XT, bone frame taken as
    the actor frame [PROV]) as a dynamic set in the pawn and weapon collision.
  - Blocks pawns, hitscan and projectiles (zero-extent blocking HIGH); takes hitscan and radius damage, not melee.
  - BarrierHealth 1000, DegenRate 15/s; at 0: FadeOutTime 3 s, then gone; destroyed with the owner.
  - Cooldown 20 s once the barrier is gone. Mesh WEP_Barrier_SKEL with Barrier_Equip, drawn by Gameplay.
  - PARTIAL: the flashbang instant break and the TnBuffIncreaseBarrierHealth +500.
  - Test (PARTICIPANT 10/10): up at 0.5 s; shots absorbed (999 → 784) with the target untouched; owner
    stopped at 6.9 m; 60 HP decay in 4 s; cooldown 20 s after the fade.
- **SpawnAmmoCrate (ammo beacon)** [CONF TnAbilitySpawnInventory / SpawnAmmoCrate / TnDroppedPickupAmmoBeacon / Defrag
  script + authored]:
  - Skill_Barrier anim; SpawnDelay 0.5 s; dropped from the owner with TossVelocity (2000, 1200, 0) rotated by the
    owner; falls and lands.
  - BeaconLifespan 60 s; gone with the owner.
  - Each tick, the owner and teammates within 1500 UU with line of sight: current weapon FillReserveAmmo, and
    TnBuffAmmoBeaconIncreaseDamage ×1.15 (BuffTime 1 s, refreshed).
  - Health 100: owner / team damage ignored. Not a pickup.
  - Cooldown[0] 60 s once it is gone. The HUD exposes the mesh position for Rendering
    (PROP_NEU_AmmoPickup_STAT).
  - PARTIAL: FadeOut duration (removal is immediate); skill gifts / grenades (no skills in versus).
  - Test (PARTICIPANT 11/11).
- **Buff killstreaks** [CONF authored TnKillstreak* CDOs (TnAbilityAddBuff BuffTarget / BuffToAdd) + script]:
  - **Orbital Beacon:** team TnBuffSeeEnemyObjectiveMarkers 30 s. SetupEnemyMarker draws enemy markers unless the
    enemy has a Warcry buff.
  - **Orbital Beacon 2.0:** other team TnBuffHardLocked 10 s (marker for the instigator's team) plus 1 damage
    TnDamageTypeFlashBang. HardLocked FloatModifier[0] 1.4 = damage taken (TnPawn._AllDamageModifierSelf) [CONF RE §K].
  - **Health Matrix 2.0:** team TnBuffRefillHealthOnKill 60 s. TnPawn.HandleDied gives the killer
    HealDamage(TnHealTypeHealthPickup = SHT_AddAllSegments).
  - **EMP:** other team TnBuffAbilityJammedKillstreak 30 s. TnBuffAbilityJammed.Apply: CooldownMultiplier 0
    (cooldowns frozen; ready abilities still usable); Cloak / Disguise / Warcry removed; hover falls; whirlwind aborts.
  - HUD seeEnemies / hardLocked / refillOnKill / abilitiesJammed (s left). Test (PARTICIPANT 12/12).
- **Drain** [CONF authored TnAbilityDrain / TnBuffDrainSource Blueprints[0] + RE §J]:
  - Self buff 7 s at caster speed ×0.7.
  - Each tick, every enemy within 2000 UU with line of sight takes 25 DPS (TnDamageTypeDrain); the caster heals
    35 HPS per target (heal type AddHealthToAll [PROV]).
  - Cooldown 60 s after the buff (HadAndLostBuff). Removed by AbilityJammed.
  - Beam FX DrainRay_Beam_FX from MeleeSocket_LeftHand → Rendering (HUD drain).
  - Test (PARTICIPANT 13/13): enemy −50 in 2 s.
- **SpawnSentry** [CONF TnAbilitySpawnSentry / TnSentryPawnAbility / TnAiSentryController + authored Default_TURRETDEF /
  Default_WEPDATA / Sentry_DSYS; RE §J]:
  - Spawn: 0.2 s delay; owner + 375 UU up, clamped by a trace, then settled on the floor; the previous sentry is killed.
  - Body: 135 HP draining over Lifetime 30 s; owner damage ignored; melee kills it; dies with the owner.
  - Aim: closest visible enemy within pitch ±45° (SightRadius 30000); YawPitchControl 270°/s; fires within 3° and
    6000 UU.
  - Weapon: 8 instant-hit (range modifier 1.0 to 8000 → 0.5 at 30000) every 0.12 s, spread 0.1; heat +2 to 100 then
    OverheatDelay 2 s (heat reset [PROV]). Kill credit to the owner.
  - Cooldown 60 s once gone. Mesh WEP_SentryDeploy_SKEL with WEP_DeployedTurret_Activate, drawn by Gameplay.
  - Hit volume: the 200 UU cylinder [HIGH].
  - PARTIAL: flashbang dormancy, Rocket / Repair blueprints, turret pitch on the mesh, 5 s corpse, the 2-sentry claim
    limit (one sentry per owner here).
  - Test (PARTICIPANT 14/14): 16 shots in 2 s, hits of 8, drain 18 HP in 4 s.
- **GuidedMissile** (ability and the Soldier 7-kill Omega Missile streak) [CONF RE §J3 + authored GuidedMissile_PROJDATA /
  GuidedMissile_STRATEGY]:
  - Launch: 1.0 s Skill_GuidedMissile; spawned along the controller rotation with pitch clamped to 3.8°–90°.
  - GuidingMissile: inputs cleared (pawn stops); camera attached at (135, 0, 125) UU in the missile frame, FOV 120.
  - Steering: per-tick camera deltas → LeftRight / UpDown clamp ±1 → lateral ControlStrength 2500; speed held at 2000.
  - Ability press detonates: 10000 within 4500 UU. The owner within 45 m dies to self damage (×0.45).
  - Fuse 30 s; cooldown 45 s once the missile is gone.
  - PROV: chest socket (eye height used), fuse expiry detonation, camera-delta rotator units.
  - Test (PARTICIPANT 15/15 on Streets, Gorge, Debris and Rust).
- **RollerSphere** [CONF TnAbilityRollerSphere / TnRollerMineAbility CDOs, RE §J4, AssetTools ability_physics.json]:
  - Spawn: 0.5 s, at owner + (500, 0, 100) if safe (else retry every 1 s), local velocity 2750 UU/s.
  - PhysX sphere: radius 1.208 m (241.5 UU × scale 0.5), LinearDamping 0.6 (authored PHYSMAT); Friction 0.7 and
    Restitution 0.3 (Engine PhysicalMaterial defaults).
  - PROV: slope acceleration, angular damping, mass.
  - ArmTime 3 s; Fuse 10 s; Health 200 (enemy damage only).
  - Armed contact with an enemy → 135 / 1500 UU (TnDamageTypeRollerMine, no momentum).
  - Owner / team melee kick: +5000 UU/s horizontal.
  - Aura: visible enemies within 1500 UU get speed ×0.75 (1 s robot / 2 s vehicle), refreshed.
  - Gone with the owner; cooldown 60 s once gone. Mesh RollerMineAbility_STAT for Rendering (HUD rollerPos).
  - Test (PARTICIPANT 16/16): 26.4 → 14.5 m/s in 1 s (e^−0.6); safe before arming; armed contact −135.
- **Class-pool abilities** [CONF RE §K]:
  - **HardLock (Mark Target):** the homing-lock pick (picker 4, robots allowed) gets TnBuffHardLocked level 0 for 10 s
    (marker for the team; damage taken ×1.4); no target → fails; Warcry removes it.
  - **AbilityJammer:** projectile 10000 UU/s from offset (0, 175, 25), enemies only → TnBuffAbilityJammed 15 s.
    Abilities are blocked (controller HasDerivedBuff), cooldowns frozen, cloak / warcry / drain stripped.
  - **TransformDisruptor:** projectile 6000 UU/s → TnBuffTransformDisruptor 3 s: forced into the other form,
    transforming disabled.
  - Shots aim through the crosshair. Their life after a miss (3 s) is PROV.
  - Cooldowns 60 s.
  - Test (PARTICIPANT 18/18).
- **Camera obstruction uses simple collision only** [CONF RE MILESTONE04_CAMERA_COLLISION addendum, f150a6a]:
  - execTraceCamera → SingleLineCheck with flags World | 0x0A000000 (0x02000000 = BlockCameras). No complex / per-poly bit.
  - Static meshes are traced through their simple collision (UseSimpleLineCollision / UseSimpleBoxCollision default true),
    which is what collision_pawn.glb holds.
  - Example: in Streets' Ceiling_Arch_STAT (StaticMeshCollectionActor_2508) the curved render underside below the simple
    slab has no collision. A camera can enter it, in the original too (M08 soak frame 20_TDM_508e): authentic, left as is.
- **Class preset grenades** [CONF authored TR_MPPlayerCharacterData_p.<Class>_PCD_MP]: Scout FlashBangs, Scientist HealGrenades,
  Soldier FlakGrenades and Leader KamikazeMines are equipped on the class's chassis. The exported per-chassis on-foot list
  omits them there, so a grenade bag is accepted when it is the selection class's preset grenade; other classes' grenades
  stay refused. Reported by the M08 soak (every preset grenade was refused). WEAPON 19/19.
- **Weapon / spawner killstreaks** [CONF RE §K + authored]:
  - **P.O.K.E. 2.0:** TnWeaponPoke for 20 s (SecondsUntilDeactivated [H]): DisallowWeaponSwitching, ground speed ×1.5.
    Fire or Q = the MWT_Poke attack: Melee_Axe sweep at 0.335 s, 9999 TnDamageTypePoke, impulse 200000, lunge.
  - **Nucleon Shock Cannon:** HeavyRocketTurret (HeavyTurret_Rocket_WEPDATA: 10 rockets, 1.75 s, 120 m/s, 500 / 25 m),
    WT_Heavy: dropped on swap or transform (not re-takeable [PARTIAL]); ground speed ×0.75.
  - **Thermo Mine Re-Spawner:** 15 s buff spawning a kamikaze mine every 2 s at owner + (400, 100, 0).
    Mine: hover 2 s (1 m [PROV within 50–150 UU]), then seeks the closest visible enemy within 2000 UU at 2300 UU/s;
    125 / 500 UU on contact; health 50; life 60 s.
  - All 12 class killstreaks are implemented. Test (PARTICIPANT 21/21 on Streets and Gorge).
- Remaining unimplemented abilities (class pools only): Disguise, DecoyTrap are listed per slot and reported
  unimplemented (log + HUD `implemented = false`) [PARTIAL].
  Skills are not applied in versus (skill-data index 0, no skill effects) [PARTIAL]; killstreaks: see above.
- **Correction:** the Pass 21f contract doc said robot Shift ran a dash. It did nothing until this pass.

### Vehicle forms — CONFIRMED script (RE §C), rigid-body details PROVISIONAL
- **Car** (Car–Car7): the same TnHoverCarSimulation as the truck.
  - Blueprints: HoverCar_Physics (accel 4000, mount radius 100, dash 0.5 s @ 3000); HoverCar_Supension (K 8000,
    rest 200, D 4000); Car_Physics (mass 1500, 4 wheels from TnWheelPhysicsBlueprint LocalPosition / friction).
  - Hover dash along the **dominant stick axis** (TnCarForm.Hovering.DoDash).
  - **Barrel roll:** Shift while boosting → Roll(): v += yawFrame(0, dir·1200, 1000 − vz); RollDuration 0.7, cooldown
    2.0. A full turn completes in VEHTEST.
  - The roll rate (one turn per RollDuration) stands in for the unrecovered PhysX max angular velocity [PROV].
- **Tank** (TnHoverTankSimulation):
  - suspension 4 rays, radius 250, K 20000, D 6000;
  - strafe servo in the camera-yaw frame, only when stable;
  - boost cap 2500 with input forced forward; release → drift 0.5 s; jump every 0.5 s;
  - pitch / roll corrected unless stable on the ground;
  - Shift "180" = view half-turn, cooldown 1.2 s [PROV: TnTurnAroundCameraBehavior timing].
  - Cannon recoil (−750 local X on fire) waits for vehicle weapons [PARTIAL].
- **Jet** (TnPlaneForm Hovering / Flying), gravity cancelled in both modes:
  - **Hover:** servo in the full view frame (mask 1,1,1), accel 2500 × max(drift², |stick|), cap 1500. Ascend / Descend
    (C / V, the shipped binding) → Dash ±Z at 1000. Roll at |stickX| ≥ 0.5, 0.6 s @ 3000.
  - **Flight:** boost held → always thrust along the view (4000 / 3000), quadratic lateral drag (DragCoefficient 600).
    Lean = RLerp((−pitch²·27, yaw²·16, yaw²·77)°, 0.1), with the pitch term fading 45 → 60°. Roll 0.8 s. Release, or
    a hit above 3000, returns to hover.
  - The plane rigid-body mass in the drag formula is unknown (100 used) [PROV]. The motion lean of hover is omitted
    [PARTIAL].
  - **Camera:** HoverPlane ±45° / 9 m / FOV 80; FlyingPlane ±80° / FOV 100 [CONF authored]. Follow-camera behaviour
    [PROV].
- **Vehicle FX and sounds** drawn by Gameplay (VehicleFx: BoostFx / HoverFX / JumpFX / ram) are OptimusTruckForm's, on
  VH_OptimusPrime bones. They now play only for the Optimus chassis (Truck / Truck7). Other chassis' authored sets
  (character.json vehicle.fx, e.g. Starscream Afterburner_D_FX) are left to Rendering [PARTIAL].
- **Hulls:** each chassis' VH_*_PHYSSYS convex hull (BodySetup ConvexElems bounds, C_Reference_XR) from authored data
  [CONF] (VehicleHullTable.inc). Element 0 of VH_Optimus_PHYSSYS reproduces the Pass 17 hull exactly.
- **ChassisOffset:** unset → 0 (no class default authored). The loader first defaulted it to Optimus's 15, which lifted
  the COM of Soundwave and others wrongly.
- **VEHTEST forms:**
  - car rest at the spring L_eq;
  - car dash right 30 m/s;
  - car barrel roll 180° max roll, landing level;
  - tank hover 15 / boost 25 with strafe input ignored / drift after release;
  - jet holds altitude, ascends at 10 m/s, flies 40 m/s along the view, releases to hover.
- **Truck numbers unchanged** (rest 1.2872 m, steering, nitro, ramp sweep, boost continuity).

### Transform clearance — UWorld::FindSpot order (RE §C6, CONFIRMED native)
- Robot-extent overlap test against world geometry.
- Depenetration candidates: Z, then X, then Y at 1.0 extent, then 0.5; then the ±X±Y±Z diagonals at 0.5. The first
  clear spot wins; refuse only when none is clear.
- A vertical push never lands on a surface above the start.
- VEHTEST clearance: refuse / fit / displace pass.

### Weapons — selection → inventory → mesh → firing → kill feed are one weapon
- **Data:** versus uses **MultiplayerData** (TnMultiplayerGame.DesiredWeaponDataType = 3, CONFIRMED RE). It falls back
  to PlayerData when a weapon has none (TnWeapon, CONFIRMED). Generated `WeaponTable.inc` (52 weapons) from
  authored.db + mp_weapons.json + cooked weapon sockets.
  - mp_weapons.json's "player WEPDATA" block is the SP set; reported to AssetTools.
- **Inventory** (TnCharacterApplier.ApplyWeapons / CreateWeapons): CharacterData.WeaponTypes in order, first active.
  - Iconic → the chassis preset.
  - Custom → the selection's list, validated against the chassis' TnDataProvider_Weapon restrictions. A disallowed
    pick is refused and reported (`loadoutRefused`), never replaced.
  - VehicleWeapons are kept and reported.
- **Swap Weapons** (PgUp / PgDn): PutDownTime + EquipTime from WEPDATA; no firing in between.
  - **Instant-hit weapons simulate:** damage, interval, NumShotsToFire pellets, falloff, spread, reload.
  - **Projectile, melee and grenade weapons** are equipped and shown but flagged `weaponSimulated = false` [PARTIAL].
- **Presentation:** the active weapon's own mesh and AnimSet at the chassis socket, with its MuzzleFlash socket.
  - Ion Blaster particle FX are drawn only for the Ion Blaster; other weapons expose their FX template names
    [PARTIAL, Rendering].
  - Weapon sounds stay the Ion Blaster's [PARTIAL, Systems].
- **WFC_WEAPONTEST 9 / 9:**
  - the table reproduces IonBlaster_WEPDATA;
  - iconic loadouts for Truck / Car2 / Jet / Tank3 / Truck4;
  - Car2 AssaultRifle refused (ChassisRestriction Jet / Tank);
  - Shotgun 8 pellets;
  - swap timing to ShortSword (not simulated);
  - firing spends the active weapon's ammo with its damage type.
- **Visually verified:** Sideswipe holding the Neutron Assault Rifle.

### Code of Power (CTF) and Countdown to Extinction (EXT) — all six versus modes on the shared framework
- **Rounds** (TnGameRules_RoundsBase, CONFIRMED):
  - GRI.Rounds = PointsToWin for CTF (default 2), TimeLimit per round (default 300);
  - the round timer replaces the match clock (RunGameTimer false);
  - at 0 → CurrentRound++ → EndGame(none, "Score") after the last round, else BetweenRounds 5 s → RestartRound (every
    player respawns, no death counted);
  - RoundEnded / RoundStarted events.
- **SingleFlagCTF:** first attacker RandomInt(2), alternating each round.
  - SetupRoundStart: the attackers' capture point _Active; the defenders' flag factory in Pickup, the other asleep.
  - Mercy rule on the last round (the last attacker already leads → end).
  - A capture does not end the round.
- **Flag:**
  - defenders can't take it (ValidTouch);
  - capture = carrier inside the active capture point's ObjectiveVolume → ScoreObjective(1): +1 team, +10 personal
    (IndividualScore 10); the flag goes straight home;
  - carrier death → dropped flag: AutoReturnTime 30; defenders touching it drain ReturnFlagTime 10 at dt × count,
    recovering +dt with none; attackers re-take it; falling below KillZ sends it home.
- **Bomb:**
  - neutral factory; anyone takes it; GRI.AttackingTeam = holder team;
  - plant on the ENEMY TnBombPlantPoint (ObjectiveVolume) → FuseTime 15;
  - defenders inside accumulate DefuseTime 5 (reset when none) → the bomb drops at the point (DefuseBombSpawnClass);
  - detonation → ScoreObjective(planter, 1), HurtRadius DetonateDamage 9999 / DetonateRadius 5000 UU (AOE), bomb home,
    factory WaitAfterScoreTime 5;
  - PointsToWin 3, TimeLimit 900.
  - All values come from authored TnBombPlantPointBase / TnDroppedPickupFlagBase / factory defaults [CONF].
- **Teams:** flag factories, capture points and plant points carry authored DefenderTeamIndex (byte default 0).
  Clusters filter TNGT_CTF / TNGT_EXT.
- **Touch shapes:** factory CylinderComponent 200 / 100 UU; dropped pickup TouchCylinder = CylinderComponent default
  22 UU [HIGH]; pawn cylinder 2 m [PROV for non-Optimus chassis].
- **PARTIAL:**
  - the carrier's TnWeaponFlag1Hand / TnWeaponBomb weapon swap (the carrier keeps its weapon);
  - carrying in vehicle form is allowed [UNKNOWN];
  - flag / bomb messages are logged (switch numbers), not presented;
  - XP events are not implemented.
- **WFC_CTFTEST 10 / 10:**
  - round-1 activation;
  - defender refused, attacker capture +1 / +10, flag home;
  - drop + defender return in 10 s;
  - round timer → 5 s break → attackers swap → match end by Score;
  - EXT: holder sets the attacking team; plant / defuse 5 s / drop at the point;
  - detonation +1 team, +10 (+1 blast kill) personal, blast kills within 50 m, factory sleeps.
- **Assists** now divide by the victim's HealthMax (its class blueprint), not 550.

### Camera settings (RE TARGETED_PASS3 §G2)
- `PlayerController::setLookSettings(CameraSensitivity 0–100, InvertY_Robot, InvertY_Car, InvertY_Plane, InvertY_Tank)`.
- Orbit speed follows Lerp(0.03, 0.20, s/100) relative to the default 30 [CONF curve; absolute mouse rate PROV].
- Invert per form: car and truck share InvertY_Car.
- Frontend calls it on Settings commit and at match start.

### Vehicle cameras per chassis — CONFIRMED authored
- Each chassis' HmCameraStrategySet (roster cameras) supplies hover / driving / flying strategy values: anchor, orbit
  distance, pitch range and FOV (CameraTable.inc, tools/gameplay/gen_camera_table.js).
  - Truck 1.85 / 9.5 m (drive 2.15 / 10.5);
  - car 1.25 / 5.25 m;
  - tank 1.75 / 8.25 m, pitch −10..25°;
  - jet hover 1.5 / 9 m ±45°, flying ±80° FOV 100.
- The truck row reproduces the existing constants exactly.
- CAMSYNC at 144 Hz: Sideswipe and Starscream show no separation (robot 0.0002°).

### Killstreaks — framework CONFIRMED script; 4 of 12 effects implemented
- **Counting:** PRI._CurrentKillStreak += kills (AddKills); death resets it (AddDeaths → KillStreakEnded).
- **Earning:** UpdateKillstreakRewards(count) → FindKillstreak(Specialty, count) → AcquireKillstreak (stack, no duplicates).
  Each class has three streaks at 3 / 5 / 7 kills (TnDataProvider_Killstreak).
- **Triggering:** B (TriggerKillstreak) fires the newest; RequiresRobotForm streaks in vehicle form transform first and
  then trigger (Deferred). ClientGameEnded clears the stack.
- **Implemented:**
  - Overshield Matrix: team OverShieldPickup heal;
  - Ammo Matrix: team FillReserveAmmo + TnBuffLockAmmoClip 10 s;
  - Energon Recharger: regen × FloatModifier 2 for 30 s;
  - Intercooler: ability cooldown × 5 for 30 s.
  - Buff values come from the authored TnBuff* defaults [CONF]; the exact buff hooks are HIGH.
- **PARTIAL** (acquired and triggered, effect reported unimplemented): Orbital Beacon 1 / 2, P.O.K.E. 2.0, Thermo Mine
  Re-Spawner, Health Matrix 2.0, Nucleon Shock Cannon, Electromagnetic Pulse, Omega Missile.
- **Test:** WFC_PARTICIPANTTEST 5 / 5. Custom Soldier: 3 kills → Ammo Matrix; B refills and locks the clip (32 stays 32);
  death resets the streak.
- **HUD:** killStreak, killstreaks[] (newest last), killstreakImplemented, regenBuff, fastCooldownBuff, ammoLockBuff.

### Projectiles and vehicle weapons
- Projectile weapons fire their WeaponProjectiles[0] class with its MultiplayerData TnProjectileData: InitialSpeed, Damage,
  DamageRadius, DamageType [CONF authored]. Examples: TankShell 20000 / 170 / 2500; RocketVh 18000 / 55 / 1500.
- Flight is straight. Homing lock-on (TnProjectileDataHoming HomingForce / ClosingForce) is PARTIAL: fired straight.
- On any hit: HurtRadius with stock UE3 linear falloff [HIGH]. Teammates are filtered; the instigator is never hit by its
  own shot in flight.
- **Damage taken** is scaled by the victim form's DamageMultiplier (ROBODEF 1.0; VEHDEF e.g. 0.75 tank, 0.8 jet, 0.9 car)
  and, for self damage, by SelfDamageMultiplier 0.45 [CONF data; HIGH placement in TakeDamage].
- **Vehicle form fires** its CharacterData.VehicleWeapons[0] (projectile or hitscan). Origin = the chassis' vehicle
  WeaponSocket_Primary (bone × socket, CONF); aim = the camera aim point. The tank cannon turret rotation
  (UpdateCannonRotation) is not animated [PARTIAL].
- Repair rays are flagged unsimulated (they heal) [PARTIAL]. Projectile meshes and trails are drawn as a small box
  marker until Rendering draws them [PROV presentation].
- WFC_WEAPONTEST 12 / 12: Warpath's TankCannon shell flies and hits for 131; self damage 49.8 ≤ 170 × 0.45 × 0.75.

### Non-local participant pawns (bot-ready architecture, RECONSTRUCTION EXTENSION boundary)
- Participants (MatchOpponent) are full pawns: the same chassis, specialty, loadout, movement, transformation, damage,
  death / respawn and objective paths as the local pawn. Their inputs come only from `setIntent` (harnesses); no AI.
- WFC_PARTICIPANTTEST 4 / 4. TDM assists now use the victim's class HealthMax.

### HUD objective markers for every mode
- HudGameState.objectives now lists every active-in-mode objective: DOM nodes, KOTH zones, CTF flag factories (active at
  home) and capture points (active for the attackers), the EXT bomb factory and plant points, each with its team.
  Tags carry `label` (false when cloaked).

### HUD state additions
`selectedChassis`, `drawnChassis`, `specialty`, `spawnError`, `weaponId`, `weaponIcon`, `weaponSimulated`,
`weaponSwitching`, `inventory[]`, `activeWeapon`, `vehicleWeapons[]`, `loadoutRefused[]`. `segmentCount` follows the class.

### Multi-map
- World loads any processed MP map (`WFC_MAP`, or the launch URL's map). Everything is read through the shared contract:
  world / collision / gameplay / physics / navigation.
- KillZ comes from the persistent level's WorldInfo: Streets −750 m, Gorge −75 m, Rust −5.1 m, Debris +100 m.
- Streets' rotating domes are restricted to Streets.
- Gorge, Rust and Debris load with their own starts, pickups, objectives and destructibles.
- **Hazard volumes** (AssetTools maps/<Map>/hazard_volumes.json): convex PhysicsVolume brushes with DamagePerSec / DamageType.
  Stock UE3 pain applies: DamagePerSec × PainInterval on entry and every PainInterval (1 s default) [HIGH].
  TnDamageTypeInstantKillAi forces Died only for AI pawns (TnAiPawn.TakeDamage); for players it is 2000 damage, which is
  lethal [CONF].
- Harnesses take the map's paths and KillZ. `WFC_MAPSUITE` runs per map: TDM launch, 12 respawns on floor and clear,
  KillZ death, hazard damage, pickups, second match.

### Stress per chassis (Streets, fixed 60 Hz)
| chassis | WFC_CHAOS | WFC_XFORMTEST |
|---|---|---|
| Truck (Optimus) | 60 starts: 0 under map, 0 KillZ, 0 stuck | 0 / 1520 under map |
| Car2 (Sideswipe) | 30: 1 under, 0 KillZ, 1 stuck (before the hull / ChassisOffset fix) | 0 / 760 |
| Jet (Starscream) | 30: 0 / 0 / 0 | 0 / 760 (1 forced back to vehicle) |
| Tank3 (Warpath) | 30: 0 under, 0 KillZ, 1 stuck | 0 / 760 (1 refused, 7 forced back) |
| Truck4 (Soundwave) | 30: 6 under + 1 KillZ before the fixes → **1 under (4 frames, vehicle under a deck its 1.42 m hull top clears), 0 KillZ** after | 0 / 760 |

- Weapons and loadouts: WFC_WEAPONTEST 10 / 10.
- TDM 43 / 43, modes 21 / 21, chassis 13 / 13.
- CAMSYNC 60 / 144 / 240 Hz unchanged (robot 0.0003°).

---

## PASS 21e — TRANSFORM CLEARANCE, ROSTER CONTRACT, HUD STATE COMPLETION, REGRESSION GUARDS (2026-10-04, gameplay agent)
Inputs:
- RE OVERNIGHT_2026-10-04 §A2, A5, B3, E, F;
- Rendering M08 (HUD ownership: Hud_GFX presents, Gameplay supplies state).

Handoffs:
- `docs/handoffs/GAMEPLAY_FRONTEND_HUD_CONTRACT.md`: HUD state / events, character selection, settings and input;
- `docs/handoffs/GAMEPLAY_BOT_READINESS.md`.

### Transform clearance — CONFIRMED ORIGINAL behaviour, PROVISIONAL geometry test
- **Before vehicle -> robot**, `PlayerController::tryBeginTransform` (TnPawn.Transform -> MoveToSafeTransformationLocation
  -> FindSpotAwayFromPawns with the target extent):
  - the robot cylinder must fit at the floor under the vehicle, or at one of 16 nearby spots (1 m / 2 m rings, floor
    needed, no wall in between);
  - **no spot -> refused**: `cantTransformCount` pulse = HUD NotifyCantTransform (`mc_cantTransform`) +
    TransformFailedSound (`BL_TRANS_POWER.TRANSFORM_DISABLED`, TnPlayerController default, CONF);
  - **a displaced spot** moves the collision at once; the meshes slide back over 0.5 s (OffsetMeshes).
- **After the fold**, InRobotForm.BeginState: MoveToSafeLocation; still stuck -> **ForceIntoForm(vehicle)** [CONF].
- **Fit test** [PROV; whether the native search tests world geometry or only pawns is PARTIAL in RE]: five vertical
  columns (the centre from above MaxStepHeight, four at 0.7 r from 1.8 m) clear up to 4 m. An earlier variant that tested
  the offset columns from 0.4 m refused 69 / 1520 stress transforms on ordinary slopes and stairs, so it was rejected.
- **Validation:**
  - VEHTEST clearance: refused deep under a 3 m ceiling, fits on open floor, displaced to z 3.0 when 0.5 m inside the
    ceiling edge;
  - WFC_XFORMTEST 0 / 1520 under the map, 0 refused, 2 forced back to vehicle;
  - **OPEN:** the 16 "low overhang" cases happen after the fold, while the stress test keeps driving the robot forward
    under props. That is robot walking, not the transform.

### Character roster contract (`src/game/CharacterRoster.h`) — CONF data
- `Match::selectCharacter` stores a `CharacterSelection`:
  - custom or iconic;
  - one of four specialties;
  - a stable chassis UniqueId.
- At spawn, `resolveChassis(selection, team faction)` picks the body:
  - custom -> the specialty default per faction (Ironhide / Soundwave, Air Raid / Starscream, Sideswipe / Barricade,
    Warpath / Brawl);
  - DM forces Decepticon.
- Spawning waits for `hasSelectedCharacter` (CheckReadySpawn). The local player is pre-selected as iconic Optimus
  (`Truck`) [RECONSTRUCTION DEFAULT until the frontend selection screen]. Only the Optimus pawn resources load; the other
  32 chassis need AssetTools ROBODEF / VEHDEF exports.

### HUD runtime state — completed against the HUD list
- Added:
  - `weaponName`;
  - damage direction: `damageTakenCount` pulse, instigator location `lastDamageFrom`, view-relative
    `lastDamageBearing` [PARTIAL: the Hud_GFX indicator call is not traced];
  - `cantTransformCount`.
- Kill feed rows now live **5 s + 1 s fade** (Hud_GFX, RE A2) instead of LocalMessage.Lifetime 3 s.
- FFA result text is empty (TnFreeForAllGameOverMessage, A5).
- WFC_TDMTEST 41 / 41: feed expiry at 6 s, HUD damage event and weapon identity.

### Vehicle input — verified against RE (no change needed)
- RMB / LT: fine aim in robot form only; Boost (held) in vehicle form.
- Shift: Dash while hovering, Nitro while Driving (steer × 0.3).
- Driving has Accelerator fixed at 1 (no throttle).

### Boost-continuity status (after 21d)
- Rendering repro (`WFC_VEHDROPLOG`, 840 frames): starts 1 / 4 / 7 / 18 / 21 -> 4 / 2 / 1 / 0 / 35–38 drops. Counts
  vary slightly run to run (real frame time).
- **Every** drop is a near-vertical face (|n.y| ≤ 0.4).
- Start 21 is the truck held against a wall below 2 m/s, re-boosting into it after each 0.5 s drift. Whether the original's
  RB contact report re-fires at that speed is **UNKNOWN** (PARTIAL). It was not tuned.
- Floor seams and steps up to 0.3 m: 0 drops (VEHTEST guard).

### Modes (six recovered)
| Mode | State |
|---|---|
| TDM | complete loop, CONF rules (WFC_TDMTEST 41 / 41) |
| DM | FFA rules (Decepticon bodies, draw on tie, empty result text) |
| Conquest (DOM) | CONF bytecode rules (WFC_MODEPLAYTEST) |
| Power Struggle (KOTH) | CONF bytecode rules (WFC_MODEPLAYTEST 21 / 21) |
| Code of Power (CTF) | PARTIAL: map state only; needs the carried-objective weapon system (flag return 30 s / 10 s defender drain, rounds) |
| Countdown to Extinction | PARTIAL: map state only; same dependency (fuse 15 s, defuse 5 s, dropped bomb 30 s) |

### Regression guards (protecting the human-reported bugs)
| Bug | Guard |
|---|---|
| High-refresh pawn / camera separation | WFC_CAMSYNC 60 / 144 / 240 Hz (robot 0.0003° at all rates; the M05 bug gave 1.28°) |
| Transform under the map | WFC_XFORMTEST 0 / 1520; WFC_CHAOS 0 / 60 |
| Single-frame boost drop | VEHTEST boost continuity, 0 drops on steps ≤ 0.3 m; `WFC_VEHDROPLOG` repro |
| Ramp snagging | VEHTEST ramp sweep (hover and boost climb 20–50°) |
| Transform clearance | VEHTEST clearance (refuse / fit / displace) |

Wide Streets traversal: oracle 852 / 852, robot tour 100 / 122, vehicle tour 98 / 122, 0 falls; sweep 984 runs, 0
KillZ; coherence 0 missing collision.

---

## PASS 21d — BOOST-STATE FLICKER, VEHICLE CONTACT, HIGH-REFRESH GUARD (2026-10-04, gameplay agent)
Inputs:
- Rendering M08 handoffs `GAMEPLAY_BOOST_FX_FLICKER.md` and `GAMEPLAY_CAMERA_FRAME_PACING.md`;
- RE MILESTONE05_PLAYTEST_RE §1–2 and the OVERNIGHT_2026-10-04 note §B.

### Boost exhaust open / close (human-reported; diagnosed by Rendering) — FIXED at the source
- **Cause (mine, Pass 20).**
  - The frontal-collision drop (Driving.OnRigidBodyCollision: contact normal · forward > 0.866 -> Hovering, CONF) was
    judged by the travel direction, not the contact normal.
  - Any block of the low hull probe while moving forward (including grazing contacts) ended boost. That was followed by
    the 0.5 s drift and a re-boost, so the BoostFx restarted about every 0.6 s.
  - Rendering logged 15–31 drops per 14 s on open floor at M05.
- **Fix.** The drop needs the blocking face's normal (into the obstacle) within 30° of forward. The exhaust FX stays bound
  to the real Driving state; nothing is smoothed.
- **Re-run of Rendering's repro** (starts 1 / 4 / 7 / 18 / 21, boost held 14 s, `WFC_VEHDROPLOG=1`): 2 / 1 / 3 / 0 / 22
  drops.
  - **Every** remaining drop is a near-vertical face (|n.y| ≤ 0.34) of a real obstacle: crates and batteries near
    (139, −622); a wall at start 21, where the truck sits pressed against it at 0 m/s and re-boosts after each drift.
  - These are authentic frontal impacts.
- **Regression guard (VEHTEST).** Boost across 0.05 / 0.1 / 0.2 / 0.3 m steps: **0 drops**. A 0.5 m riser reaches the
  0.45 m hull probe, so it is a frontal hit (RE §2.2: boost lips contact the hull, C).

### Vehicle contact (ramps / angle changes)
- **Hover** already matches RE §2.1 (CONFIRMED ORIGINAL): 4 diagonal 250 UU rays, a normal-weighted implicit spring, the
  45° ground test, yaw-only orientation on contact, upright only when airborne or upside down, (up.Z)² strafe.
- **The stops were my Pass 20 hull probes:** faces of 45–60° were treated as walls. Now faces with |n.y| > 0.5 (under 60°)
  don't stop the hull; a rigid-body box meeting a sloped face is pushed up it, which the chassis / spring code reproduces
  [PROV]. Near-vertical faces still block.
- **Boost (Driving).** The body settles onto the support slope instead of level, so the recovered BoostScale (fades to 0
  between forward.Z 0.5 and 0.866, CONF) sees climbs, and gravity acts along the slope [PROV]. The per-wheel
  TnWheelAssembly suspension (K 120000, D 8000, rest 30 + radius 45 UU, CONF) is **not** modelled: the wheel mount
  heights relative to the mesh origin are unknown (RE / AT request).
- **VEHTEST ramp sweep** (3 m ramps; hover 15 m/s, boost 25 m/s):
  - hover 20 / 35 / 50° climb, 65° stops;
  - boost 20–50° climb with the body pitching 19–31°; 65° is a frontal hit -> Hovering.
  - **OPEN:** hover at 35° launches about 6 m and tilts 69° (the same before this pass). It comes from the recovered spring
    response to fast compression on a steep face. I did not tune it; the RB hull contact the rebuild approximates is the
    likely difference.
- **Regressions.**
  - WFC_XFORMTEST 0 / 1520 under the map; the overhang cases are unchanged (15).
  - Map oracle 852 / 852; vehicle tour 98 / 122 (was 97).
  - WFC_CHAOS: 0 under the map, 0 KillZ, 0 stuck, 4 prop entries.
  - VEHTEST hover / steering / nitro unchanged.

### High-refresh pawn / camera separation — regression guard
- The fix is PASS 21a (camera per render frame); Rendering's independent diagnosis and patch agree.
- **WFC_CAMSYNC** (on-screen character offset jitter per frame):

| scenario | 60 Hz | 144 Hz | 240 Hz |
|---|---|---|---|
| robot run + turn | 0.0003° | 0.0003° | 0.0002° |
| hover drive + turn | 0.055° | 0.011° | 0.004° |
| boost | 0.106° | 0.023° | 0.009° |

- The per-tick cache (M05) gave 1.28° at 144 Hz.
- The vehicle residual falls with the refresh rate: it is the truck's own motion, not pacing.

---

## PASS 21c — CONQUEST (DOM) AND POWER STRUGGLE (KOTH) ON THE SHARED MATCH FRAMEWORK (2026-10-04, gameplay agent)
Sources:
- RE MILESTONE05_PLAYTEST_RE (28debca) §3;
- RE's decompiled TnDominationPointBase, TnKingOfTheHillZoneBase, TnGameRules_ScoreKills / ScoreObjectives /
  ScoreKingOfTheHill / ScoreDomination / ReportGameProgressBase / Points, TnGameObjective;
- authored defaults (authored.db).

Test: `WFC_MODEPLAYTEST` **21 / 21** (DOM + KOTH + TDM afterwards); `WFC_TDMTEST` 39 / 39.

### Shared framework (one Match, not one engine per mode)
- **Launch.** `World::launchMatch` accepts TDM, DM, DOM and KOTH. CTF and EXT are refused because their rules are not
  implemented.
- **Per-mode settings** (`MatchSettings::forMode`, CONFIRMED authored):
  - DOM / KOTH: PointsToWin 400, TimeLimit 900;
  - `TeamScoreAmount` 0: ScoreKillsMP authors none, so in DOM / KOTH a kill is +1 personal and 0 team
    (`AddScore(1, TeamScoreAmount)`, CONFIRMED bytecode);
  - ReportGameProgressPoints instead of Kills.
- **Spawn clusters.** `ActiveGameTypes` filter (TNGT): the 6 TDM-only clusters don't register in DOM / KOTH.
- **Objective spawn modifiers.** Active KOTH zone All −50 / d within 5000; owned DOM node Friend +1 / d [CONF authored].
- **Objective scoring entry points.**
  - `Match::scoreObjective` (ScoreObjectives: `AddScore(IndividualScore, Score)`, then ReportGameProgressPoints at
    50 / 25 left).
  - `scoreTeamObjective` (DOM) [HIGH: the TnTeamGame override is not in the decompiled set; RE §3 states +1 team / 3 s].
  - `addPersonalScore` (DOM capture).
  - Each reaches the same score-limit end.
- **Objective membership.** The pawn inside the objective's authored ObjectiveVolume brush (physics.json TriggerVolume
  planes). TnGameObjective.PostBeginPlay -> `ObjectiveVolume.SetAssociatedActor` [CONF]; the volume forwards touches
  [HIGH stock UE3]; the cylinder-vs-brush overlap is approximated by the pawn location [PROV].

### Conquest (DOM) — CONFIRMED bytecode
- **TnDominationPointBase.Tick / UpdateOccupiers / UpdateScoring:**
  - with no occupants -> capture 0;
  - attackers = living occupants not on the defending team;
  - a neutral node is claimed as if owned by the other team;
  - with no defender present, capture += dt × attackers (restarted when the claiming team changes);
  - at **CaptureTime 20 s** -> SetTeam, **PersonalScoreAmount +2** for each capturer, timers reset,
    TnDominationMessage (switch + 10 × PointNumber);
  - an owned node -> **+1 team every 3 s** (ScoreInterval 3, ScoreAmount 1).
- **Verified:**
  - a solo capture is pending at 19.5 s and done at 20 s, +2 personal;
  - +1 team after 3 s;
  - a defender present holds progress;
  - an enemy alone recaptures in 20 s;
  - two attackers capture in 10 s;
  - the score-limit end;
  - 3 totems visible (DOM state from Pass 19).
- **PARTIAL.** No points announcements for DOM (it scores through ScoreTeamObjective, not ScoreObjective). The capturing
  announcement throttle (15 s) is not emitted.

### Power Struggle (KOTH) — CONFIRMED bytecode
- **Zones.** Inactive until **MatchStarting**, which picks the initial zone.
- **Active.Tick:**
  - UpdateClaim: one team, contested 254, or none 255;
  - every ScoreInterval 1 s, if uncontested and owned, each living pawn in the zone -> Game.ScoreObjective(PRI, 1),
    i.e. **+1 personal (IndividualScore 1) and +1 team**;
  - ActiveTimeLeft 60 s -> ActivateNewZone (unvisited cycle).
- **CheckEndGame.** Every zone deactivates at the end.
- **Verified:**
  - no zone before the start, one after it;
  - +5 / +5 over 5 s alone;
  - contested gives no score;
  - rotation to another zone after 60 s;
  - the end at the limit with zones deactivated.
- **PARTIAL.** The KOTH hill dialog / message switches are logged, not presented (Systems / Rendering).

### HUD
- `HudGameState::objectives`: marker type ("Domination" / "KingOfTheHill"), NodeID, owner (255 / 254), active, capture
  progress (NormalizedCaptureTime), BeingCaptured, KOTH time left, position.
- Match events add `PointsLeftAnnouncement` (switches 4 / 3).

### Not implemented (evidence present, mechanics missing in the rebuild)
- **CTF (Code of Power).** Rounds (TnGameRules_RoundsBase), the flag as a carried weapon, capture-point activation per
  attacking team, the mercy rule.
- **EXT (Countdown to Extinction).** The bomb as a carried weapon, plant / 15 s fuse / 5 s defuse, HurtRadius 9999.
- Both need a carried-objective weapon / inventory system first. Their map state (Pass 19) and the RE specs
  (RE PLAYTEST §3, GAMEPLAY_UNKNOWNS §4) are ready.

---

## PASS 21b — MATCH HUD STATE, KILL FEED, MATCH END, REGEN (2026-10-04, gameplay agent)
RE: MILESTONE05_PLAYTEST_RE (28debca) §3 / §9; TnDeathMessage decompile; authored LocalMessage / damage types.
Test: `WFC_TDMTEST` **39 / 39**.

### Kill feed (Gameplay owns the events; presentation owns text and colour)
- **CONFIRMED (TnDeathMessage.GetColoredString).**
  - Switch 1 -> `DamageType.SuicideMessage(victim)`, otherwise `DamageType.DeathMessage(killer, victim)`.
  - `\`k` / `\`o` take the killer / victim names, each coloured friendly / enemy for the viewer
    (TnMessageHelpers.GetColorForPRI).
- **HIGH (stock GameInfo.BroadcastDeathMessage).** Switch 1 when the killer is none or the victim itself.
- **Lifetime.** Engine.LocalMessage.Lifetime is 3.0 s; TnDeathMessage authors no override.
- **`KillFeedEntry`.** time, messageSwitch, killer / victim player, both teams, DamageType class:
  - `TransGame.TnDamageTypeIonBlaster` for Ion Blaster kills;
  - `Engine.DmgType_Suicided` for suicides;
  - `Engine.DmgType_Fell` for KillZ [HIGH: stock WorldInfo.KillZDamageType].
- `Match::killFeed()` returns the live entries, `killHistory()` the whole match; `HudGameState::killFeed`.

### HUD / match state (`World::hudState()`, no drawing)
- Added:
  - `spectating` (dead >= MinRespawnDelay 3.0 s, CONF RE E7);
  - `timeLimit`, `faction` (DM resolves every player to the Decepticon faction, CONF RE §3 / §7);
  - `endReason` ("Score" / "" / "Forfeit"), FFA `winnerPlayer` (an equal top score is a draw, CONF);
  - `matchOverTimeLeft` (15 s);
  - the kill feed;
  - scoreboard rows (name, team, score, kills, deaths, assists, alive, local).
- Existing fields are unchanged: health / segments / ammo / clock / countdown / scores / tags / result.
- **Verified.**
  - The clock does not run in PendingMatch.
  - The clock and score are frozen in MatchOver.
  - The second match starts from zero.
- **PARTIAL.** The FFA result text ("You won" / "You lost" / "Draw"): TnFreeForAllGameOverMessage strings were not read.

### Health regeneration — CONFIRMED (RE §9)
- 20 HP/s after 2.0 s without damage, up to the top of the current segment.
- The robot blueprint's parameters apply in both forms; the truck blueprint's 12 HP/s / 7 s is authored but unread.
- Applied to every live pawn.
- Test: 400 -> nothing for 2 s -> 425 (segment top), not 550.

---

## PASS 21a — M05 "INTERLACED" CHARACTER REGRESSION + PRE-MATCH PRESENTATION (2026-10-04, gameplay agent)

### Character / vehicle "interlacing" (human-reported M05 regression) — FIXED (owner: Gameplay)
- **Cause (mine, Pass 20).**
  - The third-person camera position was moved into the fixed 60 Hz simulation step and cached (`camLoc_`), while the
    camera rotation still updates per render frame (`handleInput`).
  - The integrated build renders at about 130 fps on the playtest machine (no swap interval is set), so most frames run
    0 simulation steps. Each of those frames paired a stale camera position with a fresh rotation.
  - The view swung around the pawn every frame, so the character and the truck appeared to separate and jitter.
  - The animation, the assets and the renderer were not involved: Character / SkinnedModel are unchanged since M04
    except `respawnReset`; dynamic meshes are drawn immediately, not through the new translucency queue.
- **Fix.**
  - The camera position is again evaluated per render frame from the current rotation and pawn location, exactly as
    before Pass 20.
  - The RE obstruction model (`WFC_CAMRE`) keeps its per-step smoothing state as a camera-space offset applied with the
    current frame's rotation.
- **WFC_CAMSYNC (render N Hz against the 60 Hz simulation, turning and moving).** Jitter of the character's on-screen
  offset per frame (mean |second difference|):

| scenario | 144 Hz per-tick cache (M05) | 144 Hz per-frame (fixed) | 75 Hz M05 / fixed |
|---|---|---|---|
| robot run + turn | 1.276° (max 1.91°) | **0.0003°** | 0.972° / 0.0002° |
| hover truck + turn | 1.189° (max 1.92°) | **0.011°** | 0.923° / 0.038° |
| boost | 0.649° (max 4.14°) | **0.023°** | 0.375° / 0.080° |

- VISUALLY VERIFIED: pending a human on the integrated build. The numeric cause and fix are confirmed.
- **Integration note.** No swap interval is set anywhere, so the frame rate is uncapped. Gameplay is correct at any
  rate now; frame pacing belongs to Frontend / Rendering.

### Pre-match presentation (human-reported) — FIXED
- **Integrated M05 capture** (countdown): no Optimus, but the Ion Blaster drawn floating at the world-load DM spawn,
  with the hidden pawn frozen mid-fall.
- **Original [CONF RE bootstrap §2 / §5.2].** `ShouldSpectateOnLogin`: there is no pawn before the start, and
  PendingMatch spawns nobody.
- **HIGH (stock UE3).** GameInfo.Login creates the controller at FindPlayerStart and it spectates from there.
- **Now.**
  - `World::startLocalMatch` takes the login start from the spawn manager (team start of the initial cluster; the
    SpawnIterator is consumed like the original).
  - The controller views from it at the start's rotation (`PlayerController::setSpectatorView`).
  - The pawn is parked at rest there, and neither the pawn, the weapon nor the vehicle FX are drawn while there is no
    pawn.
  - On the spawn the view returns to the third-person camera, and Optimus appears at his team start
    (TnTeamPlayerStart_*, initial cluster 7810 / 4159).
- **PARTIAL.** The death / spectate camera (Death strategy, MinRespawnDelay 3.0 s spectating) is not reproduced: the
  camera stays at the death location.

---

## PASS 20c — ADVERSARIAL MOVEMENT HARDENING (2026-10-03, gameplay agent)
- **WFC_CHAOS** (Phase 3): from 60 nav points, 20 s each of seeded random play through the real input path
  (71,940 ticks, 355 transform presses, 312 jumps, 307 boosts). Checks:
  - UNDER THE MAP: a walkable BSP (level-shell) floor 0.3–3 m above the pawn;
  - inside / under a prop;
  - KillZ;
  - stuck (< 0.5 m in 5 s with move input).
- **Robot knee probe [PROV].** 0.55 m height (above MaxStepHeight 0.35), 0.7 m reach, walkable faces skipped.
  - It stops the robot body walking into raised BSP blocks, crates and supports 0.6–2 m tall (no probe covered 0.35–2 m).
  - The short reach leaves stairs to the centre-point ground model: a 35° stair 0.7 m ahead is about 0.49 m high.
  - The native cylinder sweep with step-up is not reproduced.

| run | under the map | KillZ | stuck | inside / under a prop |
|---|---|---|---|---|
| before the knee probe | 8 (old detector) | 0 | 1 | — |
| knee probe 0.9 m | 1 | 0 | 2 | 7 |
| **knee probe 0.55 m** | **0** | **0** | **1** | **3** |

- **Regression.**
  - Authored ReachSpec oracle 852 / 852 (robot + vehicle).
  - Visible components crossed where the hull is smaller than the mesh: 33 (was 43 at the start of Pass 20).
  - TRAVERSE 160 runs, 0 falls.
  - WFC_TDMTEST 30 / 30.
  - VEHTEST unchanged; harness 179 / 2 known / 21.
- **Remaining reported locations.**
  - Robot wedged between BlockingVolume_6729 / _7015 and the TrainTrack at (221.6, −724.8, −441.3).
  - Three prop-entry cases.
  - Transform under a low overhang: 15 / 1520 stress cases (54 before the knee probe; RE question, PASS 20a). WFC_XFORMTEST: still 0 / 1520 under the map, 0 KillZ.

### Open RE requests (narrow)
1. `MoveToSafeTransformationLocation` / `FindSpotAwayFromPawns(target extent)`: does it resolve world geometry, and
   when does `NotifyCantTransform` fire? (Transform under a 1.95–2 m overhang.)
2. The native cylinder physWalking step-up / encroachment against simple hulls, to replace the robot probes.
3. The TraceCamera exact flag word and the AABB sweep result when starting inside geometry (camera model, PASS 20a).
4. Segmented-health regeneration rules; death animation / Death camera strategy timing.

---

## PASS 20b — MP_IAC_STREETS TDM SESSION RUNTIME (2026-10-03, gameplay agent)
RE: MILESTONE05_FRONTEND_MATCH_BOOTSTRAP §3, §5, §6 and MILESTONE05_GAMEPLAY_UNKNOWNS §3, §5, §6. Test:
`WFC_TDMTEST` — **30 / 30 checks** through World (real pawn, hitscan, pickups, map state).

### Launch contract (Frontend / Integration -> Gameplay)
- `MatchLaunch::fromURL` takes the original StartLevel URL, e.g.
  `MP_IAC_Streets_Base_m?…?GameModeTag=TDM?PointsToWin=40?TimeLimit=900.00…`. Missing keys keep the TnOnlineGameSettings
  defaults.
- `World::launchMatch(MatchLaunch)`:
  - validates the map: only MP_IAC_Streets is loaded, anything else is rejected;
  - applies the mode's authored world state (`MapState::setMode`, exact HasRule gates, Kismet UnHide);
  - resets the map as a fresh level load (map clock 0, objectives and KOTH re-initialised, destructibles to state 0,
    pickups back to Pickup);
  - starts the match.
- Runtime: `WFC_MATCH_URL=<url>` or `WFC_MATCH=TDM|DM`.
- Other modes set map state only; their match rules are not implemented (by design for this pass).
- **CONFIRMED.** A new match = MatchOver -> ReturnToGameLobby -> ServerTravel, i.e. a fresh level.

### Combat foundation
- **CONFIRMED.**
  - TnPlayerPawn.TakeDamage discards teammate damage except TnDamageTypeAOE.
  - Damage reaching the pawn enters its DamageHistory, used for ScoreAssists (first other damager, damage / HealthMax).
  - Lethal damage -> Game.Killed(instigator).
  - Segmented health 550 + overshield 550; the overshield part is consumed first.
  - Ion Blaster InstantHitDamage 15 with range falloff, unchanged.
- **Death / respawn (CONFIRMED / HIGH).**
  - The dead pawn is removed from simulation and drawing until RestartPlayer.
  - RestartPlayer = a fresh TnPlayerPawnMultiplayer: robot form, any fold cancelled, default vehicle state, HealthMax, no
    overshield, Ion Blaster 50 / 150 (InitialReserveAmmoCount), at the chosen start with its authored yaw, after the 5 s
    wave delay.
  - Death does not reset pickup timers; only StartMatch Resets factories.
- **UNKNOWN / PARTIAL.**
  - Segment regeneration.
  - Death animation / ragdoll and the Death camera strategy (the pawn is simply hidden).
  - PlayerRestartDelay (no script reader).
  - The downed state (TnSkillCanBeDowned) is not modelled.

### Test participants (separated from shipped behaviour)
- `MatchOpponent`: a static synthetic participant with a Match slot, the robot cylinder and segmented health. It
  spawns / dies through Match.
- It exists only under `WFC_TDMTEST` / `WFC_MATCHTEST` or the explicit diagnostic `WFC_MATCH_OPPONENTS=N` (drawn as
  boxes). No AI.

### HUD state output (no drawing)
- `World::hudState()` -> `HudGameState`, named after the original bindings:
  - health / HealthMax / overshield / active segment;
  - clip / reserve ammo;
  - form / transforming;
  - TimeToRespawn;
  - match state / GRI game status (2 / 3 / 5), pre-match countdown, RemainingTime / ElapsedTime;
  - GoalScore, team scores, TeamID, personal score / kills / deaths / assists;
  - Winner and the result text ("Your team won" / "Your team lost" / "Tie game").
- **TDM player tags** (TnObjectiveMarkerTypeTransformerVersus): hidden for self and the dead; allies labelled; enemy
  markers disabled without the reveal buffs [CONF RE §5].
- Match events (`World::matchEvents()`): countdown, start, spawn, kill, progress announcements (switches 0–2, 5–7),
  nearly complete, end, return.

### Map / mode state
- TDM:
  - totems, KOTH zones, flag / bomb factories and objective bases are hidden (the actors remain);
  - the 24 ordinary pickups (no rule gate authored) stay active;
  - TnTeamPlayerStart for the player's team;
  - ScoreKillsTDM present.
- KOTH rotation now follows RE §3 [CONF]: 60 s; candidates are zones not yet active this cycle; the cycle resets when all
  have been active; no back-to-back repeat.

### Validation (all on the corrected Streets data)
- WFC_TDMTEST 30 / 30.
- WFC_MATCHTEST: TDM to 40, clock tie at 125 s with announcements 120 / 60 / 30, DM to 20.
- WFC_MODETEST: KOTH visited cycle.
- Harness 179 / 2 known / 21.
- VEHTEST, TRAVERSE (160 runs, 0 falls), PICKUPTEST (30.02 / 60.02 / 119.99 s) unchanged.
- Firing sim 3.1 ms.

---

## PASS 20a — BOOST->ROBOT FALL-THROUGH, VEHICLE/ROBOT WALL PROBES, MATCH CORE, CAMERA (checkpoint) (2026-10-03, gameplay agent)
RE: TARGETED_PASS 1e, MILESTONE05_GAMEPLAY_UNKNOWNS (§1, §2, §6), MILESTONE05_FRONTEND_MATCH_BOOTSTRAP §5, MILESTONE04_CAMERA_COLLISION (990f3e7).

### Boost -> robot under-map (human-reported) — FIXED
- **Cause.**
  - The robot was given its full collision cylinder (half-height 2.0 m) on frame 0 of the vehicle->robot fold, from the
    shared actor location.
  - While boosting, the truck's wheels are down and its root is at floor level. The vehicle actor is only 1.22 m above
    the floor, so the robot's feet started 0.78 m inside the floor.
  - The airborne floor search only looked 0.35 m (MaxStepHeight) above the feet, so it missed that floor and the robot
    fell through.
  - In hover (root 1.24 m up) the feet start 0.46 m above the floor, which is why normal transforms worked.
- **Fix (CONFIRMED ORIGINAL semantics).**
  - TransformingToRobot.UpdateCylinderSize lerps the half-height from the vehicle's to the robot's by
    RemainingTimeAsFactor (1 -> 0).
  - The floor is resolved against that growing cylinder's bottom, so the floor pushes the actor up as the cylinder
    grows.
  - Visuals stay on the continuous actor location (the robot mesh hangs CollisionHeight below it), and the smooth
    transform is unchanged.
  - The vehicle cylinder half-height = the mesh bounds half-height, 1.22 m [HIGH].
- **Falling sweep [HIGH: UE3 physFalling MoveActor sweep].** While falling, the floor search starts from the previous
  step's bottom, so a floor crossed within one step is landed on.
- **Stress (WFC_XFORMTEST, 20 nav points x 4 headings x 19 cases).**
  - Cases: stationary, hover, max hover, boost, boost + nitro, boosted turn, boost jump, and a transform-time sweep
    during boost.
  - Before: 656 / 1520 under a floor, 186 below KillZ.
  - After: **0 / 1520 under the map, 0 KillZ**.
  - 54 end on a real floor under a low overhang (see the next item and UNKNOWN).

### Vehicle wall probe — FIXED (found by the stress test)
- The vehicle reused the robot wall probe at 2.0 m above its root. That is above the truck hull top (1.85 m), so the
  truck drove through obstacles lower than about 2 m: train-coach BlockingVolume_11128, Small1_Box2 crates.
- **Now:** three probes across the hull height (root −0.35 .. +1.85 m). The low probe sits above the wheel or spring
  clearance and ignores walkable faces, so ramps and floors don't block [PROV approximation of the RB box contact].
- **Robot head probe (3.6 m):** the 4 m cylinder no longer walks under overhangs lower than its top (for example an
  ArchTop01 hull 1.95 m above the floor). There is still no low probe: the centre-point ground model owns steps and
  stairs.
- **Results.**
  - Authored ReachSpec oracle: still **852 / 852** (robot + vehicle).
  - Visible components crossed where the hull is smaller than the mesh: 43 -> 36.
  - VEHTEST unchanged: rest 1.287 m, 0.5 m bumps at 15 m/s, jump +3.80 m, dash 30 -> 15.

### Segmented health and pickup acceptance (CONFIRMED ORIGINAL, RE §6)
- Health = TnSegmentedHealth (TR_Health_p.SharedHealth): segments [175, 125, 125, 125], so HealthMax 550; Overshield 550.
- Health pickup only below HealthMax (SHT_AddAllSegments, full heal). Overshield only while normalized overshield < 1
  (Health = HealthMax + 550). Ammo crate only when not AmmoMaxed.
- Pickup.ValidTouch rejects a touch through a wall (FastTrace) and re-checks after 0.5 s.
- Segment regeneration: **UNKNOWN** (not read; none applied).

### Local match core (src/game/Match.*, World::startLocalMatch) — RE bootstrap §5
- Launch-independent: `World::startLocalMatch(MatchSettings)`, opt-in with `WFC_MATCH=TDM|DM`. Free play is unchanged.
- **CONFIRMED.**
  - InitGame: GoalScore = PointsToWin (TDM 40, DM 20); TimeLimit 900 s (TimeLimits[1]).
  - PendingMatch: a 10 s countdown with no spawns, then StartMatch.
  - Factories Reset() at StartMatch.
  - InProgress: a 1 s GRI timer; time announcements at 120 / 60 / 30 s (60 -> GameNearlyComplete).
  - ScoreKills: +1 to the killer and +1 to the team. No score for a suicide (DmgType_Suicided or self) or an environmental
    death; deaths always count.
  - TrackKillsMP kills; ScoreAssists = first other damager, damage / HealthMax.
  - ReportGameProgressKills at 5 / 3 / 1 left.
  - CheckScore -> EndGame("Score"); the clock -> EndGame(""); a tie means no winner, with no overtime.
  - MatchOver for 15 s, ScoreKill a no-op, then ReturnToGameLobby (host handoff).
  - TnRespawnHelperWave: 5 s respawn, initial spawn immediate.
  - PickTeam: the smaller team; a tie is random.
- **Spawning (CONFIRMED).**
  - Initial clusters 4159 (Decepticon) and 7810 (Autobot); round-robin, up to 4 picks.
  - Initial lock 15 s, then re-scoring every 0.1 s; switch on a delta of 5 or 10 s uptime.
  - Modifiers: friend +1/d, enemy −3/d, tombstone −5/d within 5000 UU.
  - The cluster faction is the team of its first spawn point; authored _CenterPoint.
- **PARTIAL.** IsSafeSpawnLocation (native) is approximated as no live player within 4 m. The DM FFA start choice is the
  first safe FFA start. Objective / KOTH / DOM spawn modifiers are not registered in TDM. Tombstone lifetime.
- Tests: `WFC_MATCHTEST` (TDM to 40, TDM clock tie, DM to 20, local pawn spawn / suicide / 5 s respawn / return).

### Camera obstruction (RE 990f3e7) — implemented, opt-in
- TnThirdPersoncollision (robot: rise-then-horizontal fallback, 1 s smoothing window, 2000 / 500 UU/s) and
  TnAvoidClipping (vehicle: origin offset (−200, 0, 75), two ranked candidates, always smoothed).
  - The ray uses the zero-extent world, the box tests use the non-zero-extent world; movers are ignored.
  - The near-plane box sweeps are **approximated by rays [PARTIAL]**.
- **WFC_CAMTEST (15 nav points x 8 headings x 5 scenarios; % of frames).**

| model | camera behind visible geometry (robot / hover backing) | camera under a floor | near-plane contact | one-frame pops (robot) |
|---|---|---|---|---|
| default (provisional pull-in) | 9.6 / 11.0 % | 0.2–1.2 % | 0.8–2.5 % | 22 |
| RE algorithm | 16.8 / 24.5 % | 1.9–3.0 % | 2–6 % | 0 |

- The default stays the provisional pull-in, which the human likes. The RE model runs with `WFC_CAMRE=1` until the box
  sweep is exact.

### UNKNOWN / RE requests
- **Transform under a low overhang (54 stress cases).** The truck (hull top 1.85 m) fits under a 1.95–2.0 m slab, and the
  4 m robot then stands under it. RE §1 names `MoveToSafeTransformationLocation` -> `FindSpotAwayFromPawns(target
  extent)`. Whether that resolves world geometry, or the original refuses the transform (HUD `NotifyCantTransform`
  exists), needs a narrow RE trace.

---

## PASS 19 — MP_IAC_STREETS WORLD STATE + CORRECTED WORLD/COLLISION (AssetTools 8d8195e) (2026-10-03, gameplay agent)
Data: AssetTools 8d8195e. StaticMeshCollectionActor transforms were corrected (S·R·T × CachedParentToWorld; 1,906/1,906
validated, 0 stale collision matrices). world.glb, collision_pawn.glb, collision_weapon.glb, physics.json, props.json, map.json
and render_index.json were regenerated together.

Gameplay reads them at every start (collision is built from collision_pawn.glb and collision_weapon.glb at load; there is no
cache). Every measurement below ran on the corrected files. RE: MILESTONE04_STREETS_RUNTIME_SEMANTICS (0ab03b2) and
MILESTONE04_STREETS_PICKUP_OBJECTIVE_PRESENTATION (00dcb20). Tests: `WFC_MAPTRAVERSE=1`, `WFC_MODETEST=1`, `WFC_TRAVERSE=1`.

### Match mode / rule set (CONFIRMED ORIGINAL)
- A mode is its authored `TnOnlineGameSettings<tag>.Rules` list (authored.db). For example:
  - DM = ScoreKillsDM, TrackKillsMP, ReportGameProgressTime/Kills;
  - CTF adds SingleFlagCTF, ScoreFlags, ReportGameProgressTimeCTF;
  - EXT adds ScoreBombingRun;
  - DOM adds ScoreDomination + ReportGameProgressPoints;
  - KOTH adds ScoreKingOfTheHill.
- Every world-state gate is an exact rule-class match (`MapState::hasRule`). This covers the Kismet SeqCond_GameRuleActive UnHide of the 4 objective bases, the factories, capture/plant points, totems and KOTH zones. There is no combined or fake mode.
- `World::setMatchMode` is applied before load. Application takes it from `WFC_GAMEMODE` until a front end exists; DM by default.

### Presentation sync (Gameplay → renderer, every frame)
- `setActiveGameRules` (the authored class paths), `setMapClock` (MapState clock), `setActorHidden` and `setMapEffectState`:
  - **setActorHidden** covers the 4 bases, 3 totems, 5 KOTH zones and 3 objective factories.
  - **setMapEffectState** covers the 24 pickup factories (custom effect / highlight beam) and the flag/bomb beam (Pickup = on; Disabled = hidden).
- The three map-state hooks mirror the Rendering lane's interface verbatim.
- `setMapClock` is new. Rendering currently animates movers from its own wall clock, so the drawn domes can drift from the moving collision Gameplay simulates.
  - **HANDOFF:** evaluate movers, totems and KOTH at `setMapClock` time.

### Movers
- Domes: PHYS_Rotating at 2730 UU/s, unchanged (CONFIRMED).
- **SkyBeam (corrected):**
  - The track has `bUseQuatInterpolation`, so the rotation is SlerpQuat between the bracketing Euler keys with a linear alpha (UE3 GetKeyTransformAtTime). Pass 17 interpolated Euler angles on CurveAuto tangents.
  - IMF_RelativeToInitial uses InitialTM = authored Rotation only. Pass 17 conjugated by the scaled authored matrix, which distorted the delta for the non-uniform DrawScale3D.
  - Check: InitialRot reproduces the world.glb placement of all three actors (residual 0.00000).
  - StaticInterpActor_5249: 0° → 11.35° (2.25 s) → 22.70° (4.5 s) → 0° (9 s).
- Moving collision: domes 15810/7381/8114 and SkyBeam dome 5249 (the cones and bases carry none, as authored).

### Objectives
Same table as PASS 18, now rule-gated, plus:
- **KOTH:** the Active zone rotates after the authored `ZoneActiveTime` 60 s (TnKingOfTheHillZoneBase CDO) [HIGH: authored constant and the ActiveTimeLeft field; the timer body is not traced].
- **KOTH zone visual:** `ActiveMesh` (FX_Mesh_p pTorus1_STAT) is shown only while the zone is Active. Gameplay pushes hidden state; drawing the torus is a Rendering handoff.
- **Objective cylinders** (totem, zones, factories) have no authored blocking flags, so no blockers are added. Hidden totems keep a non-blocking cylinder.

### Player start
- Start class follows the mode: TnFreeForAllGame (DM) → TnFreeForAllPlayerStart; TnVersusGame modes → TnTeamPlayerStart [HIGH].
- The spawn yaw is the start's authored Rotation. The invented face-the-centroid heuristic is removed.
- Which start TnSpawnPointManager picks (cluster scoring, InitialSpawns) is UNKNOWN: index 0 [PROV].

### Collision sources audited
- **TnForcedDirVolume ×4:** PhysicsVolume subclass whose CDO has bBlockActors and COLLIDE_BlockNonZeroExtent.
  - Streets instances: bBlockPawns, ArrowDirection (0,0,−1), ExitSpeed 1500, at UE Z −64000 (about 80 m above the floor).
  - These are sky caps, correctly included as pawn blockers. The push script is not traced.
- **Objective bases:** absent from both collision GLBs (authored CollideActors false) — correct.
- **"No pawn collision" props:** 260 visible props have pawn collision "none" because they author BlockNonZeroExtent = false (pawns pass, weapons blocked). Examples: 45 GS_SupportB columns, Building_ONE_Middle ×7, OmegaWall bases, the wrecked tank. This is authored; BlockingVolumes are the pawn blockers there.
- **No test geometry in normal play:** DamageTarget only with WFC_TESTDUMMY. Debug boxes appear only with B / WFC_DEBUGCAM or the no-assets graybox fallback.

### Traversal against authored data (WFC_MAPTRAVERSE, corrected data)
| check | result |
|---|---|
| Nav points (123) on floor, inside bounds, above KillZ | 123/123 |
| **ORACLE:** all 426 authored TnReachSpecs (R_WALK, path sizes 250–1210 UU), walked as robot + driven as hover truck | **852/852 arrived, 0 falls, 0 floor gaps** (0.5 m samples) |
| TOUR through all 123 nav points (coverage, not an oracle; y −727 … −699 m, ~1.7–1.9 km per form) | robot 100/122, vehicle 103/122 legs; every blocked leg stops at visible geometry or an authored volume and is logged with actor names |
| Boost + jump sweep, robot run + jump, 123 points × 4 headings | 984 runs, 0 below KillZ, 0 outside the collision bounds |
| Transform robot → vehicle → robot after settling (984) | 960 normal; 24 height changes > 0.6 m (see KNOWN DIFFERENCES) |
| Visible components crossed by the pawn | 49 authored no-pawn-collision; 49 crossings outside the component's authored hull (the visual mesh extends past the simple hull); **0 missing / displaced collision** |
| Point-in-convex-hull re-check of the crossings (collision_pawn.glb triangles, per convex piece) | all outside their hull except one hover-truck edge graze (13 cm, Small1_Box2 StaticMeshCollectionActor_12707 at (137.9, −713.2, −630.3)) |
| BSP | render and collision triangles identical (2460); one ramp face at (134.0, −717.7, −426.7) crossed by the chest segment 3× (movement edge case on a 27° ramp, not data) |

### Visual vs physical disagreement (authored, reported)
- **Interior room walls** (Wall_Base_Straight / Wall_Top_Straight / Corner2, ENV_IAC_Interior_1_p):
  - The authored collision box matches the render bounds except a strip about 0.7 m deep on one face.
  - The robot's chest reaches into that lip, and the 3rd-person camera (0.3 m in front of collision) can sit inside it.
  - Same matrix for render and collision (8d8195e validation): authored BodySetup, not an export defect.
- **Large props whose simple hull is smaller than the mesh** (crossing distance outside the hull):
  - bld_2048x4096x4096_thru 19.6 m;
  - Wall_Base_Corner2 11.0 m;
  - PROP_IAC_SideSupp01 6.4 m;
  - TrainCoach_Open 2.8 m;
  - craterDebris 2.2 m;
  - SpireBase / PillarBuilding / GiantPillar 0.1–1.4 m.
- **Camera** (UNKNOWN original camera trace extent): over 308k frames, the camera segment crossed visible geometry on 72 components authored BlockCameras and 51 authored camera-transparent ones. The camera traces the pawn collision world [PROV].

---

## PASS 18 — BOOST STEERING + STREETS MODE STATE (RE a1666c2 / 0ab03b2) (2026-10-03, gameplay agent)
Sources: `RE-Workspace/notes/MILESTONE03_VEHICLE_BOOST_STEERING.md` (RE a1666c2) and
`MILESTONE04_STREETS_RUNTIME_SEMANTICS.md` (RE 0ab03b2). Measured with `WFC_VEHTEST=1` and `WFC_MODETEST=1`.

### Boost steering — recovered control logic (CONFIRMED ORIGINAL)
- **Steering source:** TnPlayerInput.GetNormalizedTurn = aTurn (XboxTypeS_RightX).
  - HmPlayerInput radial deadzone 0.25 over (aTurn, aLookUp), rescaled (|v| − 0.25)/0.75; no temporal filter.
  - Driving.UpdateSimulationInputs: Steering = sign(s)·s²; × SteeringScale (Nitro 0.3 for 3 s).
- **Left stick X:** RollControl only. The truck cannot barrel roll (RollDuration 0); it drives UpdateLeveling (|RollControl| > 0.1).
- **Camera:** in boost the camera yaw follows the truck's yaw (TnDrivingOrbitRotation), OrbitSmoother 0.25 s. The right stick does not rotate the camera.
- **Wheel/tire laws:**
  - front wheels steer up to 25°, rear 0;
  - per wheel, F = clamp(−v_lateral(wheel frame) · 0.0015 · Load, ±2·(M/4)|g|), applied along body +Y at the wheel;
  - yaw comes only from the torque (inertia 58.9e6);
  - ground angular damping 5·(1−|s|)²;
  - air control 12 rad/s² / 2600 unchanged.
- **Removed:** the provisional fixed yaw rate (180°/s at full lock, instant) and the 8 s⁻¹ lateral grip.

### HIGH CONFIDENCE
- Static per-wheel Load = (M/4)·|g| (no load transfer). Wheel positions relative to the COM: axles ±130 UU, track ±126 front / ±137 rear.

### PROVISIONAL
- No load transfer and no wheel suspension: the contact point is treated at ground level.
- Tire roll torque is not applied (the body settles on its wheels).
- The PhysX damping integration form `ω *= max(0, 1 − c·dt)` is assumed (UNKNOWN in RE).
- The root-vs-COM velocity offset is ignored.

### PC input translation [PROV]
| Xbox path | Original role in boost | PC |
|---|---|---|
| Right stick X (aTurn) | boost steering (camera yaw in hover) | **mouse X** (the PC camera-yaw axis): mouse rate / 1200 px/s = stick deflection, 0.05 s rate average, no deadzone |
| Left stick X (aStrafe) | RollControl only (hover: strafe) | **A/D** (unchanged): no steering in boost |
| LT | Boost | right mouse button |
| RB | VehicleSpecialMove (hover dash / Nitro) | Shift |
| Pad present | radial 0.25 deadzone on the right stick, no filter | — |

### Measured (WFC_VEHTEST, from straight-line speed)
| u0 (m/s) | input | yaw rate 0.1/0.25/0.5/1/3 s (°/s) | slip @1 s | RE model |
|---|---|---|---|---|
| 30 | 1.0 | 53.8/115.1/146.6/146.6/122.0 | 51° | 48/101/137/108/98, 36° |
| 30 | 0.5 | 12.3/22.3/28.6/30.5/32.5 | 6.3° | 12/21/27/28/27, 6° |
| 30 | 0.25 | 2.9/4.8/5.7/5.9/5.9 | 1.1° | — |
| 10 | 1.0 | 20.6/51.5/95.5/140.4/121.5 | 26° | 18/46/82/112/97, 20° |
| 30 | 1.0 Nitro | 15.3/29.2/40.3/47.1/49.4 | 9.7° | (0.3× steering) |

- The RE table is MODELLED (a planar sim of the same laws), not recovered. The rebuild keeps boost acceleration and drag active during the turn, which raises the full-lock rates.
- Raw stick → steering: 0.3 → 0.004, 0.5 → 0.111, 0.75 → 0.444, 1.0 → 1.0 (deadzone + square).
- Stick release: 146.6 → 28.7 °/s in 0.25 s → 0.1 in 1 s (damping + aligning; no snap).
- Left stick only: 0.01° in 1 s. Boost release → Hovering, drift 0.5 s, yaw back on the view.
- Hover, jump, dash, suspension and boost speed are unchanged (same VEHTEST values as PASS 14).

### Streets mode state (CONFIRMED ORIGINAL; RE 0ab03b2)
| actor | CTF | EXT | DOM (Conquest) | KOTH | DM / TDM |
|---|---|---|---|---|---|
| 4 objective bases | shown | shown | hidden | hidden | hidden |
| Flag factories ×2 | Active (+ "Flag" marker) | Disabled | Disabled | Disabled | Disabled |
| Bomb factory | Disabled | Active (+ "Bomb") | Disabled | Disabled | Disabled |
| FlagCapturePoint ×2 | Active (marker only while _Active) | inert | inert | inert | inert |
| BombPlantPoint ×2 | inert | Active, marker added (display needs AttackingTeam) | inert | inert | inert |
| Domination totems ×3 | hidden (collision kept, touch ignored) | hidden | **visible, Active, "Domination" marker** | hidden | hidden |
| KOTH zones ×5 | hidden | hidden | hidden | 1 random Active (shown, "KingOfTheHill"), rest Inactive | hidden |

- Disabled = SetHidden + SetCollision(false,false).
- The totem idle animation (DeactivatedLoopAnim) runs in every mode (`animClock`).
- Default mode is DM (WFC_GAMEMODE selects).
- **UNKNOWN:** what triggers KOTH rotation (`activateNewKothZone()` API only), the FlagCapturePoint `_Active` driver, the flag/bomb factory marker add timing **[PROV]**, and the friendly/enemy/contested marker presentation (HUD movie side).

### Objective marker handoff
`MapState::objectives()` carries:
- the hard-coded marker class and type string ("Domination", "KingOfTheHill", "BombPlantPoint", "FlagCapturePoint", plus factory "Flag" / "Bomb");
- the authored MarkerString;
- markerAdded (mode gate + state) and markerShouldDisplay (per-type rules).

This feeds the future `_global.UpdateMarker(id, dist, sx, sy, sz, type, desc)` path. No HUD was built.

---

## PASS 17 — MILESTONE 04 STREETS WORLD STATE, AssetTools a23c675 (2026-10-03, gameplay agent)

| Item | Authored evidence | Conf | Rebuild |
|---|---|---|---|
| Collision worlds | collision_pawn.glb (non-zero extent: BSP, 71 BlockingVolumes, 4 TnForcedDirVolumes, authored simple hulls) / collision_weapon.glb (zero extent: 34 weapon-blocking volumes) | CONFIRMED (flags/geometry), HIGH (UE3 rules) | **APPLIED**: movement 101k tris (was collision.glb render geometry, 1.85M), hitscan / line checks / visibility on the weapon world |
| KillZ | BASE TnWorldInfo KillZ −75000 UU | CONFIRMED | −750 m (was collision bounds − 25 m) |
| Truck hull | VH_Optimus_PHYSSYS box x −310..338, y ±154, z −35..185 UU | CONFIRMED | Replaces the PROV wall-probe radius (1.75 m → hull extent along the travel direction), minimum clearance (0.6 m → hull bottom −0.35 m) and top (2.44 m mesh bounds → 1.85 m) |
| Rotating domes | StaticInterpActor_15810/7381/8114 PHYS_Rotating Yaw 2730 UU/s (15°/s), collide + block | CONFIRMED | `MapState` movers: world-space pose about the pivot; triangles split into moving collision sets (pawn + weapon) |
| SkyBeam | GameplayStarted → "StartBeam" → SeqAct_Interp_3464 (loop 9.0022 s), EulerTrack CurveAuto, IMF_RelativeToInitial, on 5249 (collides) / 13497 / 10471 | CONFIRMED (data), HIGH (Euler vs quat interpolation, ≤20°) | Same clock as the domes. `worldDelta` per mover for Rendering. 5249 has moving collision. PosTrack (≤0.008 UU) not applied |
| Objective bases | 4 InterpActors bHidden, UnHide via SeqCond_GameRuleActive CTF / EXT, non-colliding | CONFIRMED | `MapState::modeVisibleActors()`: hidden in DM (default) / TDM / KOTH / DOM, visible in CTF / EXT (WFC_GAMEMODE) |
| Objectives / HUD signals | Flag/bomb factories, capture/plant points, domination points, KOTH zones; MarkerType / MarkerString / RequiredGameRule | CONFIRMED (future_hud_handoff) | `MapState::objectives()` with marker fields and activeInMode. No scoring |
| Wall panel collision | Base (intact) / Chunk02 (destroyed, settled) pieces | CONFIRMED (meshes/states) / PROV (per-poly, hulls not extracted) | Moving sets switched by state. No authored reset (state 2 terminal) |
| Player starts | 84 (60 team + 24 FFA), 12 clusters | CONFIRMED | WFC_START / WFC_START_ACTOR select a start; F6/F7 cycle them (test only, not a WFC binding) |
| Test dummy | — (rebuild instrumentation) | — | Only with WFC_TESTDUMMY=1 |

**Traversal (WFC_TRAVERSE=1, fixed 60 Hz):**
- 20 starts (one per cluster plus a spread) × robot and vehicle × 4 headings: 160 runs, 0 falls below KillZ, 0 snags. Every short run was against a wall within 4.5 m.
- 32 transforms at the run end points with no fall-through.

---

## PASS 16 — RUNTIME SEMANTICS, RE d50e2a9 (2026-10-02, gameplay agent)
Source: `RE-Workspace/notes/MILESTONE03_RUNTIME_SEMANTICS_ASSETTOOLS_7a69756.md` (RE commit d50e2a9). It corrects
AssetTools §2: TnPickupFactory SetPickupVisible/Hidden, IsReadyToPickup, GiveTo, TakePickUp and GetRespawnTime have bytecode.
Measured with `WFC_PICKUPTEST=1` (slice world, fixed 60 Hz), `WFC_HUDLOG=1` and the frame log.

| Item | Native/script (d50e2a9) | Conf | Rebuild | Measured |
|---|---|---|---|---|
| Factory states | 'Pickup' (visible, ammo crate PHYS_Rotating Yaw 10000) → valid Touch → GiveTo → the same frame enters 'Sleeping' (SetPickupHidden); collision kept, touches ignored; exactly RespawnTime; → 'Pickup' (SetPickupVisible). Actor never destroyed. Availability = !bPickupHidden | CONFIRMED | **APPLIED** | Ammo 30.02 s, health 60.02 s, overshield 119.99 s. Overlap while sleeping ignored |
| Touch semantics | Touch = overlap begin. TnHealthPickupFactory.SetPickupVisible → CheckTouching | CONFIRMED | **APPLIED** (was a per-tick overlap test) | Standing on the factory at respawn: health re-taken at once; ammo/overshield not re-taken until a new touch |
| Sounds | PickupSound plays on the receiving pawn (AnnouncePickup); no respawn effect/sound (RespawnEffectTime 0) | CONFIRMED | Event `receiverPos` + sound on Taken only | — |
| Highlight beam | PickupEffect.ActivateSystem in SetPickupVisible / DeactivateSystem in SetPickupHidden, only if ShouldDisplayHighlightFx (true only for TnAmmoCratePickupFactory) | CONFIRMED | `beamActive()` / event `beamActive` | Ammo beam 1 while available; health/overshield beam 0, custom FX 1 while available |
| ValidTouch / PickupQuery | ValidTouch: !bHidden, controller, line of sight; TnGame.PickupQuery not traced | PARTIAL | "nothing to gain" rejection stays **[PROV]** | — |
| HUD spread | NotifyWeaponSpreadChanged(raw), sent when it changes by > 0.002; raw = CurrentSpread × CurrentAirborneMultiplier × (fine aim ? 0.5 : 1) + Data.Spread (0) | CONFIRMED | `hudNotifies()` per HUD tick; `Character::effectiveSpread()` also drives the hitscan cone | Fine aim 0.080→0.040. Filter verified (WFC_HUDLOG) |
| Weapon/aim notify | NotifyCurrentWeaponChanged(class) + NotifyFineAimChanged(0/1), sent together when either changes | CONFIRMED | **APPLIED** | — |
| Spread model | IncrementSpread +0.005/shot; CooldownSpread every tick −(Max−Min)·dt/Cooldown (whole range in 2 s); Ion Blaster MP 0.08–0.18 | CONFIRMED | **APPLIED** (was "snap to Min after 2 s idle") | 10-shot burst 0.095 → back to 0.080 in ~0.3 s |
| Airborne | TnWeaponSpreadModifier AirborneMultiplier 2.0, ramp up 0.25 s, land ramp down 0.5 s (hover 0.1 s n/a) | CONFIRMED values / HIGH linear ramp | **APPLIED** (robot form) | Jump: 0.080 → 0.160 in 0.25 s; back over 0.5 s after landing |

**Harness note (Experimental):**
- `weapon.spread_after_10` (expects 0.13) and `weapon.spread_cap` (expects 0.18 after 2.5 s of fire) encode the superseded no-recovery-while-firing model.
- Under per-tick CooldownSpread at 15 shots/s the net bloom is +0.025/s: 0.10 after 10 shots, and the cap is reached after ~4 s.
- The check expectations need updating to d50e2a9; Gameplay did not edit the harness.

---

## PASS 15 — AUTHORED-DATA HANDOFF, AssetTools 7a69756 (2026-10-02, gameplay agent)
Sources: `AssetTools/manifests/fineaim_hud.json`, `streets_pickup_factories.json`, `streets_pickup_fx.json`,
`streets_destructibles.json` (commit 7a69756). Placement data comes from the slice's existing `gameplay.json` and
`physics.json` (no new extraction). Measurements come from `WFC_PICKUPTEST=1`, which runs the loaded slice world
at the fixed 60 Hz step.

| Item | Authored evidence | Conf | Rebuild |
|---|---|---|---|
| Ion Blaster fine-aim presentation | No special reticle or scope. HasFineAimScope unset (false); NotifyFineAimChanged shows scopes only for HeavyPistol/BurstRifle/SniperRifle; mc_crosshairIonBlaster stays in both aim states | CONFIRMED AUTHORED DATA | No scope/ADS asset is expected. `PlayerController::hudAimState()` exposes weaponClass (TnWeaponIonBlaster), EHudAimType (0/1), spread, crosshairVisible and TTFH_None |
| Fine-aim visible change | Prongs move to spread × 300 px (eased 0.2 s); FineAimSpreadModifier 0.5; PerShotSpreadModifier 0.08–0.18, +0.005/shot, cooldown 2 | CONFIRMED (HUD/data) / HIGH (native spread combination) | hudAimState.spread = bloom × 0.5 in fine aim. Measured 0.105→0.150 while firing; 0.090 at the cap in fine aim |
| Camera in fine aim | TnPCS_FineAim (no authored props) | — | Unchanged native camera (PASS 14): FOV 45, orbit-space offset |
| Pickup factories | 14 TnAmmoCrate (RespawnTime 30), 9 TnHealth (60), 1 TnOverShield (120). Touch cylinder r200/h100, COLLIDE_TouchAll | CONFIRMED AUTHORED DATA | `PickupFactory` actors at the authored gameplay.json placements. The graybox near-spawn pickups are removed |
| Objective factories | Flag ×2 / Bomb ×1, RequiredGameRuleClass CTF / BombingRun | CONFIRMED AUTHORED DATA | Not instanced: those modes are out of scope |
| Payloads | Health AddedHealth 50; AmmoCrate ValidWeaponTypes Primary/Secondary/Vehicle; OverShield no authored amount | CONFIRMED (health, types) / native (amounts) | Health +50. Ammo refills the reserve to MaxAmmoCount **[PROV amount]**. Overshield sets a granted flag only **[PARTIAL]** |
| Factory states | Pickup ↔ Sleeping; SeqEvent_PickupStatusChange; TakePickUp/GiveTo/ValidTouch native | CONFIRMED (states/events) / native (bodies) | One PickupEvent per transition (Taken/Respawned, available flag, authored PickupSound). A pawn with nothing to gain does not consume **[PROV ValidTouch]** |
| Pickup FX/meshes | Health/OverShield CustomPickupEffect auto-active while available; ammo crate mesh + inactive Pickup_FX | CONFIRMED / HIGH | Not drawn by Gameplay. Rendering/Systems consume `pickupFactories()` / `pickupEvents()` |
| Wall panel | TnStaticDestructibleActor_14465, WallPanelSign: state 0 health 20 → 1 (damage/touch/kismet) → 2 after 10 s. No damaged state. Initial state 0 | CONFIRMED AUTHORED DATA (initial state HIGH) | `Destructible` at its authored location (8.96, −3.52, 899.68 m) with the Base-piece damage/touch box. One DestructibleEvent per transition; meshes/FX/cues stay with Rendering/Systems |
| Wall panel placement | ~1400 m from the player starts, only actor above Z −50000 | CONFIRMED (positions) | Kept authored. Its absence from the playable view is not a reconstruction failure |

Measured with WFC_PICKUPTEST:
- **Ammo crate:** taken once (reserve 10→250), respawned after 30.02 s.
- **Health:** taken once (30→80), respawned after 60.02 s.
- **Overshield:** taken once (grant 0→1), respawned after 119.99 s.
- **Events:** exactly one Taken and one Respawned per cycle. Full health/ammo leaves the pickup available.
- **Wall panel:** 15+15 damage → destroyed → settled 10.00 s later, position unchanged.

**Superseded by 7a69756** (kept in older rows for history):
- "missing Ion Blaster ADS scope/reticle" and the ADS/spread-visualization TODO: there is no ADS scope; the crosshair + spread is the presentation.
- FIDELITY PASS 11 robot-camera "shoulder offset ... PROV semantics" row: resolved in PASS 13/14.
- "missing static destructible / visible destructible geometry" (harness KNOWN `missing.static_destructibles`, PLAYTEST-01): the single placed instance is authored far outside the play space. Experimental should retire that KNOWN.
- STATUS "Footsteps deferred (no clear footstep asset)": superseded. Streets surface audio is recovered (AssetTools 7a69756) and owned by Systems.
- Graybox pickup scaffold ("pickups near spawn for visual life"): replaced by the authored factories.

---

## PASS 14 — NATIVE RE MILESTONE 03 RECONCILE (2026-10-02, gameplay agent)
Source: `RE-Workspace/notes/MILESTONE03_VEHICLE_NATIVE_FIDELITY.md` (native RE 76bb0a). Measurements from
`WFC_VEHTEST=1`, which runs the real 60 Hz vehicle step on generated geometry (src/game/VehicleTests.cpp).

| Item | Native report | Rebuild | Measured |
|---|---|---|---|
| P1 suspension | 4 COM-relative probes ±130.8, body-down rays 250, implicit spring K/m, B/m, m=M/4, g = −dir.Z·GetGravityZ (RB −1940.4), push-only, cos-scaled, RB damping 0 | **APPLIED**; spring gravity corrected from world −2940 to RB −1940.4 | Rest COM 1.2872 m = native L_eq 128.7 UU (mass link M=2500 stays **PARTIAL** per report) |
| P1 bumps / drop | — | springs only, no ride-height target | 0.25 m step @15 m/s: COM 1.075–1.575 m, pitch −1.8..4.0°; 0.5 m: 0.863–1.857 m, −3.0..8.1°; 10 m drop: impact 17.1 m/s, min COM 0.60 m (PROV hull clearance), settles 1.287 m |
| P2 attitude | grounded pitch/roll = springs + UpdateRoll; yaw = camera each tick; upright 5%/tick only airborne/upside down | **APPLIED** (Pass 13) | — |
| P2 visual lean | TnAccelerationAnimBlend: m = ClampLength(v,2000)/2000 × max(0,up.Z); child0 = 1−|m|, dirs max(0,±sign·m²/|m|) | **APPLIED** (was velocity/1500 per axis PROV); ADD_Nav_Hover_VEH additive kept | — |
| P3 hover velocity | local X/Y toward stick×1500, one ClampLength 3000·(1−drift/0.5)²·up.Z²; no hover grip model | **APPLIED** | Coast-down 15→0 m/s in 0.500 s forward and sideways |
| P3 boost tires | F = clamp(−v_lat·coeff·scale·Load, ±2(M/4)|g|) | cap **APPLIED**; coefficient **PROV** (not recovered) | — |
| P3 drift turn | heading = camera; authority ramps | **APPLIED** | Camera +90° after boost release: yaw 90° at once, travel heading 0.2° @0.15 s → 11.9° @0.6 s |
| P4 hover jump | +1200 world Z additive, local ω −1, 0.3 s ground cooldown | **APPLIED** | vy +12.00, apex +3.80 m over rest (ballistic 3.71 + spring push), horizontal kept |
| P4 boost jump | local (600,0,1400), ω (0,−2,0) | **APPLIED** | +6.0 fwd, +14 up (13.68 after one tick of g), pitch rate 1.8 rad/s after air damping |
| P5 dash | body-local (1,0,0); mask (1,1,1); 100000; exit snap fwd 1500; refuse/cancel unstable | **APPLIED** | Stick right ignored: tick 2 = 29.8 fwd / 0.08 lat (one 30 Hz tick = two 60 Hz ticks), 30.0 during, exit 15.0 fwd |
| P6 camera offset | orbit-space translation, full camera rotation, X toward pawn, Y right, Z up; CurveAutoClamped cubic; C2 smoother (T/2) | **APPLIED**; curve now Hermite with flat end/extremum tangents | Fine aim at level pitch: camera 7.35 → 9.16 m from the actor (X +150 → −50), no lateral change |
| P7 hand | HandSkelControl R_Arm04_Hand_XB scale 0.1, strength 0/1 instant; ShouldEquipHand rules | **APPLIED** | Shrunk with the gun drawn; full size during R→V and V→R before the restore; shrinks on the restore tick |
| P8 ram | TnPawn victims only (mass ≤ 1000, other team); robot RammedReaction (falling, dir·5000+base for 0.5 s, then (0,0,baseZ)); vehicle AddVelocity ×0.5 | victim rule **APPLIED** (the DamageTarget dummy is not a TnPawn and is no longer rammed); robot reaction + vehicle AddVelocity implemented | WFC_RAMSELF: 50 m/s + base for 0.5 s, horizontal 0 after. The slice has no pawn victims; masses **PARTIAL** |
| P9/P10 visibility | notifies 0.8796 / 0.3958 / 0.0984 / 0.6634 s, ÷ Rate (4 downed), final state at BeginState | **APPLIED** (exact times, Rate constant 1; no downed state) | Vehicle hidden from 0.6634 s; clip geometry untouched |

---

## PASS 13 — VEHICLE BODY, VEHICLE CAMERA, TRANSFORM HANDOFF (2026-10-02, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Hover support | 4 TnSuspension rays from COM + Normal(1,1,0)×185 at 90° steps, along body −Z, length 250; TnSpring implicit (K=10000/m, B=4000/m, m=Mass/4); force along body up × Dot(up,N) | TnHoverCarSimulation.UpdateSuspension/CalculateSuspensionLocation/InitializeSuspension, TnSpring.Update/CalculateSpringVelocity/Reset, HoverTruck_Suspension | CONF | **APPLIED**; COM 1.36 m (was a fixed 1.85 m ride height = misread SuspensionRadius) |
| Body mass/COM/inertia | 2500; (−47,0,20)−(0,0,15); 2.27e7/4.64e7/5.89e7 | TnCarSimulation.InitializeFromBlueprint, Truck_Physics, OptimusTruckForm.ChassisOffset | CONF | **APPLIED** |
| Roll/pitch | UpdateRoll: local angular accel X = RightLeft − ωz; uprighting 0.05/tick only with no contact or upside down; damping 0 | UpdateRoll/UpdateTurn/Activate | CONF (sign HIGH; per-tick factor at 30 Hz PROV) | **APPLIED** |
| Vehicle jump | Hovering: on ground (N.Z>0.707), interval 0.3 s, +1200 Z, ω(0,−1,0). Driving: local (600,0,1400), ω(0,−2,0), air control 2600/12, pitch-forward −25°/3 | Hovering/Driving.UpdateJumping, TnHoverCarSimulation/TnCarSimulation.Jump, UpdateAirControl | CONF | **APPLIED** |
| Hover dash | Truck: forward only; refused if unstable; local all-axis strafe to 3000 then one-tick decel to 1500 | TnTruckForm.Hovering.DoDash, UpdateDash | CONF | **APPLIED** (Pass 12 dominant-axis superseded) |
| Boost acceleration | Lerp(2500, Drag(Max), v/Max) + ExtraBoost (8×, to 0.5·Max, ≤5000) × BoostScale; Drag = v²·g_RB/6000² | UpdateBoost/CalculateExtraBoostAcceleration/CalculateDragAcceleration | CONF | **APPLIED** (tire steering PROV) |
| Driving exit on impact | Frontal contact (N·fwd > 0.866) → Hovering | Driving.OnRigidBodyCollision | CONF | **APPLIED** (normal approximated by blocked travel) |
| Driving steering | Steering = sign(s)·s² of GetNormalizedTurn (look X) | PlayerInCarForm.SetLocalInputs, Driving.UpdateSimulationInputs | CONF input / PROV yaw rate | **APPLIED** |
| Vehicle camera | HoverTruck: anchor 185, orbit 950, FOV 80, pitch −20..30, orbit smoother 0.1, offset Z (45,0,120) over ±25°. Truck: 215/1050/85, nitro 100 & 650, yaw = pawn, pitch chase 3/s, smoother 0.25 | CAM_Driving_Strategies_p, TnDrivingOrbitRotationCameraBehavior, HmOrbitSmootherCameraBehavior | CONF | **APPLIED** |
| Hover yaw source | Controller rotation = camera rotation (after smoothing) | PlayerInVehicleForm.PlayerMove | CONF | **APPLIED** |
| Camera smoothing | HmC2Smoother: ω = 4/SmoothTime, Padé exp | HM_Engine bytecode | CONF | **APPLIED** (FOV, offsets, rotation) |
| Robot camera offset | Offsets[3] vectors: (150,300,150) (150,300,−35) (150,300,150); FineAim (−50,300,80)/(−50,300,−35)/(−50,300,80) | raw property data (static array) | CONF values / interpolation PROV | **APPLIED** (Pass 11 lateral-only reading superseded) |
| Strategy blend | TransitionTime of the new strategy: OTS 1.5, Hover 1.5, Truck 1.0 | strategy objects | CONF / blend curve PROV | **APPLIED** |
| Transform visibility | ToVeh: robot hide 0.880, vehicle unhide 0.396; ToRobot: robot unhide 0.098, vehicle hide 0.663 | AssetTools notifies (EVIDENCE_TARGETED_PASS 5a–5d) | CONF | **APPLIED**, both meshes on the shared clip time |
| Shared actor location | RB placed at pawn Location + vehicle mesh translation (−bounds centre) | TnVehicleForm.OnActivate/CalculateCylinderBounds | CONF | **APPLIED** (vertical only) |
| Weapon on V→R | Restore at 25%, usable +0.2 s; drawn on the robot mesh | TARGETED_PASS2 §7 | CONF | **APPLIED** + firing requires the drawn gun |
| Arm mesh | CP_OptimusArm_SKEL when no weapon (R→V fold, V→R before 25%), ARM_Equip / ARM_Unequip, WeaponSocket_Secondary | TARGETED_PASS2 §9, character.json | CONF | **APPLIED** (13b); HandSkelControl PROV/not applied |
| Fine-aim offset | Lateral Y 300 in all rows; fine aim changes orbit X (+150 → −50) and Z ends (150 → 80) | raw OffsetCurvesByPCS + TnLocationOffset/HmOrbitUpdateLocationRotation bytecode | CONF | **APPLIED** (no lateral change by design) |
| Landing clip | SharedAcrobatics.LandingAnims {1200,1200}_03, {1000,1200}Land, {4500,0}_03, {500,0}_02, {250,0}Land | authored data (Systems handoff) | CONF data / MED semantics | **APPLIED** (13b) |
| Wall contact | physWalking slide: velocity into the wall removed, displacement velocity | stock UE3 | HIGH | **APPLIED** (13b, single-ray probe PROV) |
| Ram | AttemptToRam during nitro: once per target, 300 to AI robots | TnTruckForm bytecode + OptimusTruckForm | CONF | **APPLIED** vs damage targets (13b); knock-back n/a |

---

## PASS 12 — RECONCILED WITH NATIVE RE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Vehicle→robot entry | Local player keeps full velocity and enters falling | native RE | CONF | **APPLIED** (replaces Pass 11 ground snap) |
| Robot→vehicle entry | Velocity clamped 3500 UU/s written into the RB, no reprojection; starts at robot yaw | native RE + OnActivate bytecode | CONF | **APPLIED** |
| Hover steering authority | Fades in over 0.5 s: accel × (1 − Drift/0.5)², Drift started by Hovering.BeginState | native RE + CalculateDriftScale/Drift bytecode | CONF | **APPLIED** (also after leaving Driving) |
| Hover control frame | View yaw: Hovering.DoUpdate → HoverSimulation.Update(..., ViewRotation.Yaw); UpdateTurn matches the yaw; UpdateStrafe toward (fwd, right) × 15 m/s, ClampLength accel 30 m/s² × drift × stability | bytecode | CONF | **APPLIED** (supersedes Pass 7 travel-direction steering; stability scale = 1 on flat ground) |
| Normal boost | Boost held → TnCarForm.Driving (Truck_Physics MaxSpeed 3000, MaxAcceleration 2500); not while drifting; release → Hovering | Hovering/Driving.UpdateBoosting bytecode; Truck_Physics | CONF (state, values) / PROV (steering, throttle) | **APPLIED** with wheels pose (HoverToBoost → Idle_Wheels; BoostToHover on exit) |
| Hover dash | Dash while hovering: dominant input axis (local), DashSpeed 3000, 0.5 s, accel 100000; cooldown TimeBetweenDashes 2.0 | Hovering.DoDash/UpdateDashing, UpdateDash, get_TimeBetweenDashes | CONF | **APPLIED** |
| Nitro / ram | Dash while driving: 3 s, speed ×1.5, steering ×0.3, cooldown 8 s, stop on leaving Driving | Systems checkpoint (script literals) | CONF | speed/steering **APPLIED**; ram collision not implemented; state duplicated with Systems VehicleNitro (unify at integration) |
| Dash binding | VehicleSpecialMove: PC Shift ("Ability0 \| VehicleSpecialMove"), pad RightShoulder → PlayerInCarForm.StartVehicleSpecialMove → set_DashingInput | bindings + bytecode | CONF | **APPLIED** (Systems' temporary Q superseded) |
| Input latching | Fire held flag persists; reload on release of a tap < 0.3 s; jump/dash edge latched until consumed; transform immediate | native RE | CONF | **APPLIED** |
| Weapon restore | _RestoreWeaponTransformFractionRemaining 0.75 = restore at 25% elapsed (vehicle→robot); usable after EquipTime 0.2 s | native RE + ini | CONF | **APPLIED** (usable at 0.48 s of 1.13 s fold); visible gun waits for the 50% mesh handoff [PROV] |
| Fine aim on transform | Transforming to vehicle ends it | native RE | CONF | **APPLIED** (wish cleared) |
| Locomotion play rate | Walk/jog at 1.0×; the original shows the same stride mismatch | native RE | CONF | no compensation (as built) |

---

## PASS 11 — TRANSFORM MOMENTUM, ROBOT SPEED, FINE AIM (2026-10-01, gameplay agent)

Evidence: UnrealScript bytecode decoded from `TransGame.xxx` with `work/pass11/ue3dis.py` (validated on
`TnPawn.UpdateSpeeds`), shipped bindings (`Xe-TransInput.ini`), character/vehicle/camera content
objects (`Optimus_ROBODEF`, `HoverTruck_Physics`, `TruckTransformerMomentum`, `OverTheShoulder_STRATEGY`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Velocity at transform start | Untouched. `TnPawn.Transform` → `Transforming.BeginTransformation` → `TnTransformation.Execute` never zero velocity | bytecode | CONF | **FIXED** (rebuild zeroed velocity in beginTransform) |
| Input during a fold | Movement keeps working; only `IsAbleToFire`, `PreWeaponSwitch`, `StartTransform` check IsTransforming | bytecode (all 20 get_IsTransforming call sites) | CONF | **FIXED** (rebuild locked all input); fire/reload stay blocked, jump blocked [PROV] |
| Movement form switch | `BeginTransformation`: _CurrentForm = TargetForm + target movement capabilities at fold START | bytecode | CONF | **APPLIED** (moveForm(); mesh still hands off mid-fold) |
| Robot→vehicle handoff | `TnVehicleForm.OnActivate`: Velocity = ClampLength(pawn Velocity, 3500); RB rotation = pawn rotation | bytecode; `kMaxTransformSpeed` 3500 | CONF | **APPLIED** |
| Vehicle→robot handoff | `TnRobotForm.OnActivate` leaves velocity; robot `CalcVelocity` continues it | bytecode | CONF | **APPLIED** |
| Momentum preservation | Above max speed: MaxAccel = AccelRate/(1+P); P from TruckTransformerMomentum OnGround 7/3/1, InAir 100/100/5 (fwd/neutral/back; fwd = input within 30° of velocity); InAir set while falling OR transforming | `CalculateMomentumPreservation`, `CalculateMaxAcceleration` bytecode | CONF | **APPLIED** |
| Robot velocity model | Desired = Normal(input)·max(450, ‖input‖·MaxSpeed); no input → 0 (ground) / keep (air); local-frame per-axis accel clamp; no speed cap | `TnPawn.CalcVelocity` / `CalculateDesiredFlatVelocity` bytecode | CONF | **APPLIED** (AirControl application PROV) |
| Character movement values | BaseGroundSpeed 1400, AccelRate 12000, AirSpeed 1200, AirControl 0.4, TerminalVelocity 6000, Collision r200/h200, JumpHeight 500 (SharedAcrobatics) | `Optimus_ROBODEF` via `TnPawn.ApplyTransformer` bytecode; identical on 6 playable ROBODEFs | CONF | **APPLIED** (replaces Passes 2/10 class defaults) |
| Robot fast movement | No sprint key exists. Full input = 14 m/s jog (the "missing fast movement"); partial pad input ≥4.5 m/s walk; vehicle→robot keeps vehicle/boost speed (momentum run, ≤35 m/s handoff) | bindings + above | CONF | **APPLIED** |
| Boost input | RightMouseButton / LeftTrigger are bound to both FineAim and Boost; robot OnStartBoost empty; vehicle states boost | `Xe-TransInput.ini`, bytecode | CONF | **APPLIED** (RMB/LT boost in vehicle; Shift kept as alias) |
| Truck physics | DashSpeed 3000, DashDuration 0.5, SuspensionRadius 185 (MaxLinearSpeed default 1500) | `VEH_SHARED_p.HoverTruck_Physics` | CONF | **APPLIED** |
| Fine aim input | PC RMB = ToggleFineAim; pad LT = FineAim (hold) | `Xe-TransInput.ini` | CONF | **APPLIED** |
| Fine aim rules | Robot only (PlayerWalking); blocked while meleeing/reloading/dodging; wants persist → resumes after reload | `TnFineAimManager.Tick/Start/Stop`, `PlayerWalking.CanFineAim` bytecode | CONF | **APPLIED** (also blocked while transforming [PROV]) |
| Fine aim movement | SetSpeedMultiplier(0.5) → 7 m/s ground (air too) | `_GroundSpeedMultiplier`, UpdateSpeeds | CONF | **APPLIED** (excess speed bleeds through momentum preservation) |
| Fine aim camera | FOV 80→45 (SmoothTime 0.1), exit 0.4; look speed 25/12.5 vs 50/25; orbit distance unchanged for Ion Blaster; shoulder curve unchanged | `OverTheShoulder_STRATEGY` behaviours | CONF values / PROV smoothing curve | **APPLIED** |
| Fine aim weapon | Spread × FineAimSpreadModifier 0.5; weapon events 15/16 (sounds) | `TnWeapon.Start/StopFineAim`, WEPDATA | CONF | spread **APPLIED**; sounds not wired |
| Fine aim upper body | No fine-aim pose (WeaponPose slot plays weapon PoseName only) | `TnWeaponPoseSlotLogic` bytecode | HI | none (aim offset continues) |
| Fine aim target snap | TnOrbitRotateToTarget FineAim: snap, speed 10; _TargetSnapTimeout 1.0 | strategy + ini | CONF | NOT implemented (no target system) |
| Robot camera | Anchor actor+200 UU; orbit 800; DefaultFOV 80; pitch ±75; shoulder offset [150,300,150] by pitch, SmoothTime 0.3; third-person collision | `OverTheShoulder_STRATEGY` | CONF values / PROV offset semantics + collision rules | **APPLIED**; traces aim through the crosshair point |
| Support/step query | groundHeight(nearY = body, stepUp = 35 UU) | — | fix | **FIXED** (pre-existing double count: robots stepped 0.7 m, vehicles could snap to decks 4.4 m higher) |

---

## PASS 10 — LOCOMOTION BLEND, GROUND SPEED, STEP HEIGHT (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Moving-state structure | TnVelocityAnimBlend(Walking, Jogging) over TnStraferAnimBlend(F,B,R,L) of `Nav_Strafe{Walk,Jog}_*` (OneHanded) | Robot_ANIMTREE | CONF | **APPLIED** |
| Walk→jog speeds | MinSpeed 450 / MaxSpeed 1200 UU/s (class default 100/1000 overridden) | `TnVelocityAnimBlend_10449` | CONF | **APPLIED** (linear weight; PROV formula) |
| Clip ground speeds | walk ≈3.5 m/s, jog ≈12.1 m/s (planted-toe stance speed) | robot.glb measurement (`work/pass8/stride.js`) | HI | Explains 1200 = jog speed |
| Direction weights | from local velocity; `_BlendSpeed` 0.2 | `Default__TnStraferAnimBlend` | CONF (value) / PROV (max(0,±dot) normalized, eased) | **APPLIED** |
| Phase sync | "Strafers" AnimNodeSynch group (all 16 strafe sequences, RateScale 1) | `AnimNodeSynch_581` | CONF | **APPLIED** (shared phase, master = highest weight) |
| Idle↔Moving transition | AmpCrossFadeCondition 0.2 s | IdleToMoving / MovingToIdle | CONF | **APPLIED** (was PROV 0.15) |
| Robot ground speed | GroundSpeed = _BaseGroundSpeed (=GroundSpeed 550 at PostBeginPlay) × Π speed multipliers; Ion Blaster GroundSpeedMultiplier 1.0 | TnPawn.PostBeginPlay / UpdateSpeeds bytecode; Default__TnWeaponData | CONF | **SUPERSEDED (Pass 11):** ApplyTransformer later calls set_BaseGroundSpeed(ROBODEF.BaseGroundSpeed = 1400) → 14 m/s |
| MaxStepHeight | 35 UU = 0.35 m (WalkableFloorZ 0.7, MaxFallHeight 3400) | `Default__TnRobotForm._MovementCapabilities` | CONF | **APPLIED** step 0.35 m (was PROV 0.6); deterministic A/B shows no new snagging. WalkableFloorZ not yet enforced. |
| Idle clip names | Tree defaults `NAV_Idle_01` / `AI_Nav_Idle_Pose_05` / `ADD_NAV_Idle` resolved per character by choosers | Robot_ANIMTREE idle branch | — | Optimus set lacks them; `NAV_Idle` kept |

---

## PASS 9 — AUTHORED AIM OFFSET PROFILE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Active profile | `Default` for the Ion Blaster (`ActiveProfileProperty`=WeaponTypeObserved; other profiles are TwoHandedMelee, TwoHandedMeleeCharge, TwoHandedGun) | Robot_ANIMTREE `TnAnimNodeAimOffset_14979` | HI | **APPLIED** |
| Profile bones | Lumbar01, Lumbar02, Neck01, Head, L Clav/Shoulder/Elbow/Hand, R Clav/Shoulder/Elbow | `AimComponents[].BoneName` | CONF | **APPLIED** (replaces the whole-upper-body local deltas of Passes 7–8) |
| Cell offsets | 9 rotations (+ translations on R_Arm01_Clav) per bone, baked from `Shooting_Aim_{L,F,R}_{D,C,U}` (`AnimName_*`) | `AimComponents` | CONF | **APPLIED**, baked at load by the verified rule: rot = Gp·(L_cell·L_C⁻¹)·Gp⁻¹, pos = Gp·(t_cell−t_C), Gp = parent model rotation in that cell. Matches the shipped quaternions to 0.01° mean / 0.07° worst and translations to 0 mm (UE→glTF: q `(−x,−z,−y,w)`, v `(x,z,y)·0.01`). |
| Application | Model-space rotation/translation about each bone's pivot, parent first; bilinear between cells | UE3 AnimNodeAimOffset (consistent with the bake: reproduces every cell pose from the centre pose) | HI | **APPLIED** (slerp per axis, weight = aimW). |
| Ranges | Profile H [−1,1], V [−1,0.8]; `RemapPawnAimRange` from PawnAimOffsetRange H [−1,0.85], V [−0.7,1]; pawn aim = angle/90° | Default profile | CONF (values) / PROV (centre-preserving remap) | **APPLIED**. Barrel pitch at aim −69/−34/0/+34/+69° = −42/−21/+3/+27/+50°. Replaces the Pass 7 barrel-pitch calibration. |
| Pivot arm swing | `Nav_IdlePivot90_*` swings the gun forearm ≈50° mid-step (root-discarded chest ±7°) | clip data; tree layers the aim offset over the turn unchanged | HI | Kept as authored. |

---

## PASS 8 — RECOIL, TURN IN PLACE, SHIPPED ANIM TREE (2026-10-01, gameplay agent)

Primary evidence: `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` (cooked in `MP_IAC_Streets_BASE_m.xxx`)
plus class defaults in `TransGame.xxx` / `HM_Engine.xxx`, read with AssetTools `ue3pkg`/`props`
(read-only; dumps in `work/pass8/`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Upper-body blend root | `AnimNodeBlendMultiBone` start bone `C_Spine01_Lumbar01_XB` (plus a left-arm branch at `L_Arm01_Clav_XB`) | Robot_ANIMTREE | CONF | Matches the Pass 7 mask root (PROV → **CONF**). |
| Recoil bones | RightHandRecoil → `R_Arm02_Shoulder_XB`; LeftHandRecoil → `L_Arm01_Clav_XB`; SpineRecoil → `C_Spine02_Lumbar02_XB` | Robot_ANIMTREE `SkelControlLists` | CONF | **APPLIED** (spine + right hand; the Ion Blaster defines no left-hand recoil). |
| Recoil values | Spine: 0.8 s, RotAmp [500,1000,0], Freq [10,10,0], all ERS_Zero. Hand: 0.5 s, RotAmp [2000,500,−2000], Freq [15,10,10], Yaw ERS_Random, LocAmp X −8 UU @10 | `Default__TnWeaponMesh` archetype + IonBlaster_WEPMESH overrides; struct default `Default__HmSkelControlRecoil` (0.33 s, zeros) | CONF | **APPLIED** (`game/Recoil.h`), restarted per shot (`TnRecoiler`). |
| Recoil update law | sin-wave per axis × smoothstep(TimeToGo/Duration), mesh-space in the aim frame (`bBoneSpaceRecoil` false) | UE3 GameSkelCtrl_Recoil (reconstructed; native not decompiled) | MED | **APPLIED**. UE rotator → model-axis mapping (yaw/roll sign) PROV. Measured: +12° muzzle climb under sustained fire. |
| Turn-in-place values | Threshold 4096 UU (22.5°), TransitionBlendTime 0.1 s, PercentageToAllowAbort 0.5, RotTransitions ±16384/±32768; player `RRO_Discard` | `Default__TnAnimTurnInPlace`, `Default__TnAnimTurnInPlacePlayer` | CONF | **APPLIED**. |
| Turn clips | Rt_90/Rt_180 → `Nav_IdlePivot90_R`, Lt_90/Lt_180 → `Nav_IdlePivot90_L` (WS_ONE_HANDED) | TnWeaponAnimChooser in Robot_ANIMTREE | CONF | **APPLIED**. Clips author ~81° on `C_Root_Reference_XR` over 0.53 s. |
| Turn trigger / unwind | Inferred: legs planted (UnwindLowerBody), trigger when \|offset\| ≥ RotationOffset − threshold, unwind RotationOffset along the clip's root-yaw curve | Field names + values; native logic not recovered | PROV | **APPLIED**. Chosen over "trigger at 22.5° and unwind the offset" because it keeps the feet consistent with the authored step. |
| Aim offset yaw columns | Leg/aim difference feeds the aim offset horizontally (`TnAnimNodeAimOffset.TurnInPlaceOffset`) | TransGame class layout | HI | **APPLIED** (Pass 9: through the authored profile ranges). |
| Aim offset interpolation | InterpSpeed 12 | `Default__TnAnimNodeAimOffset` | CONF | **APPLIED** (FInterpTo-style). |
| Aim offset authored profile | `TnAnimNodeAimOffset` "Default" profile | Robot_ANIMTREE | CONF | **APPLIED in Pass 9** (see below). |
| Incoming transform clip | Matched pair `Transform_ToVehicle_ROBO` ↔ `Transform_ToVehicle_VEH` | clip durations (1.967 s both) | HI | **FIXED** (was SuperBoost_Veh, 0.8 s). |

---

## PASS 7 — ANIMATION LAYERS & MESH FACING (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Mesh forward axis | Skeletal meshes face model **+X** (UE convention; umodel `(x,z,y)` keeps X). | `Shooting_Aim_F_C` barrel dir in model space = (1.00,−0.06,−0.02) | CONF | **FIXED** `kMeshYawOffset=+π/2` (Pass 6's 0 left robot/truck side-on, gun 90° off the reticle). Verified: chase cam sees back/rear; muzzle along aim yaw. |
| Upper-body aim offset | `Shooting_Aim_*` 9-pose grid points the gun at the aim. | `robot.glb` category `aim`; weapon.json `owner_animations.aim` | HI | **APPLIED**: F column (body faces aim yaw → yaw offset 0), delta from F_C on `C_Spine01_Lumbar01_XB` subtree, driven by controller pitch. |
| Aim pitch → grid mapping (superseded by Pass 9 authored ranges) | UE3 AimOffset normalized pitch range | Calibrated from the poses: barrel pitch D −47.6° / C −3.3° / U +72.2° | CONF-derived | **APPLIED**: piecewise-linear so barrel pitch ≈ aim pitch (measured −27/+7/+42° at −34/0/+34°). |
| Reload while moving | Owner reload clip over locomotion | weapon.json `owner_animations.reload_robot` = `Shooting_Reload_IonBlaster_ROBO` | HI (clip) / PROV (mask) | **APPLIED**: upper-body slot (mask root `C_Spine01_Lumbar01_XB` PROV), full body when standing, mesh-space per-bone blend (UE3 `AnimNodeBlendPerBone` default). |
| Slot / aim / additive ease time | — | — | GUESS | `kSlotBlend=0.15 s` **PROVISIONAL**. |
| Vehicle move animation | Directional single-frame hover poses + additive hover bob | `vehicle.glb` `Nav_Hover_{Pose,F,B,L,R}_VEH`, `ADD_Nav_Hover_VEH` (additive=true) | HI (clips) / PROV (weights) | **FIXED** (was looping the one-shot `Nav_BoostToHover_VEH` transition). Weights = local velocity / MaxLinearSpeed per axis. |
| Vehicle turn rate | **~π rad/s** | `TnHoverCarSimulationBlueprint.AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AiMaxAngularSpeed is AI-only (truck: 20); player steering rate PROV |
| Robot gameplay idle | Character-specific nav idle | `NAV_Idle` from `Optimus_ROBO_ANIM`; `Cust_Idle` = customization screen | MED | **FIXED** (was `Cust_Idle`, first of category). |
| Jump / land | Take-off once → descent loop → land | `Nav_TakeOff_01`, `Nav_Jump_Descent`, `Nav_Land` | MED | **APPLIED**: take-off non-looping; land after ≥0.3 s airborne (`kLandMinAirTime` PROV), skipped when moving. |
| Weapon recoil | see Pass 8 | weapon.json + archetype | CONF | **APPLIED in Pass 8**. |
| Turn in place | see Pass 8 | Robot_ANIMTREE | CONF | **APPLIED in Pass 8**. |

Additive-clip convention: `ADD_*` clips are stored as **deltas** (identity-ish quats, zero
translations at rest), so `samplePose(additive=true)` starts from identity and `addPose` composes
`base * delta` in bone-local space.

---

## PASS 7 — WEAPON LAYERING, RECOIL, WEAPON MESH, FX, SOUNDCUES (Systems agent, 2026-10-01)

> **Integration note (integration/milestone-01):** the recovered data in this pass is kept as
> provenance. In the merged build, the *Reload layering*, *Recoil controls/defs/evaluation* and
> *Upper-body aim offset* (PASS 7b) rows are implemented by **Gameplay's** code (Passes 7–9), not
> by the Systems implementation described here. Both branches recovered the same Robot_ANIMTREE /
> RecoilDef values. The Systems `AimOffset.h`, `updateUpperBody`/`evalLayered` and shot-serial
> recoil trigger were not merged, to avoid a second aim offset and double recoil per shot.
> Remaining value difference: Systems reads the owner-anim slot blend as **0.1/0.1 s [CONF]**
> (`TnWeaponOwnerAnimator` CDO), while Gameplay's reload slot uses `kSlotBlend` 0.15 s [PROV].
> This is left for the Gameplay owner to reconcile. The weapon mesh, sockets, notifies, FX, SoundCues
> and audio rows below are all live in the merged build.
Recovered from cooked packages with AssetTools `objtree`/`typed_props` (read-only); decoders in
`tools/systems/`. Owner animation, recoil, weapon-mesh, FX and cue data come from
`WEP_IonBlaster_p` / `FX_*_p` / `BL_WPN_*` exports cooked into `A1_IAC_Base_m` / `MP_IAC_Streets_BASE_m`,
plus class defaults (CDOs) in `TransGame.xxx` / `HM_Engine.xxx`.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Reload layering | Reload plays in Robot_ANIMTREE's **UpperBodyCustom** AnimNodeSlot, fed through AnimNodeBlendMultiBone_4930 (InitTargetStartBone **C_Spine01_Lumbar01_XB**, PerBoneIncrease 1.0: spine + arms + head = 1, hips/legs = 0). Legs keep locomotion. | `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF | **APPLIED** (bone-local blend, as AnimNodeBlendMultiBone). Verified: base `Nav_StrafeJog_F` @5.5 m/s while upper = `Shooting_Reload_IonBlaster_ROBO`. |
| Reload owner anim | `ReloadAnimation = Shooting_Reload_IonBlaster_ROBO`, non-additive, BlendIn/Out **0.1/0.1 s** | `IonBlaster_WEPDATA.TnWeaponOwnerAnimator_10029` + `Default__TnWeaponOwnerAnimator` | CONF | **APPLIED**. Slot releases at reload end (1.5 s) and blends out while the 1.633 s clip finishes. |
| Fire owner anim | none (`FireAmmoAnimations` empty for the Ion Blaster); firing drives skel-control recoil only | same | CONF | APPLIED |
| Equip/overheat anims | `ADD_Shooting_equip_weapon` (CAMT_UpperBody, additive, 0.2/0.3 s); overheat Start/Loop/End additive | same | CONF | not yet (no equip/overheat gameplay) |
| Recoil controls | HmSkelControlRecoil: **RightHandRecoil** on R_Arm02_Shoulder_XB, **SpineRecoil** on C_Spine02_Lumbar02_XB, LeftHandRecoil on L_Arm01_Clav_XB | Robot_ANIMTREE SkelControlLists | CONF | **APPLIED** (left hand: no Ion Blaster RecoilDef, idle) |
| Recoil defs | Spine: 0.8 s, RotAmp (500,1000,0), RotFreq (10,10,0), RotParams all Zero. RightHand: 0.5 s, RotAmp (2000,500,-2000), RotFreq (15,10,10), Y=Random, LocAmp X=-8 UU @10 | `TnWeaponMesh` CDO merged with `IonBlaster_WEPMESH` | CONF | **APPLIED** |
| Recoil evaluation | RecoilDef layout identical to UE3 GameSkelCtrl_Recoil: restart per shot, smoothstep(TimeToGo/Duration) x Amp x sin(phase + TimeToGo x Freq), aim space | struct identity with UE3 | HI | **APPLIED** (rotation about the bone origin in model aim space) |
| Camera recoil / shake | RecoilCameraParams (FOV +0.085, pitch 0.1 deg, 0.05 s, delay 0.25 s); CameraShake (0.17, roll 0.15, falloff 0.1/0.06 s) | `IonBlaster_WEPDATA` | CONF | **not applied**: camera is owned by the Gameplay agent |
| Weapon mesh | 34-joint skeletal mesh with its own AnimSet: WP_Fire -> `IonBlaster_Fire`, WP_Reload -> `Shooting_Reload_IonBlaster_AP`, idle `IonBlaster_Idle`; HmAnimatedMesh BlendOutTime 0.2 s | `IonBlaster_WEPMESH.WeaponEventAnims`, `HmAnimatedMesh_9126` | CONF | **APPLIED** (was a static mesh) |
| Weapon sockets | MuzzleFlash -> C_Robo04_XT; ShellSocket -> C_Robo15_XT (+loc/rot); MagSocket -> C_Robo01_XT (+loc/rot) | `WEP_IonBlaster_SKEL` SkeletalMeshSockets | CONF | **APPLIED**; muzzle/tracer origin = MuzzleFlash socket (replaces the geometric barrel tip) |
| Event timing (AnimNotifies) | Fire: Shell_AssaultRifle_FX @0.005 ShellSocket. Reload_AP: ANIM_RELOAD_01 @0.000, Reload_AssaultRifle_FX @0.034 MuzzleFlash, ANIM_RELOAD_02 @0.137, Magazine_IonBlaster_FX @0.174 MagSocket. Idle: IDLE_01 @0.022, IDLE_02 @2.751 | `WEP_IonBlaster_ANIM` (HmAnimNotify_Sound / HmAnimNotify_PlayEffect) | CONF | **APPLIED** for sounds and effects (shell/reload/magazine FX: PASS 7c) |
| Muzzle flash | `FX_AssaultRifle_p.FX.MuzzleFlash_AssaultRifle_FX`, local space at MuzzleFlash | WEPMESH.MuzzleFlashes | CONF | **APPLIED**: Long (MuzzleFlash_Side_02, velocity-aligned, burst 10, life 0.15-0.2, 1.9-2.1 x 3.5-5 m, +1.9 m, 9 m/s), Top (smokeball_02 star), Sparks (SparksSheet 2x2 SubUV). Omitted: Glow_Mod (modulate), distortion ring, BackSteam/BackJet (alpha <= 0.05) |
| Tracer | `Tracer_AssaultRifle_FX`: Bolt (Bolt_ADD_MAT, PSA_Velocity, burst 1, life 0.6, 2 x 5-7 m, 15000 UU/s) + Trail2 smoke ribbon (Tracer_Smoke, life 0.9) | WEPMESH.TracerTemplates | CONF | **APPLIED**; bolt removed at the impact point; smoke ribbon opacity 0.35 **PROV** |
| Impact squib | `Impact_IonBlaster_FX`: GLOW_Dup (MuzzleFlash2, burst 10), Sparks_bolts (Spark_Tail, burst 20, 1.5-10 m/s), Smoke (SmokeThin, burst 4, x5 growth); rules 60 %, max 5 live, 25 m max distance | WEPMESH.DefaultSquib / TnWeaponMesh CDO | CONF | **APPLIED**; surface normal approximated by -shot dir (collision query returns no normal) |
| Effect colour | native colour constant `ff 33 19 ff` read as ARGB = (51,25,255) blue-violet; EnergonColor param (255,255,255,A=0) = no override; SwitchableColorScaleOverLife A/B chosen by Team | LOD streams, WEPMESH params, editor thumbnails | HI | APPLIED (team A) |
| FX module roles | WFC compiles modules into a native per-LOD stream; distributions decode exactly (type/op/n/chunk + BE float table), but **which module each belongs to is inferred from order** (Lifetime, StartRotation, ..., AlphaOverLife, StartSize, SizeMultLife, Velocity, ColorOverLife, Location) | stream analysis | MED | used as above; x4 / x2 overbright assignment MED |
| Fire cue | `BL_WPN_GUN_ION_BLASTER.SHOOT`: root -9 dB, Distance 4000-35000 UU, MaxConcurrent 4; layers by SOUND_DISTANCE: HEAD (<=200 UU), HANDCANNON_LR (400-4000), NRG_DISTANT (4000-8000), shell drops (+0.474 s) | cooked SoundCue / SoundNodeRoot / SoundNodeWaveEvent | CONF | **APPLIED** (generated table) |
| Low ammo / tail / impacts | SHOOT_LOW_AMMO (ammo <= 5), SHOOT_TAIL (WP_LoopingTail, 3000-12000 UU), IMPT_WORLD (-12 dB), IMPT_DMG (-9 dB, rolloff 2), max 6 | same | CONF | **APPLIED** |
| Reload / idle cues | ANIM_RELOAD_01 (3 events), ANIM_RELOAD_02 (12 timed events to 1.33 s, Distance 1000 UU, rolloff 2), IDLE_01/02 (-21 dB) | same | CONF | **APPLIED** via AnimNotifies (replaces the single provisional clip-reload wav) |
| Attenuation model | DistanceMin/Max + RolloffFactor = FMOD Ex inverse rolloff (banks ship as .fsb) | field names + FMOD banks | HI | APPLIED |
| SOUND_DISTANCE for own weapon | `kSmartPan_PreferPlayer`: parameter measured from the owning player | inferred from the name | MED | APPLIED |
| Concurrency | MaxConcurrentPlayCount -> steal the oldest instance | - | MED | APPLIED |
| Mixer categories / reverb | SFX_WET_COMBAT_ROBOT_WPN(_SHOOT) category levels, WET reverb sends, occlusion | not extracted | - | **not applied**; master level 0.5 still PROV |
| Dry fire | `BL_WPN_FOLEY.SHOOT_DRY_FIRE_ELECTRICITY` (WP_NoAmmoFire) | cue table present | CONF | not triggered (needs a trigger-on-empty event from PlayerController, owned by Gameplay) |

Open items for the Gameplay agent: the robot's idle base clip is currently `Cust_Idle` (the
customisation-screen showcase idle, first entry of category `idle`), which turns the body and
points the gun away from the aim; `NAV_Idle` / `Nav_Idle_Pose` is the gameplay idle. Upper-body
aim offset: applied in PASS 7b. Mesh yaw offset: see PASS 7b (should be +pi/2).

### PASS 7b — upper-body aim offset (Systems agent)
- **Space and axes verified from data:** recomputing the `Shooting_Aim_F_U` vs `Shooting_Aim_F_C` mesh-space
  delta in robot.glb reproduces the authored Spine01 CU rotation exactly (0.0926), and the Spine02 delta
  (0.318) is the sum of the Spine01 + Spine02 increments. The authored rotations are therefore **mesh-space
  increments applied in hierarchy order**. The UE -> glTF quaternion mapping is (x,y,z,w) -> (-x,-z,-y,w).
- **Tree order [CONF]:** the AimOffset node sits under the `UpperBodyCustom` slot's source (via
  TnAnimTurnInPlaceRotator "UnwindLowerBody"), so it is: locomotion -> aim offset -> reload slot -> recoil.
  A playing reload replaces the aimed spine/arm rotations, as in the original.
- **Pawn aim input [MED]:** X = 0 (the body already faces the aim yaw); Y = camera pitch / 90 deg (UE3 pawn
  aim convention), remapped piecewise-linearly through PawnAimOffsetRange -> profile range. The exact
  TnAnimNodeAimOffset remap and the active-profile choice (`WeaponTypeObserved`; Default for the Ion
  Blaster) are not decompiled.
- **Mesh facing bug found (Gameplay-owned, not changed here):** the skeleton faces **+X** in model space
  (eyes are +X of the head, left clavicle at -Z, gun forearm along +X in NAV_Idle, StrafeJog_F and
  Shooting_Aim_F_C), but `core::config::kMeshYawOffset = 0` renders the mesh as if it faced -Z. The robot
  is therefore drawn rotated 90 deg from the aim; measured barrel heading = aim - 97..102 deg. With the
  renderer's rotateY convention the correct value is **kMeshYawOffset = +pi/2**. The recoil aim frame
  now uses the measured +X forward, so it is correct either way.

### PASS 7c — shell, magazine and reload FX (Systems agent)
Spawned by the weapon AnimNotifies at their authored times and sockets (see the Event timing row).

| Effect / emitter | Original data | Conf | Rebuild |
|---|---|---|---|
| Shell_AssaultRifle_FX "Shell" (ShellSocket, every shot @0.005) | mesh `FX_GrenadeLauncher_p.GrenadeAmmo_STAT` (WEP_GrenadeLauncher_MATINST -> WEP_GrenadeLauncher_CLR), burst 1, life 1.0, StartSize (0.75,0.3,0.3), spin U[(-1,-1,-1),(5,5,1)] turns/s | CONF | **APPLIED** as a mesh particle |
| shell ejection velocity | not on the Shell emitter; the paired ShellGlow emitter (same socket) carries U[(300,-300,100),(1000,-300,300)] UU/s | MED | used for the shell mesh |
| Shell "SMOKE" | SmokeCoolDepth, burst 4, life U[0.5,0.75], size U[50,100] UU growing x3, alpha 0.1 -> 0, velocity U[(100,-300,-300),(300,300,300)] UU/s | CONF (role order MED) | **APPLIED** |
| Magazine_IonBlaster_FX "Shell" (MagSocket, reload @0.174) | mesh `FX_IonBlaster_p.IonBlaster_Mag_STAT` (IonBlaster_Mag_MATINST -> WEP_IonBlaster_CLR), burst 1, life 3.0, StartSize 1, spin U[-1,1] turns/s, velocity (200,300,300) UU/s | CONF (velocity role MED) | **APPLIED** |
| Magazine "Smoke_Dup_Dup_Dup" | as Shell SMOKE, alpha 1 -> 0, colour 1 -> 0.1 | CONF | **APPLIED** |
| Reload_AssaultRifle_FX "GLOW_Dup_Dup" (MuzzleFlash, reload @0.034) | flareball01 (smokeball_01), local space, burst 10, life U[0.2,0.5], size U[20,35] UU x (1.5 -> 0.1), alpha peak 0.2, brightness curve 20 -> 1 | CONF data / MED roles (size and velocity from uniform-curve tables) | **APPLIED** |
| Reload "Smoke_Dup" | SmokeCoolDepth, SpawnRate 20/s for 0.75 s following the muzzle, life U[0.5,0.75], size U[100,200] UU x3, alpha peak 0.25, grey 0.83-0.90, -50 UU behind the muzzle | CONF | **APPLIED** (continuous emitter) |
| Gravity / ground contact for the mesh particles | no acceleration or collision module authored | - | **PROV**: world pawn gravity (-29.4 m/s^2) and rest on the collision floor |
| Omitted | ShellGlow / GLOW (Glow_Mod_MAT modulate), Shimmer (distortion), BackSteam / BackJet (alpha <= 0.05), Blaster_Trail ribbons on shell and magazine, the "Trail" emitters | - | not rendered |

Asset loading: umodel `.gltf` + `.bin` meshes are now accepted by `assets::loadGlb` (text glTF branch).

### PASS 8 — vehicle boost presentation (Systems agent)
Source: `TR_Optimus_VEHDEF_p.OptimusTruckForm` (TnTruckFormBlueprint) and its
`HmPlayerVehicleAudioComponent_6670`; sockets from `character.json` (VH_OptimusPrime_SKEL); FX from
`FX_Navigation_p.bumble_boost_small1_FX` (cooked in A1_IAC_Base_m); cues from `BL_VEH_OPTIMUS_PRIME`
via `SoundEvents_Vehicles_Trans.Veh_Optimus_Prime_SoundSet`.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Boost FX binding | `BoostFx` = BoostSocket_L / BoostSocket_R -> `bumble_boost_small1_FX` | CONF | **APPLIED** |
| Boost sockets | BoostSocket_L on L_Robo23_XT (0,-35,0) UU yaw -90 deg; BoostSocket_R on R_Robo23_XT (0,35,0) yaw +90 deg: the two exhaust stacks behind the cab | CONF | **APPLIED** (bone x socket, UE -> glTF via vs_common) |
| Ignition burst (EmitterLoops 1) | "thruster": 3 Boostermesh_02 cones, life U[0.3,0.5], scale U[(4,1,2),(5,1,1)] x size curve 0 -> 2.06 -> 1, colour (1,0.6,0.05); "Cone_thrust_Dup": bulletshape mesh, life U[0.3,0.4], scale (50,37.5,15) x 21-entry growth, colour (5,1.5,0.05) x 3; "Particle Emitter_Dup": SphereGlow sprite 30 UU x growth, colour (5,1.5,0.05) x 3, +25 UU | CONF data / MED roles | **APPLIED** |
| Looping while held | "loopcone": Boostermesh_02 at U[3,4]/s after a 0.1 s first-loop delay, life 1.0, scale U[(4,1,2),(5,1,1)] x U[1,1.1], colour (1.07,0.59,0.16); "Particle Emitter_Dup_Dup": SphereGlow at 20/s after 0.2 s, life U[0.4,0.6], 30 UU growing to x3.08, spin U[-0.75,0.75] turns/s, colour (0.8,0.8,0.3) | CONF data / MED roles | **APPLIED** |
| Alpha over life | 0 -> 1 at 20 % of life, linear to 0 (every emitter) | CONF | APPLIED |
| Local space / deactivation | every emitter bUseLocalSpace + bKillOnDeactivate | CONF | **APPLIED**: particles ride the sockets (turning, jumping); release kills them at once |
| Materials | Boostermaterial_02_MAT (additive, two-sided; LightBeam_Falloff_01 + DiffClouds + Spot), bumble_boostcone_MAT (additive, SphereGlow_01 x 1.2), Basic_Particle_Add_MAT (additive, SphereGlow_01) | CONF | APPLIED with the primary texture only (LightBeam_Falloff_01 for the cones) **PROV** |
| HDR colour | colours > 1 (x3 colour scale) | CONF | approximated by hue-preserving normalisation + GL x2/x4 overbright **PROV** |
| Boost audio | BoostSound Auto_Boost_Start -> VEH_OPTIMUS_BOOST_START (5 timed events); BoostLoops Auto_Boost_Loop -> VEH_OPTIMUS_BOOST_LOOP (5 looping layers from 0.44 s, volume/pitch curves on `Optimus_Prime_Speed` (Max 120) + time envelopes); BoostStopSound -> VEH_OPTIMUS_BOOST_END; BoostWheelsSound -> VEH_OPTIMUS_BOOST_WHEELS (50 % ChanceToPlayNone) after BoostWheelsGroundCheckDelay 0.27 s if grounded; BoostFadeOutTime 0.15 s | CONF | **APPLIED** |
| Speed parameter units | `Optimus_Prime_Speed` in mph (curves put nominal pitch at 33 = 15 m/s cruise) | MED | APPLIED |
| START cue curves | VolumeCurve/PitchCurve on an event of a cue with no root SoundParameter | MED | fed the speed parameter |
| Cue loop region | root LoopStart/LoopEnd | - | not used; looping layers loop their whole wave |
| Boost activation | presentation follows the movement code's boost condition (vehicle form + boost held, not transforming), so stationary boost shows the effect | MED | the 0.3 s DashDuration-vs-sustained question belongs to Gameplay movement |
| Material / emissive changes on boost | none authored on OptimusTruckForm (only overshield / defrag materials) | CONF (absence) | n/a |
| Camera feedback | not on the truck form or its audio component; camera behaviours belong to Gameplay | - | not applied |
| Related | HoverFX / JumpFX: PASS 9. RamFX + nitro: PASS 10. Engine / jump / land audio: PASS 11 | CONF data | done (tire squeal open) |

Cue system extension (used by the boost cues): `bLooping` wave events, VolumeCurve/PitchCurve keyed by the
root SoundParameter (SOUND_DISTANCE or speed), Envelope volume/pitch curves over playback time,
ChanceToPlayNone, live parameter/position updates and fade-out stop. The cue table is now generated
by `tools/systems/gen_cues.py` into `src/game/SoundCues.inc` (weapon cues regenerated unchanged).

### PASS 9 — hover thrusters and jump boosters (Systems agent)
Same source object as PASS 8 (`OptimusTruckForm`): `HoverFX` (6 x HoverBooster_* -> `CarHover_A_01_FX`)
and `JumpFX` (JumpBoostSocket_C/R/L -> `Jump_FX`). Implemented in the generic `VehicleFx` (which now also
carries the PASS 8 boost tables unchanged).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Hover sockets | HoverBooster_LFront/RFront on L/R_Wheel01_XB (scale 3), _LBack/RBack on Wheel02, _LBack2/RBack2 on Wheel03 (scale 2/2.5/2.5); +-13 UU, rotation (180, +-90, 0): emission axis down/out from the wheels | CONF | **APPLIED** incl. socket scale (positions, sizes, meshes scale with the socket) |
| Hover looping emitters | Rings_Dup (Ring_Distort_Add -> Ring_CLR, 3/s + 1, life U[0.35,0.5], 120 UU shrinking to 0.5, spin, colour (1,0.5,0.25)); lightcone_Dup (Light_Cylinder_STAT mesh, 10/s, life U[1.5,2], scale U[(0.3,0.3,0.075),(0.25,0.25,0.1)] x 1 -> 1.2 -> 1, alpha 0.5, brightness flicker 0.8-1.33, colour (1,0.1,0.1) x 2; NOT bKillOnDeactivate) | CONF data / MED roles | **APPLIED** |
| Hover one-shot on activation | Sparks_bolts (Spark_MAT, burst 10 + 100/s for 0.3 s, velocity U[(600,-50,100),(1200,50,400)] UU/s, colour (2.5,2,2) x 3); ElectroRing (lightningring_01, 20/s for 0.2 s, colour (1,0.5,0.2) x 3); Pulse (Boostermesh_03, 3-4/s for 0.5 s, scale (0.8,4,4), colour (2,0.1,0.1)) | CONF data / MED roles | **APPLIED** |
| Hover active state | no flag authored; the audio component's Hover vs Boost/Wheels land sounds and the boost wheels peel-out imply: hover thrusters whenever in vehicle form except while boosting | MED | **APPLIED**: vehicle form, not boosting, not transforming |
| Jump FX | 12 emitters, all EmitterLoops 1 / 0.5 s (0.3 / 0.2 s for sparks / electro ring), bKillOnDeactivate: glow bursts (SphereGlow 80-120 UU x5, colour (4,2,1) x 3), booster smoke (20/s, 9-10 m/s down), Boostermesh_03 thruster streaks / bases (burst 3, growth 0 -> 3 -> 1), Boost_Circuit energon cones, Spark_MAT burst 30 + tail sparks, electro ring (6,4,2) x 3, all 50 UU below the socket (socket X points straight down) | CONF data / MED roles | **APPLIED** |
| Jump trigger | not in data; the vehicle jump (JumpLinearSpeed 12 m/s) | MED | take-off edge with vy > 2 m/s; ends early only if the vehicle form ends |
| Light cylinder material | LightCylinder_Rays_MAT_INST -> LightVolume_Base_MAT: view-dependent volumetric (SideViewV, NearFade, DepthBias, dust panners); instance DustPower 0.1 | CONF params | **PROV**: drawn with LightBeam_Falloff_01 x DustPower 0.1 (shader graph not evaluated) |
| Spark_MAT | no texture; procedural streak from texture-coordinate math | CONF (absence) | **PROV**: generated soft-streak texture |
| Rings trailing (150,0,0) | role undecided (location vs velocity) | MED | used as velocity (as an offset it puts rings 4.5 m from the wheels) |
| Omitted | hover base_glow (Glow_Mod_MAT modulate), rays_Dup (Trail_Distort distortion); every material's secondary panning cloud/energon layers | - | not rendered |

### PASS 10 — truck nitro / ram (Systems state + FX + audio; movement effect owned by Gameplay)
Source: TransGame.TnTruckForm compiled UnrealScript (TransGame.xxx) — function/state names and float
literals in the getters' bytecode — plus `OptimusTruckForm.RamFX` and the audio component / sound set.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Trigger | state **Driving** (on wheels = boosting): `UpdateNitro` starts the nitro on the **DASH** input (`_Dashing`) when the cooldown allows; `Driving.EndState` calls `StopNitro`; in state **Hovering** dash is a plain hover dash (`DoDash`) | CONF (script structure) | **APPLIED** in `VehicleNitro`: driving = vehicle form + boost held, not transforming |
| Nitro duration | `get_NitroDuration` = **3.0 s** x NitroDurationModifier (1.0) | CONF | **APPLIED** |
| Speed scale | `get_NitroSpeedScale` = **1.5** x modifier | CONF | **exposed** (`speedScale()`), **not applied** — Gameplay owns movement |
| Steering scale | `get_NitroSteeringScale` = **0.3** x modifier | CONF | **exposed** (`steeringScale()`), **not applied** — Gameplay owns handling |
| Cooldown | `get_TimeBetweenNitros` = **8.0 s** x modifier, starting on activation | CONF (native RE) | **APPLIED** |
| Max ram mass | `get_MaxRamMass` = 1000 | CONF | exposed constant (ram collision is Gameplay's) |
| StartNitro side effects | RamFX on RamSocket, NitroForceFeedback (3 s), nitro camera state | CONF | RamFX **APPLIED**; force feedback / camera not (no rumble path; camera = Gameplay) |
| RamFX | `Truck_ram_FX` on RamSocket (C_Body_XB (380,0,-40) UU, scale (1,1.5,1.5)): Ram_STAT wedge mesh, 20/s, life 1.0, alpha 0.35, colour (2,1.8,1.3), -250 UU; dust + rays are distortion (omitted) | CONF data / MED roles | **APPLIED** |
| Ram_model_MAT | emissive = 2 x (vertex colour x c)^2, c = saturate(pow(1-N.V, FresnelExponent 2) x FresnelScaleUp 1.5) x 2 x lerp(A x L1, L1, 0.4) over four panning Flame_Tile layers | CONF graph | fresnel rim applied per vertex (squared); panning layers = one static Flame_Tile **PROV** |
| Nitro audio | NitroSound Auto_Ram_Nitro -> `VEH_OPTIMUS_RAM_NITRO_START` (7 events); CustomLoopingSound Auto_Ram_Alert -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_ALERT` | CONF | **APPLIED** at nitro start; the alert plays once **MED** (component-level looping not decoded) |
| Ram impact audio | RamSound Auto_Ram_Impact -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_IMPACT` (from AttemptToRam) | CONF | hook `World::notifyRamImpact(pos)` for Gameplay's ram collision |
| BoosterSound | Auto_Ram_Boost -> `VEH_OPTIMUS_RAM_BOOST_START` | CONF mapping | in the cue table; trigger not decoded, not played |
| Dash input | abstract `platform::Button::Dash` | - | ~~**PROV** temporary key **Q**~~ → **Shift** in the merged build (Gameplay Pass 12 [CONF]: Shift = "Ability0 \| VehicleSpecialMove"); Q removed at integration/milestone-02 |

**Value conflict to resolve in Gameplay (documented, not changed here):** the rebuild's vehicle boost uses
`Default__TnHoverCarSimulationBlueprint` DashSpeed 5000 UU/s / DashDuration 0.3 s (core::config
kVehicleBoostSpeed / kVehicleDashTime). Optimus's truck actually references `VEH_SHARED_p.HoverTruck_Physics`
(TnHoverCarSimulationBlueprint) with **DashSpeed 3000 UU/s, DashDuration 0.5 s**, SuspensionRadius 185 UU,
and, for the Driving (wheels) state, `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint) with MaxSpeed
3000 UU/s, MaxAcceleration 2500, JumpLinearVelocity (600,0,1400), Mass 2500. The hover DASH is a separate
mechanic from the nitro (Hovering.DoDash vs Driving nitro). Systems did not modify any vehicle movement value.

### PASS 11 — vehicle engine audio (Systems agent)
Source: `OptimusTruckForm.HmPlayerVehicleAudioComponent_6670` and `Veh_Optimus_Prime_SoundSet`; cues generated
from `BL_VEH_OPTIMUS_PRIME` (tools/systems/gen_cues.py).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Drive loops | DriveSounds gears (MaxSpeed 20, 110): OnLoadLoops Auto_Engine_Gear_1_OnLoad -> `VEH_OPTIMUS_DRIVE_ONLOAD` (2 looping layers), OffLoadLoops -> `VEH_OPTIMUS_DRIVE_OFFLOAD` (3 looping layers incl. idle); one-shots map to None; ReverseSound maps to the same cues | CONF | **APPLIED**; both gears share the cues, so gear selection is audible only through the speed curves |
| Speed response | every layer's volume/pitch curves on `Optimus_Prime_Speed` (mph, Max 120) | CONF curves / MED units | APPLIED |
| On-load vs off-load | native component logic not decoded | MED | on-load while throttle input is held, off-load otherwise |
| Transitions | EngineFadeOutTime 0.2 s | CONF | APPLIED (outgoing loop fades 0.2 s) |
| Airborne | JumpRevSounds UseJumpRev, Auto_Jump_Loop -> `VEH_OPTIMUS_DRIVE_JUMP_LOOP` | CONF | APPLIED while airborne |
| Jump start | AscendSound Auto_Jump_Start -> `VEH_OPTIMUS_DRIVE_JUMP_START` | CONF | APPLIED on take-off |
| Landing | HoverLandSound {0.15 s: `HOVER_LAND_LIGHT`, 2.0 s: `HOVER_LAND_HEAVY`}, BoostLandSound {0.15 s: `WHEELS_LAND_LIGHT`, 2.0 s: `WHEELS_LAND_HEAVY`} by time in air | CONF | APPLIED (wheels variant while boosting); the robot-form placeholder thump no longer plays in vehicle form |
| Boost | the boost loop cue carries its own engine layers | MED | the drive loop yields to `VEH_OPTIMUS_BOOST_LOOP` while boosting |
| Tire squeal | TireSquealSoundParameter / Auto_Tire_Squeal_Default -> `VEH_OPTIMUS_TIRE_SQUEAL`, crossfade 0.5, TireSquealSpeedMin 20 | CONF data | **not applied** (needs a lateral-slip signal from Gameplay's vehicle handling) |

### VEHICLE MECHANICS — normal boost vs hover dash vs ram/nitro (consolidated, Systems checkpoint)
Three distinct mechanics in the original; do not conflate them. Values marked CONF are authored data or
compiled-script literals; "current rebuild" is what the movement code uses today (Gameplay-owned, unchanged
by Systems).

| Mechanic | Original trigger / state | Authored values | Current rebuild | Owner |
|---|---|---|---|---|
| **Normal boost** (drive on wheels) | holding Boost switches TnCarForm from state **Hovering** (HoverBlueprint) to **Driving** (CarBlueprint, wheels on the ground); `get_IsBoosting` / `set_BoostingInput`. Original PC binding: **right mouse in vehicle form** (same button as robot Fine Aim) | `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint): MaxSpeed **3000 UU/s (30 m/s)**, MaxAcceleration **2500**, Mass 2500, JumpLinearVelocity (600,0,1400). Presentation (CONF): BoostFx bumble_boost_small1_FX on BoostSocket_L/R; BOOST_START / BOOST_LOOP / BOOST_END cues, BoostFadeOutTime 0.15 s, BoostWheelsGroundCheckDelay 0.27 s | movement: Sprint (Shift) held, top speed `kVehicleBoostSpeed` 50 m/s reached over `kVehicleDashTime` 0.3 s (taken from the hover-sim **class default** DashSpeed/DashDuration, not Truck_Physics). Presentation: Systems PASS 8 | movement + binding: Gameplay; FX/audio: Systems |
| **Hover dash** | DASH input while **Hovering**: `TnTruckForm.Hovering.DoDash` -> TnHoverCarSimulation dash (`_DashTimeRemaining`, TimeBetweenDashes) | `VEH_SHARED_p.HoverTruck_Physics` (Optimus's actual hover sim): **DashSpeed 3000 UU/s (30 m/s), DashDuration 0.5 s**, SuspensionRadius 185 UU; class defaults (`Default__TnHoverCarSimulationBlueprint`): DashSpeed 5000, DashDuration 0.3, MaxLinearSpeed 1500, accel 3000, JumpLinearSpeed 1200, SuspensionRadius 200 | **not implemented as a separate mechanic**; its class-default values currently drive the normal boost (see above). Systems reads no hover-dash state | Gameplay |
| **Ram / nitro** | DASH input while **Driving** (boosting on wheels): `TnTruckForm.Driving.UpdateNitro` -> `StartNitro`; leaving Driving (`EndState`) -> `StopNitro` | script literals: NitroDuration **3.0 s**, NitroSpeedScale **x1.5**, NitroSteeringScale **x0.3**, TimeBetweenNitros **8.0 s**, MaxRamMass **1000**; StartNitro: RamFX (Truck_ram_FX on RamSocket), NitroForceFeedback 3 s, nitro camera state; NitroSound VEH_OPTIMUS_RAM_NITRO_START, CustomLoopingSound VEH_TRUCK_RAM_ALERT, RamSound VEH_TRUCK_RAM_IMPACT | state/timer/cooldown + RamFX + audio: **Systems** (`VehicleNitro`, abstract `Dash` action, PROV key Q); speed/steering scales **exposed, not applied**; nitro camera + force feedback not applied | state/FX/audio: Systems; speed, steering, handling, ram collision, camera, final binding: Gameplay |

Input notes: `Dash` is an abstract action (no PC binding recovered; temporary **Q**, PROVISIONAL). Right mouse is
deliberately left unbound by Systems so Gameplay can map it per the original: **robot form = Fine Aim,
vehicle form = Boost**.

### Native RE confirmation (Systems, 2026-10-01) — weapon cadence + vehicle states
**Ion Blaster cadence [CONF, native RE]:** one-shot refire timer reset to zero after each shot; fires when
elapsed **> 0.065 s**; fractional overshoot discarded; at most one shot per simulation tick. At 60 Hz this is a
shot every 4th tick = **900 RPM**, the original runtime cadence. `Weapon` now uses exactly this timer
(standalone check: 900 shots/min, every gap 4 ticks). Experimental's `systems-1-fire-interval-remainder.patch`
(923 RPM) is **not applied**: it would make the rebuild faster than the original.

**Vehicle states [CONF, native RE]** (movement math stays with Gameplay):

| State | Input | Behaviour | Values |
|---|---|---|---|
| Normal Boost | **LT / RMB held** | Driving (wheeled) behaviour; returns to Hover when released | top speed ~**3000 UU/s**, accel ~**2500**, special low-speed acceleration (Truck_Physics; TnCarForm CarLowSpeedBoost* modifiers) |
| Hover Dash | **RB / abstract Dash**, Hovering only | forward burst | **3000 UU/s** for **0.5 s**, **2 s** cooldown (HoverTruck_Physics DashSpeed/DashDuration; TimeBetweenDashes) |
| Ram / Nitro | **RB / abstract Dash** while Driving with Boost already active | top speed x1.5 (~**4500 UU/s**), steering x0.3 | **3 s**; **8 s** cooldown from activation; ends immediately when Boost is released; no authored ram animation (RamFX + nitro/ram cues are the presentation) |
| Ram collision | during Nitro only | **one hit per target per Nitro** | OptimusTruckForm: RamDamageToPlayerRobots **175**, ToAiRobots **300**, ToPlayerVehicles **175**, ToAiVehicles **300** (class defaults 50/100/50/100); ExtraRamZVelocity **7000 UU/s** (default 1000); MaxRamMass **1000**; all x TnTruckForm modifiers 1.0 |

Systems side: `VehicleNitro` (state, 8 s-from-activation cooldown, ends on Boost release, `registerRamHit`
one-hit-per-target gate, authored damage/momentum constants exposed) and `World::notifyRamHit(target, pos)`
(gate + ram impact cue). Not applied by Systems: speed/steering scales, hover dash, ram damage/impulse.
Input: LT/RMB = Boost (Gameplay mapping; robot form RMB = Fine Aim); RB = abstract `Dash` (temporary PC key **Q**,
PROVISIONAL). The Dash action is consumed by Systems only to start the Nitro while Driving; Gameplay reads the
same `Button::Dash` for the hover dash.

> **Integration note (integration/milestone-02):** the provenance above is unchanged. In the merged
> build, Gameplay Pass 12 implements all three mechanics in `CharacterMovement` (Driving 30 m/s /
> 2500, hover dash 30 m/s × 0.5 s with a 2 s cooldown, nitro 3 s ×1.5 speed ×0.3 steering with an
> 8 s cooldown, ending on Boost release). It also owns the Dash input: **Shift** / pad RB, latched in
> `PlayerController`. The Systems-side `VehicleNitro::update` timer and its `Q`/World Dash latch
> were replaced by `VehicleNitro::follow(vehicleState().nitroRemain > 0)`, so RamFX, the nitro cues
> and the ram-hit registry track Gameplay's single state machine. Boost presentation (afterburners,
> boost cues) follows Gameplay's Driving state. The values are identical on both sides, so
> nothing was re-tuned. Ram collision is not implemented in either branch; the `notifyRamHit`
> hook is unused.

---

## PASS 6 — PLAYER-CONTROL, ANIMATION & WEAPON PRESENTATION (2026-10-01)
Driven by replaying the exe (runtime observation overrides headless smoke). Priority order as
the player reported it.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Body facing | Robot faces the **aim/camera (mouse) yaw every frame**; WASD = move dir relative to it (strafe shooter). User confirmed: "camera facing = character facing; WASD = move direction." | Observed original + user | HI | **APPLIED** — `CharacterMovement` robot `setYaw(faceYaw)` always; removed the stand-then-walk snap. `face·toCam≈−0.9` verified. |
| Mesh yaw offset | ~~Extracted meshes align to the rebuild's −Z-forward yaw with no extra rotation.~~ **Superseded by Pass 7:** meshes face model +X; offset is +90°. | ~~Runtime geometry check~~ (only tested yaw math) | — | ~~`kMeshYawOffset=0`~~ → **+π/2** (Pass 7). |
| Vehicle facing | Faces its **travel direction** (steering), not the aim. | Observed | MED | **APPLIED** `yaw=atan2(-vx,-vz)` when moving. |
| Directional locomotion | `Nav_Strafe{Jog,Walk}_{F/B/L/R}` chosen by travel dir **relative to facing**. | `robot.glb` clip set (category `run`/`walk`) | HI | **APPLIED** (dot of velocity with facing fwd/right). Verified strafe-R → `Nav_StrafeJog_R`. |
| Upper-body aim offset | `Shooting_Aim_{F/L/R}_{C/D/U}` 9-pose grid points the gun at the reticle. Robot_ANIMTREE `TnAnimNodeAimOffset_14979`, profile **Default**: 11 bones (spine chain, head, both arms) x 9 authored rotations (L/C/R x U/C/D) baked from `Shooting_Aim_*`; ranges H [-1,1] V [-1,0.8]; RemapPawnAimRange from pawn H [-1,0.85] V [-0.7,1] | `robot.glb` category `aim`; `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF (data) / MED (remap) | ~~NOT YET~~ **APPLIED in Gameplay Pass 7/9** (the implementation in the merged build). Systems PASS 7b recovered the same profile independently (aim 22.9 deg -> barrel 21.6-23.8 deg); that implementation was not merged. |
| Transform pairing | Robot & vehicle transform clips share a duration (ToVehicle **1.97 s**, ToRobot **1.13 s**) — one fold authored per mesh, played **in sync**. | `robot.glb`/`vehicle.glb` clip durations | HI | **APPLIED** — outgoing mesh → midpoint → partner mesh resumed at same normalized time (was sequential = the "crack"). Weapon holstered through the fold. |
| Transform cross-fade point | exact visibility/alpha handoff curve | — | GUESS | `kTransformHandoffFrac=0.5` **PROVISIONAL**; cross-mesh pop minimised, not removed. |
| Muzzle origin | Ion Blaster **barrel tip** (MuzzleFlash socket). Socket transform not extracted; used geometric tip. | `weapon.glb` frontmost vertex slice | CONF-derived | **APPLIED** — weapon-local (2.063,0.017,0.141) m → +2.07 m forward of the hand; tracer+flash leave the barrel. |
| Reload anim | `Shooting_Reload_IonBlaster_ROBO` (full-body); `WeaponReloadAnimTime` 1.5 s (clip 1.633 s). | `robot.glb` category `reload`; `weapon.json` | CONF | **APPLIED** full-body one-shot while reloading. Additive `ADD_Shooting_Reload_*` (reload on the move) = PARTIAL. |

**Lighting (#6/#7/#8) — superseded by PASS 7 (original render path).** World still uses only **coeff0**
of the 3-coefficient directional baked lightmap (fixed-function can't apply the directional basis
per-pixel); character uses a provisional `GL_LIGHT0` not sampled from the world. True fidelity
(3 coeffs · normal, gamma/sRGB, env-probe character lighting) needs a GL2+ shader path — a large,
isolated effort, not cut into this pass to avoid leaving the build broken.

---

## PARAMETER TABLE (recovered so far)

| Property | Original value | Source | Conf | Rebuild status |
|---|---|---|---|---|
| World gravity (pawn) | DefaultGravityZ **−2940 UU/s²** = −29.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo]` | CONF | **APPLIED** (was 22) |
| RB physics gravity scale (vehicles) | **0.66** → −19.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo] RBPhysicsGravityScaling` | CONF | applied to vehicle form |
| Transformation blend-in | **0.115 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendInTime` | CONF | **APPLIED** (crossfade) |
| Transformation blend-out | **0.25 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendOutTime` | CONF | **APPLIED** (crossfade) |
| Camera default FOV | **75° horizontal** (SmoothTime 0.4) | `Xe-TransCamera.ini [AnimatedFovCameraBehavior TnFovCameraBehavior] DefaultFOV` | CONF | **SUPERSEDED (Pass 11):** robot strategy instance OverTheShoulder TnFovCameraBehavior DefaultFOV 80 |
| Fine-aim (ADS) FOVs | 35 / 45 / 55 (close/med/far POI) | `Xe-TransGame.ini [TnPointOfInterest]` | CONF | **SUPERSEDED (Pass 11):** these are point-of-interest focus FOVs; fine aim uses TnPCS_FineAim FOV 45 (applied) |
| Fine-aim ground-speed mult | **0.5×** | `Xe-TransGame.ini [TnFineAimManager] _GroundSpeedMultiplier` | CONF | **SUPERSEDED (Pass 11):** APPLIED via SetSpeedMultiplier |
| Camera pawn-cylinder padding | R=30, H=30 UU | `Xe-TransCamera.ini [TnCamera]` | CONF | n/a (no camera collision yet) |
| Camera pawn fade start | 200 UU | `Xe-TransCamera.ini [TnCamera] PawnFadeStartDistance` | CONF | not yet |
| Ion Blaster fire interval | **0.065 s**; runtime cadence **~900 RPM** (one-shot timer reset to 0 per shot, fires when elapsed > 0.065 s, overshoot discarded, max one shot per tick) | `weapon.json` + native RE | CONF | **APPLIED** (Systems native-RE pass); Experimental's 923-RPM remainder patch deliberately NOT applied |
| Ion Blaster damage | **15** (InstantHit) | `weapon.json gameplay.InstantHitDamage` | CONF | correct ✓ |
| Ion Blaster magazine | **50** | `weapon.json MaxAmmoClipCount` | CONF | correct ✓ |
| Ion Blaster max reserve | **250** | `weapon.json MaxAmmoCount` | CONF | correct ✓ |
| Ion Blaster initial reserve | **150** | `weapon.json InitialReserveAmmoCount` | CONF | **FIXED** (was 250) |
| Ion Blaster reload time | **1.5 s** | `weapon.json WeaponReloadAnimTime` | CONF | **FIXED** (was 1.8) |
| Ion Blaster range | **30000 UU = 300 m** | `weapon.json WeaponRange` | CONF | **FIXED** (was 400) |
| Ion Blaster damage falloff | 1.0× ≤5000 UU → 0.5× @30000 UU | `weapon.json RangeDamageModifiers` | CONF | **APPLIED** |
| Ion Blaster per-shot spread | 0.08→0.18, +0.005/shot, 2.0 s cooldown | `weapon.json PerShotSpreadModifier` | CONF | **APPLIED** |
| Ion Blaster fine-aim spread | 0.5× | `weapon.json FineAimSpreadModifier` | CONF | **SUPERSEDED (Pass 11):** APPLIED |
| Ion Blaster equip/putdown | 0.2 / 0.5 s | `weapon.json EquipTime/PutDownTime` | CONF | not yet |
| Weapon socket | WeaponSocket_Primary, bone R_Arm03_Elbow_XB | `character.json sockets` | CONF | attached (rotation approx) |
| Robot ground speed | **550 UU/s = 5.5 m/s** | `TransGame.xxx Default__TnPlayerPawn.GroundSpeed` | CONF | **SUPERSEDED (Pass 11):** 14 m/s (Optimus_ROBODEF BaseGroundSpeed 1400) |
| Robot accel | **2048 UU/s² = 20.48 m/s²** | `Engine.xxx Default__Pawn.AccelRate` | CONF | **SUPERSEDED (Pass 11):** 120 m/s² (ROBODEF AccelRate 12000) |
| Robot air control | **0.70** | `Default__TnPlayerPawn.AirControl` | CONF | **SUPERSEDED (Pass 11):** 0.4 (ROBODEF) |
| Robot air speed | **1500 UU/s = 15 m/s** | `Default__TnPlayerPawn.AirSpeed` | CONF | **SUPERSEDED (Pass 11):** 12 m/s (ROBODEF AirSpeed 1200) |
| Robot max jump height | **625 UU = 6.25 m** | `Default__TnPawn._WorkingMovementCapabilities.MaxJumpHeight` | CONF | **SUPERSEDED (Pass 11):** 5.0 m (SharedAcrobatics.JumpHeight 500; JumpZ formula from ApplyTransformer; measured 5.12 m) |
| Pawn cylinder radius | **175 UU = 1.75 m** | `Default__TnPawn._WorkingMovementCapabilities.CylinderRadius` | CONF | **SUPERSEDED (Pass 11):** 2.0 m (Optimus_ROBODEF Collision) |
| Pawn cylinder half-height | **200 UU = 2.0 m** (full 4.0) | `…CylinderHeight` | CONF | **APPLIED** |
| Pawn base eye height | **80 UU = 0.8 m** (above centre → 2.8 m above feet) | `Default__TnPawn.BaseEyeHeight` | CONF | **SUPERSEDED (Pass 11):** Default__TnTransformer.BaseEyeHeight 150 → eye 3.5 m (trace origin); camera anchor 4.0 m (strategy Offset Z 200) |
| Robot walk pct | 0.5 (walk = 0.5× ground) | `Engine.xxx Default__Pawn.WalkingPct` | CONF | n/a (keyboard jogs) |
| Robot max fall speed | uncapped in movement | `…_WorkingMovementCapabilities.MaxFallSpeed=100000` | CONF | no terminal cap |
| HeightFog colour | **(234,91,116)** warm red-pink | `MP_IAC_Streets_ART_m HeightFogComponent.LightColor` | CONF | **APPLIED** (was blue-grey) |
| HeightFog density / start | 2e-5/UU (0.002/m) / 2048 UU | `…HeightFogComponent.Density/StartDistance` | CONF | APPLIED (GL_EXP, softened) |
| Vehicle max speed | **1500 UU/s = 15 m/s** | `Default__TnHoverCarSimulationBlueprint.MaxLinearSpeed` | CONF | **APPLIED** (was guessed 32) |
| Vehicle acceleration | **3000 UU/s² = 30 m/s²** | `…MaxLinearAcceleration` | CONF | **APPLIED** |
| Vehicle dash/boost speed | **5000 UU/s = 50 m/s** | `…DashSpeed` | CONF | **SUPERSEDED (Pass 11):** 30 m/s (HoverTruck_Physics DashSpeed 3000) |
| Vehicle dash duration | **0.3 s** | `…DashDuration` | CONF | **SUPERSEDED (Pass 11):** 0.5 s (HoverTruck_Physics) |
| Vehicle hover height | **200 UU = 2.0 m** | `…SuspensionRadius` | CONF | **SUPERSEDED (Pass 11):** 1.85 m (HoverTruck_Physics SuspensionRadius 185) |
| Vehicle jump speed | **1200 UU/s = 12 m/s** | `…JumpLinearSpeed` | CONF | APPLIED |
| Vehicle turn rate | **~π rad/s** (180°/s) | `…AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AI-only field; player steering rate PROV |
| Vehicle terminal velocity | 8000 UU/s = 80 m/s | `Default__TnTransformer.TerminalVelocity` | CONF | n/a (fall uncapped) |

---

## STREET COLLISION (RECOVERED this pass)
- **Authored collision participation recovered from props.json:** of 1952 placed static
  meshes, **1693 BLOCK** (CollideActors+BlockActors true) and **259 do not** (decorative:
  deco spheres, etc.). These flags come from the original ART/BASE actor/component properties.
- **UE3 collision model:** the player cylinder (non-zero extent) collides against each static
  mesh's collision. Default `UseSimpleBoxCollision=true` routes the player to the mesh's
  simplified BodySetup collision; architectural meshes commonly set per-poly. We reproduce the
  **per-poly path** (render geometry of blocking props) — faithful for the structural
  floors/walls/ramps that use it, an over-approximation for small simple-collision props.
- **Extraction extended:** new `AssetTools/scripts/wfc/vs_collision.py` regenerates
  `collision.glb` = pristine base (BSP-solid + blocking-volume hulls, kept as
  `collision_base.glb`) **+ 1693 blocking props** (instanced; file 5.46 MB → ~1.85 M world
  collision tris baked at load). Non-colliders excluded; oversize guard (>2000 m) excludes
  skydome/background shells (0 hit).
- **Verified:** player stands on the street floor at the authored FFA spawn (Y −724.5) and at
  4 spawns spanning ~180×164 m; auto-walk is now correctly **blocked by building walls** it
  previously passed through. Collision bounds Y [−800..−329].
- **Remaining:** simple-collision (BodySetup convex/box) extraction for props that use it;
  vehicle RB collision channel; moving-platform (InterpActor) dynamic collision (baked at rest
  position); exact MaxStepHeight/slope (pawn exe defaults).

## MAP / WORLD (investigated)
- `world.glb` is composed from **all three sublevels** (ART/AUDIO/BASE): BSP 2460 tris
  (ART only) + **1952 placed static meshes** + spawns/objectives. It is NOT a thin subset.
- Collision extent spans ~740 m (X) × 451 m (Z) in gltf metres (BSP-solid + blocking-volume
  convex hulls). Auto-walk forward from the FFA spawn is blocked by a wall after ~23 m — a
  building/volume ahead, not the map edge.
- **Known gaps (why it can look smaller/simpler than the original):**
  1. **Static-mesh collision not extracted** (`collision.json` note) — the 1952 prop meshes
     have no collision, so floors/ramps built from static meshes are not walkable and some
     structures are pass-through. Highest-value map task: build collision from prop render
     meshes where `collide/block=true` (data is in `props.json`).
  2. **PrefabInstance (16)** and **HeightFog** actors are not composed into `world.glb`
     (unhandled in `vs_map.py`). Prefabs hold modular set-dressing; missing them thins detail.
  3. Lighting is **baked into lightmaps** (StaticLightCollectionActor) which were **not
     extracted** — the single biggest reason the scene reads flatter than the original.

## LIGHTING / MATERIALS (investigated + partially fixed)
- Washed-out cause identified: the fixed-function renderer used global-ambient 0.35 + light-
  ambient 0.35, flooring every surface at ~0.70 brightness (no contrast). **Fixed [PROV]:**
  ambient lowered (global 0.14 / light 0.10), warm directional key, separate **specular**
  term (metallic highlights).
- **HeightFog RECOVERED [CONF]** from `MP_IAC_Streets_ART_m HeightFogComponent_11010`:
  LightColor **(234,91,116)** warm red-pink, Density **2e-5/UU = 0.002/m**, StartDistance
  2048 UU. Applied (GL_EXP, density softened to 0.0014/m for the play-area scale; clear colour
  warmed to match). This replaced the earlier **guessed blue-grey** fog.
- **BAKED LIGHTMAPS — FULLY RECOVERED, DECODED, AND RENDERED [CONF].** Streets is now lit by
  its original authored baked lightmaps instead of the stand-in directional/ambient.
  - **Serialization CRACKED (file_version 511, licensee 144).** After a component's tagged
    props the native `FStaticMeshComponentLODInfo` block is **big-endian**:
    `… [LightMapType=2] [LightGuids.Num: byte] [LightGuids: Num × FGuid(16)]`
    `[lead int32=0] 3 × ([tex: BE int32 export index][ScaleVector: 3 BE floats][1.0])`
    `[CoordinateScale: 2 BE floats] [CoordinateBias: 2 BE floats]`.
    It is a **directional lightmap** (3 coefficient textures); LightGuids[0] is a shared
    dominant-light GUID. The **texture is a positive export index into the ART package's own
    export table** (a seekfree forward-export whose name matches the `_LM` atlas) — this solved
    the "no imports / GUID" blocker.
  - **Values recovered:** per prop instance — atlas (coeff-0 `LightMapTexture2D`),
    CoordinateScale (e.g. 0.0625 = 1/16), CoordinateBias, and the coeff-0 ScaleVector (HDR).
    `AssetTools/scripts/wfc/vs_lightmap.py` parses all **1793 lightmapped components** and joins
    them to props (**1755/1952 props → lightmap**, 20 atlases used). Atlases decoded to PNG via
    umodel (1024² DXT1) in `…/MP_IAC_Streets/lightmaps/`.
  - **Rendering:** `vs_map.py` now emits TEXCOORD_1 + per-instance lightmap node extras;
    `world.glb` carries them; the loader attaches them to submeshes; the renderer draws
    lightmapped submeshes **unlit** then **multiplies by the atlas** (blend DST_COLOR·ZERO,
    UV1 via a texture matrix = uv1·CoordinateScale + CoordinateBias), with the **HDR ScaleVector
    applied via GL_COMBINE RGB_SCALE 4×**. 1962 submeshes lit. A/B toggle: `WFC_NOLIGHTMAP=1`.
  - **Verified** across spawn 0 / region 6 / region 18: correct per-region baked shadows and
    coloured bounce (purple, green/teal, warm), high contrast, bright lit doorways; no wrong-
    atlas / UV-flip / seam artefacts. Colour space: textures sampled as-is (sRGB-ish), modulate
    then HDR-scale — matches WFC's moody baked look.
  - **Remaining [PROV]:** only coeff-0 of the 3 directional coefficients is used (no per-pixel
    normal reconstruction — a shader refinement); ScaleVector >4 clamps (rare); BSP surfaces
    have no static-mesh lightmaps (BSP lightmaps are a separate path, not yet done).
  - **Pipeline ordering:** run `vs_lightmap.py` → `vs_map.py` (writes world.glb + base
    collision.glb) → `vs_collision.py` (restores full prop collision). vs_map resets
    collision.glb, so vs_collision must run last.
- **Emissive RECOVERED + APPLIED [CONF]:** Optimus's authored emissive textures
  (`textures/*_emissive.png`, the glow mask — blue optics/energon/Autobot vents on black) are
  loaded (derived as the `_basecolor`→`_emissive` sibling) and drawn as an **additive
  self-illumination pass** (GL_ONE/GL_ONE, unlit, depth-write off). Result: Optimus's eyes and
  energon details glow blue (iconic WFC look), from original data, not invented. Applies to
  robot/vehicle/weapon. [PROV] emissive *intensity* scale not recovered (drawn at 1×).
- Still missing: **normal/specular maps** (roles extracted; need a programmable GL path),
  map/environment emissive (different naming), tone-mapping / bloom / DOF.

## CONFIRMED ORIGINAL (authored data)
- Streets map = three sublevels **BASE** (gameplay: 24 FFA + 60 team starts, 58 blocking
  volumes, pickups, objectives) + **ART** (visual: BSP 2460 tris, 34 StaticMeshActors,
  16 PrefabInstances, 25 decals, HeightFog) + **AUDIO** (40 AmbientSound + 30 Hm spatial
  emitters). Source: `maps/MP_IAC_Streets_*_m.json`.
- Gravity, transform blend times, camera FOV, Ion Blaster stats — see table.

## HIGH-CONFIDENCE RECONSTRUCTION
- GLB skinning / animation sampling; material base-colour; collision ground query.

## PARTIALLY CONFIRMED
- Vehicle movement: forms (TnTruckForm/TnCarForm) are **modifier** layers (=1.0) over
  compiled base values; WFC vehicles use RB thruster/suspension/hover physics. Base
  constants live in `default.xex` — NOT yet recovered.
- Map completeness: 1952 props + BSP composed, but **PrefabInstance (16)** and **HeightFog**
  not yet composed; **static-mesh collision not extracted** (only BSP-solid + blocking hulls).

## WEAPON SOCKET (RECOVERED this pass)
- `WeaponSocket_Primary` relative transform (character.json): loc_ue [-40,0,0],
  rot_ue [pitch 0, yaw 31311 = 172°, roll 5461 = 30°]. Converted to a gltf-space socket
  matrix (via `vs_common.ue_rot`/`ue_to_gltf_matrix`) and applied — the Ion Blaster now holds
  with the correct orientation (barrel forward along the arm), not translation-only.

## CAMERA (partial)
- Recovered: pivot/eye height 2.8 m [CONF] (CollisionHeight 200 + BaseEyeHeight 80 UU).
  Flying-cam pitch range ±60°, first-person rotation ±45°, flying AnchorOffset [0,0,220 UU].
- STILL [PROV]: ground third-person **follow distance** — WFC selects it from an orbit-distance
  list (`TnLocationOffsetCameraBehavior.CurrentOrbitDistanceIndex`); the distances live in a
  camera data asset / `DefaultCamera.ini` that was not extracted. Keeping 9 m. Mouse
  sensitivity, ground-cam pitch limits still provisional.

## VEHICLE PHYSICS (RECOVERED this pass)
- WFC ground vehicles **hover**; Optimus's truck uses **`Default__TnHoverCarSimulationBlueprint`**
  (TransGame.xxx). The `TnCarForm`/`TnTruckForm` CDOs only carry `=1.0` modifiers; the base
  values live on the *Simulation* blueprint classes (which I'd missed earlier). Recovered +
  applied: max speed 15 m/s, accel 30 m/s², dash/boost 50 m/s (0.3 s burst), hover height 2 m,
  jump 12 m/s, turn ~π rad/s, terminal 80 m/s. Verified: vehicle hovers 2 m above the street
  and cruises ~13.6→15 m/s. (Non-hover `TnCarSimulationBlueprint` = MaxSpeed 500 UU; unused.)
- Remaining: reverse speed + braking/damping constants (via LinearDamping, not a single value);
  per-axis turn rate; exact dash-as-impulse vs the accel-ramp approximation; velocity transfer
  at transform (currently zeroed).

## PROVISIONAL / APPROXIMATE (rebuild guesses — NOT evidence)
- ~~Camera follow distance (9 m); ground-cam pitch limits~~ → 8 m / ±75° CONF (Pass 11). Mouse sensitivity still PROV.
- ~~MaxStepHeight (kStepUp 0.6 m)~~ → 0.35 m CONF (Gameplay Pass 10, TnRobotForm; Systems also found no override of the Engine.Pawn default 35 UU).
- Audio master level 0.5 and mixer-category / reverb treatment (weapon cue radii now CONF, Systems PASS 7; non-weapon transform/land cues still generic falloff).
- Locomotion crossfade time (0.15 s) for non-moving transitions (idle↔moving is CONF 0.2 s, Pass 10).

## AUDIO (investigated + fixed)
- Original Streets audio = 40 `AmbientSound` + 17 `HmAmbientSoundVolumeEmitter` +
  13 `HmAmbientSoundLineEmitter` (spatial bed) with SoundCues; weapon/transform cues are
  FMOD SoundCues. Exact per-cue attenuation radii/reverb not yet extracted.
- "Too loud / not spatial" cause: the mixer played every cue at full volume, mono, no
  attenuation. **Fixed:** master level 0.5 [PROV], and a 3D path (distance attenuation +
  equal-power stereo pan vs the camera listener) via `IAudio::playAt`/`setListener`. Fire is
  positioned at the muzzle; land/transform/reload at the pawn.
- Remaining: real SoundCue min/max radii + falloff curves, the ambient emitter bed, reverb,
  interior/exterior treatment, concurrency/voice limits, pitch randomization.

## NOT YET IMPLEMENTED
- Normal/specular/emissive materials; lightmaps/baked lighting; HeightFog; post FX (bloom/DOF).
- ADS/fine-aim mode; camera recoil/shake; per-shot spread visualization.
- Prefab geometry; static-mesh collision; spatial/reverb audio.

---

## OPEN QUESTIONS → EVIDENCE NEEDED
- Robot GroundSpeed/JumpZ/AccelRate: read `TnPawn`/robot-form class defaults in `default.xex`
  (Ghidra/ReVa) or measure root motion from `Nav_*` clips.
- Camera follow distance/offset: HM camera behavior assets (cooked packages) or exe.
- Vehicle base speed/accel/turn: `TnCarForm`/`TnTruckForm` compiled defaults in exe.
- Lighting: lightmaps not extracted; dominant directional light direction/colour from map.
- Remaining unimplemented abilities (in no iconic preset; class pools only): DecoyTrap, HardLock, Disguise, AbilityJammer,
  TransformDisruptor, MarkTarget … are listed per slot and reported unimplemented [PARTIAL].