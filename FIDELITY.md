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

## MILESTONE 67 — SPRITE SUBUV (2026-10-06)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| SubUV update | Every tick for every live particle; Linear(_Blend) from the SubImageIndex curve with frac interp; Random(_Blend) re-picks on RandomImageTime (lifetime fraction, default 0 = every tick); second cell = next, wrapping | RE pass 5 s16 (update runner 0x1F) | CONFIRMED | M67 (was: Random picked once at spawn, no blending) |
| SubUV blend | The fill writes both cells + interp; ParticleSubUV lerps the two samples | RE s16 (shader lerp HIGH) | HIGH | M67: sprite attribute 6 + matc ParticleSubUV mix |
| SubUVDirect / Select | Direct UV = (pos + size x corner) x scale (texel units HIGH); Select unused in MP | RE s16 | PARTIAL | not applied (10 MP LODs) |

## MILESTONES 65-66 — FIXED-AXIS RIBBONS, SPRITE LOCK-AXIS / VELOCITY MODES (2026-10-06)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Beam2 / Trail2 BillboardSettings | Direction other than CameraFacing offsets along a fixed axis (component row or world), Alignment side scales | RE s13 addendum 3 (HIGH, emulated shader) | HIGH | M65; authored on 4 templates, none in MP data; geometry checked numerically |
| Sprite LockAxisFlags | Emitter-level in WFC; local-space LODs use component rows, else world axes; lock 1-6 fixed plane + in-plane rotation; ROTATE_* turn about the axis, rotation ignored | RE s15 + addenda (CPU CONFIRMED, world formulas HIGH) | HIGH | M66: 178 MP emitters; ground burst rings lie flat (VISUALLY VERIFIED) |
| Sprite PSA_Velocity | Width along cross(camera - particle, D); length Size.y, V 0 leading; rotation ignored; stationary / view-aligned collapse | RE s15 | HIGH | M66: width axis was mirrored; stationary fallback removed |

## MILESTONES 63-64 — RIBBON WIDTH, LOCATIONEMITTER / PARTICLE TRAILS; MOLTEN PERFORMANCE (2026-10-06)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Ribbon width | Beam / trail vertex pairs are offset by (2V - 1) x Size x cross(view, dir): full width 2 x Size; we drew Size | RE s13 addendum 2 (Xenos VS emulated) | HIGH | M63: authored widths |
| LocationEmitter / Direct | Payload decoded from the LOD tail; selection, space conversion, inheritance, born-this-frame rule, Direct re-snap per RE s14 | RE s14 (spawn / update runners) | CONFIRMED (sub-frame terms PARTIAL) | M64: Streets 57 / 59 modules bound |
| Trail2 with LocationEmitter | One chain through the emitter's own particles in spawn order, <= 1 per tick, cap MaxTrailCount x MaxParticleInTrailCount | RE s14 | CONFIRMED | M64: shell casings trail smoke (was no ribbon) |
| Molten "performance drop looking down" | Not reproduced on the current build: 2560x1440 look-down sweep (76 spawns x 8 views) median GPU 0.64 ms, max 2.2 ms; real-time CPU frame 1.4-1.8 ms looking down vs 2.0-2.2 forward (fewer draws); no first-use creation after load. Likely the pre-M54 / M58 first-draw stalls on the human's build | WFC_FRAMELOG, WFC_PERFLOG, WFC_RENDERSTATS | HIGH (not reproduced) | No change; WFC_FRAMELOG at the spot if it recurs |

## MILESTONES 61-62 — BEAM / TRAIL UV LAYOUT, GORGE VERTEX LIGHTMAPS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Tiling values | No MP effect authors TextureTile / TextureTileDistance / Sheets / bTilePerParticle (735 Beam2 / Trail2 LODs); CDO TextureTile 1, Sheets 1 | pstream survey; Engine CDOs | CONFIRMED | exported with defaults |
| Beam UV | U along (0 source -> 1 target, per emitted pair incl. noise sub-steps), V 0 / 1 across; TextureTile never applied by the beam fill; no scrolling | RE pass 5 s13 (0x830298E8 UV writes) | CONFIRMED (world side of V 0: UNKNOWN, Xenon VS) | already U = r |
| Trail UV | U = 0 at the head = NEWEST particle (Spawn relinks the new particle as head), growing to the oldest by cumulative distance x TextureTile, clamped; bTilePerParticle per segment | RE s13 + addendum (0x8301A700, 0x83048E40); authored trail textures fade from U 0 (iontrail_01, RingsTrail) | CONFIRMED | M61 / M61b: was index-based from the OLDEST point |
| Gorge vertex lightmaps | Export duplicated 40 of 1576 cooked vertices across two sections | AssetTools a7b9ef0 _WFC_SRCVERT (exact pskx face pairing) | CONFIRMED | M62: samples re-ordered through the cooked index; all 4 sections bind |

## MILESTONES 59-60 — PREWARM REPLAY, BEAM SOURCE / TARGET METHODS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Prewarm across matches | Gameplay prewarms once per chassis when cached; each map load reset the renderer's programs / textures, and requests made before render data loaded were dropped | two-match flow, WFC_RENDERSTATS | CONFIRMED | M59: requests remembered and replayed after every map load. With Gameplay 2f258b6 (all match chassis cached at the match load): no mid-match chassis load, no first-use at transforms |
| Beam source / target | Particle methods never read a particle (BeamMethod Target): Default path. Emitter source = component origin; Emitter target needs a name, else the distribution. Default = distribution at EmitterTime through the component LocalToWorld (raw if bAbsolute); a named Default target reads the instance parameter first. UserSet = SetBeam*Point array, empty -> distribution. Re-resolved every tick unless locked. Tangents: Direct / Emitter = component X; Distribution raw; x Strength. Distance method: source + X x Distance | RE pass 5 s11: ResolveSourceData 0x8302F320 / ResolveTargetData 0x8302F738 | CONFIRMED | M60: repair squib arcs from its Source curve into the centre; TF_Death_Buildup lightning to its Target curve; segment beams unchanged (VISUALLY VERIFIED). Base path Hermite with the tangents (stock, HIGH) |
| Seed / Berth darker than 06b | The maps' authored ColorCorrectionTextures are contrast S-curves (Seed desaturation40: 33->18, 99->82; Berth clut_mp40: 33->21, 99->108), applied since M25 in UE3 order (grade, gamma, LUT); 06b applied none. NOCLUT: Seed median 12 -> 24, Berth 9 -> 23 | CLUT diagonal; toggles at Experimental's spawn | HIGH (human check vs original footage kept) | No change |
| Gorge vertex lightmaps | 1576 cooked samples vs 1616 glTF vertices (two sections, ~40 duplicated at export) | renderer warning | CONFIRMED (export) | AssetTools asked for a per-vertex cooked index |

## MILESTONES 56-58 — BEAM NOISE + SINE WAVE (native beam fill), FRONTEND EMITTER PREWARM (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Beam module binding | Cooked beam LODs keep no Modules array. Each LOD's serialized bytes reference its BeamSource / BeamTarget / BeamNoise / BeamSineWave exports (one unique big-endian index hit each). | RE pass 5 addendum; raw scan, 40 Streets beam LODs | HIGH | build_map_fx `beam_trail.modules`; raw distributions decoded from float bit patterns; defaults = Engine.Default__ParticleModuleBeamNoise / BeamSource / BeamTarget (NoiseRange / NoiseSpeed 50, NoiseTangentStrength 250, Source / Target strength 25) |
| BeamSineWave (WFC addition) | At every interpolated point: offset = sum Dir x Amp x sin(2 pi ((Speed t + d) / Period + Phase)), d = r x length (UU), t = particle age. The offset is rotated by the quaternion taking +Z onto the beam and faded by min(6r, 1) x min(6(1-r), 1). | RE 9i: native fill 0x830298E8 / 0x83029618 (it had never been defined as a function) | CONFIRMED | M57: repair / drain beams' strands twist and are pinned at both ends (VISUALLY VERIFIED) |
| Beam noise generation | N = Frequency (or int(appSRand x (F - Low) + Low)); N + 1 points, Point[i] = NoiseRange(i / (N + 1)). NoiseLockTime < 0 never re-drawn, <= 1e-4 every tick, > 0 on a timer; bSmooth re-draws into a target. | RE 9h: Spawn 0x8301F6F8 / Update 0x8301F928 | CONFIRMED | M56 |
| Beam noise render | Applied only with bLowFreq_Enabled. Offset = point x noise scale (1.0: FrequencyDistance is never authored) x NoiseRangeScale. bSmooth steps toward the target at NoiseSpeed x seconds since the re-draw, snapping within NoiseLockRadius. Curve tangents = normalize(N[i+1] - N[i-1]) x lerp(SourceStrength, TargetStrength, r); NoiseTension / NoiseTangentStrength have no effect. NoiseTessellation Hermite sub-steps. | RE 9i / 9j | CONFIRMED (component-space rotation of the offsets: HIGH, call chain) | M56 / M57: turret repair ray / repair squib render as lightning re-drawn every tick (VISUALLY VERIFIED) |
| Ribbon joins | Beams and trails drew one independent quad per segment, so kinks opened gaps. They are now connected strips: per-point side from the averaged direction, as UE3 shares vertex pairs. | RE 9j (vertex direction normalize(cur - prev)) | HIGH | M56 |
| Frontend first-frame stalls | The title's placed emitters compiled on their first drawn frames (Lightning_Anim_03 45 ms, Lightning_02 texture 43 ms, ...) because M54 skips the map effect prewarm for menus. | WFC_RENDERSTATS; Frontend WFC_FRAMEPROF 269 / 215 ms | CONFIRMED | M58 prewarmPlacedFx: the title's 14 materials in 204 ms under the loading screen; 0 first-use items after the load |
| Streets references after AssetTools 439a8ce | The 14 repaired Streets materials (Pixel_4_NORM / Mask, None-texture inheritance) are not visible at the 5 reference cameras: the captures are pixel-identical before and after the data change. | suite runs 19:18 vs after the 19:32 world.glb | CONFIRMED | References unchanged |
| Vehicle muzzle sockets | Every shot alternates WeaponSocket_Primary / Primary2; the projectile spawns at the flash's socket. | RE 9g (HmWeapon.OnPlayFireEffects / ChangeSocket) | CONFIRMED | Gameplay 8253a62. The flash belongs to Systems' WeaponFx (forwarded). |

## MILESTONES 51-55 — FOCUSED VISUAL-FIDELITY PASS (newest human playtest) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Molten floor "flicker / box" | A hard dark rectangle from the screen centre to one corner on the lit floor, moving with the camera. RainPuddles_Mat (MoltenRainPuddle_MatInst) reads SceneTexture at ScreenPosition + ripple offset. WFC cooks the expression flag as `ScreenAlign` (not `bScreenAlign`): matc ignored it and fed clip-space xy (-1..1) into a [0,1] lookup. | Toggle bisection: DOF / bloom / map FX / character shadows / Canvas tiles unchanged; WFC_PICK on the puddle plane; cooked `MaterialExpressionScreenPosition_16345 {'ScreenAlign': True}` | CONFIRMED | M51: matc honours `ScreenAlign`. wfcSceneColor / wfcScreenUV use UE3's v-down screen UV (ScreenPositionScaleBias (0.5, -0.5)) on the bottom-up GL copy. Box gone (VISUALLY VERIFIED, puddle viewpoints). |
| Molten "performance drop looking down" | Not reproduced. GPU frame 0.3-0.7 ms at the puddle / lava viewpoints (WFC_FRAMELOG), no `wfc spike`. The only measured mid-match hitch was the first transform (M53). | WFC_FRAMELOG, WFC_RENDERSTATS sweeps | UNKNOWN | Ask the human for the exact spot (WFC_FRAMELOG gives per-frame GPU time + draw counts + camera). |
| Orbital Debris sky black | The authored sky is SpaceDome_STATMESH x6 (Spacedome_Nebula_MAT, radius ~26.6 km) plus the Cybertron_2D_Planet card. The 20 km far plane clipped the dome: black in the screen centre, nebula only at the view edges (planar far plane vs a sphere). UE3 uses an infinite far plane. | world.glb dome bounds; captures up1 / up4 69 % / 57 % black | CONFIRMED | M51: far plane covers the loaded world's extent. up1 69 -> 14 % black, up4 57 -> 6 % (remaining black = authored space between stars). Authored nebula + starfield (VISUALLY VERIFIED). |
| Title Cybertron dark craters | BlastBuildings / BlastDebris (2 of 10 planet meshes) cook no lightmap (LightMap None, no OverriddenLightMapResolution, Static channel). The only Static-channel lights are #7 (radius 40 m, 74 m short of both meshes) and #0 (brightness 0.05). The orange specks are emissive. The other 8 meshes' lightmaps and the LowShell override are bound (AssetTools confirms). | lighting.json lights / props; cooked component props | HIGH (authored) | No change. Uncached per-pixel lighting would add only light #0 at 0.05. |
| Projectile cubes | A Gameplay placeholder (World.cpp drawBox). Projectiles are particle-only: FlightEffect PSC = body, ExplosionEffect at hit / HitNormal. | RE projectile_effect_bindings.json; AssetTools weapon.json projectile_visual | CONFIRMED | Renderer API ready (spawn / setTransform / stop). Contract sent to Gameplay. M52: template library adds the class-data templates the level packages lack (Ion Blaster muzzle / impact, A_Plasma_Trail, KamikazeMine, Exp_2500cb, Rumble). Flight bodies render on a moving source (VISUALLY VERIFIED). |
| Scout fires from the left | Gameplay's origin chain is right (posed WeaponSocket_Primary x meshMatrix, no offset). Car4 / Car2 author WeaponSocket_Primary (left gun) and WeaponSocket_Primary2 (R_GunRobo01_XT); vehicle weapons list both as MuzzleFlashSockets (RE: alternating for rockets). Runtime uses only Primary. | character.json sockets; RE pass 5 | HIGH (alternation P: RE tracing) | Gameplay: round-robin per shot. |
| Scout transform "box-like" | Car4 R->V and V->R captured every frame (lockstep): authored fold, no graybox / unposed / stale mesh frame. Gameplay swaps form + skinPose in one tick. | work/m50/xf frames 150-300 | VISUALLY VERIFIED (no rendering defect) | The measured hitch on the first transform was M53. |
| First-transform hitch | 65 ms frame (166 ms tick gap): vehicle program (59 ms) + 3 textures (51 ms) created on its first visible frame | WFC_RENDERSTATS first-use | CONFIRMED | M53: IRenderer::prewarmDynamicMesh; Integration adds the call at Character model assignment. Spike gone in a trial build. |
| Menu / match-start stalls | The effect / weapon prewarm ran on frame 2: PartyLobby 158 programs 1174 ms, Streets 161 programs 1.2 s, both after the loading screen. Unload deleted every program, so revisits recompiled. | WFC_RENDERSTATS, WFC_HITCHLOG, Frontend WFC_FRAMEPROF | CONFIRMED | M54: prewarm at the end of the world upload (16 ms yield slices); none for frontend scenes; linked programs cached across loads by source (LRU 1500); preview bodies prewarm at load. Lobby revisit 39 / 39 reused; no tick hitch after the Streets load; title longest load gap 187 -> 73 ms. |
| High refresh / jitter | Uncapped Streets with fast turning + strafe + fire + transform: CPU frame 1.4-3.5 ms; no GL errors. The only hitches were M53 / M54 (fixed). | WFC_PERFLOG / WFC_HITCHLOG | HIGH | No presentation / interpolation change (the character high-refresh fix is untouched). |
| Beam2 taper | TaperCount = InterpolationPoints + 1; width_i = size x TaperFactor(r) x TaperScale(r), r along the beam, fixed at spawn | RE pass 5 s9 (Function_8302E898) | CONFIRMED (native) | M55: repair beam tapers from 0 at the source (VISUALLY VERIFIED). |
| Trail2 tessellation | TessellationFactor vertex pairs per segment (TessellationFactorDistance does not change the count) | RE pass 5 s9 (Function_83019600) | CONFIRMED count / HIGH Hermite (stock UE3, TessellationStrength) | M55. Noise / sine wave: M56-M57; UV layout M61. |
| AMD stability | 0 GL debug errors, 0 out-of-bounds draws, 0 context resets across every run of this pass (Molten, Debris, Streets real time, frontend flow x2) | KHR_debug callback, subInBounds | HIGH | M43-M45 guards kept. |

## MILESTONE 45 — OUT-OF-BOUNDS SKINNED DRAWS (AMD stability, real defect) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Stale sub-mesh ranges on a reused skinned output | assets::skinPose kept the output's previous subs / uv when only their count matched the new model; a reused output (robot <-> vehicle form, chassis change: equal section counts) carried another model's index ranges (e.g. [2733, +26937) on a 26388-index, 7377-vertex mesh). Every such draw fetched past the index buffer on the GPU: undefined behaviour an AMD driver may answer with a page fault / reset | M43 guard on the release path: 20 rejected sub-meshes after the Optimus spawn + one 364 ms GPU frame | CONFIRMED defect (reset link HIGH, not reproduced as a reset here) | subs / uv always taken from the posed model; release path 0 rejected, 0 long GPU frames |

## MILESTONE 44 — TRAIL2 / BEAM2 RIBBONS, PSC PARAMETERS (vehicle / tracer / beam effects from data) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Beam2 | each live particle draws a camera-facing ribbon source -> target (width = particle size, colour / alpha over life, the emitter's material); live beams capped by the type data's MaxBeamCount (authored 1 on Tracer_RepairBeam_FX: 98 stacked beams before the cap) | pstream TypeDataModule props | HIGH (taper + InterpolationPoints M55; noise + sine wave M56 / M57, CONFIRMED native fill) | WFC_FXTEST ">FX_RepairBeam_p.FX.Tracer_RepairBeam_FX": one electric HealBeam_MAT beam + its source rings |
| Trail2 | trails spawn per distance travelled (UE3 SpawnPerUnit; required-module rate is 0): one particle per 5 UU of source movement, the ribbon follows the source's recent path fading over the particle lifetime; a trail spawned along a segment (tracer) draws one ribbon start -> end | Tracer_AssaultRifle / Tracer_SniperRifle / Trails_Jet_A modules | PARTIAL (tessellation applied in M55; tiling distance, bConnectToSource) | Assault Rifle / Sniper tracer smoke ribbons render from their own materials (Trail_Smoke_10_MAT_INST / Tracer_Smoke_MAT) |
| PSC parameters | IRenderer::setParticleEffectParam(handle, name, rgba): "Color" -> ColorByParameter (priority over the decoded DefaultColor), "Size" -> ParticleModuleSizeMultiplyLife whose LifeMultiplier is a DistributionVectorParticleParameter naming it (M46, RE pass 4: CarHover_A / D_01_FX _9193, hover_plane_FX _476 / _6182; identity mapping, Constant (1,1,1) fallback). Applied per LOD that references the module's export index in its serialized bytes (M47, RE raw scan confirmed: CarHover_A's shared _9193 in all 7 level-0 LODs, no level-1 LOD; build: CarHover_A 7, CarHover_D 9, hover_plane 2, all LOD 0) | RE pass 4 (HoverFX 'Size' = min(1, thrust) x socket scale; BoostFx 'Color' = lerp(EnergonColor, Yellow), alpha 100..255) | CONFIRMED rules (RE) / CONFIRMED data (module) / HIGH (LOD reference by raw index) | hover_plane / CarHover / hover_tank render and scale with Size; Systems wires per socket |
| Vehicle FX ownership | every chassis' authored HoverFX / BoostFx / JumpFX templates and sockets are in the AssetTools roster (e.g. Jet4: hover_plane_FX x5 Hover_* sockets, Afterburner_A_FX, Trails_Jet_A_FX); all FX_Navigation_p templates are in each map's template library (23) | mp_characters.json vehicle.fx | CONFIRMED data | Systems drives them through spawnParticleEffect / setParticleEffectTransform / setParticleEffectParam (only the Optimus hand reconstruction exists today) |

## MILESTONE 43 — AMD STABILITY AUDIT AND DIAGNOSTICS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Driver errors | always-on GL debug output (KHR_debug callback, non-debug context: errors still reported, verified by a WFC_GLDEBUG=selftest invalid enum) + context reset status poll (ARB_robustness / GL 4.5) + GPU frame timer (GL_TIME_ELAPSED ring, never stalls; > 250 ms logged). VISUALCHECK lines carry gpu=ms and glDebug; driver errors fail the verdict | this machine runs the same AMD driver family (26.6.x) | — | 9-match frontend map chain (Streets, Seed, Berth, Gorge, Complex, Rust, Debris, Molten, Streets; persistent renderer): 0 GL debug errors, 0 context resets, 0 out-of-bounds draws, 0 incomplete framebuffers, 0 shader failures; Streets GPU frame 1.5-3 ms |
| Out-of-bounds vertex fetch | no draw validated its index range (sub-mesh past the index buffer / indices past the vertex buffer): a GPU out-of-bounds fetch can page-fault an AMD GPU and reset the driver | code audit | CONFIRMED gap (no occurrence observed) | static and dynamic sub-meshes validated before submission (out-of-bounds ones dropped, logged) |
| Vertex lightmap texture width | (count x 3) RGB32F texture; > GL_MAX_TEXTURE_SIZE would be invalid | max count 6351 (Debris) | guard | not bound above the limit |
| Own bug caught by the new diagnostics | the first GPU timer version could EndQuery without a begun query (GL_INVALID_OPERATION); reported at once by the callback, fixed before commit | gputime.log | — | — |
| GL objects across matches | renderer-owned objects constant (1 buffer / 2 FBOs / 2 RBOs / 1 VAO / 2 programs); live textures +1 per match, all in uiTextures (GFx side) | match.glCensus | CONFIRMED | Frontend handoff |
| Not reproduced | no reset / GL error on this AMD GPU in the chain; the human's resets are UNKNOWN cause. Recommended platform change (not renderer-owned): create the context with WGL_ARB_create_context(_robustness) (robust access + reset notification; a debug context under an env switch) so a reset is reported and recoverable instead of fatal | — | UNKNOWN | — |

## MILESTONE 42 — EVERY MP WEAPON'S AUTHORED MATERIAL (untextured Sniper, grey Scientist) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Sniper (Null Ray) untextured | the render data compiled only weapon materials map geometry referenced (pickups / crates: Ion Blaster, EMP Shotgun, Grenade Launcher, rockets, flak) plus WEP_IonBlaster_MATINST; every other held weapon's material (WEP_SniperRifle_p.WEP_SniperRifle_MAT, Burst Rifle, Assault Rifle, ...) was absent, so the weapon drew its glTF fallback (flat grey). The weapon glb names the original material (extras.wfc_material) and every MP map cooks it | weapon.json / weapon.glb, materials_glsl.json (6 WEP_ before) | CONFIRMED | tools/render/weapon_materials.py (all 54 exported weapon material slots) -> build_render_data @weapons: Streets 57 / 57, Gorge 57 / 57 WEP_ materials compile. Starscream (WFC_CHASSIS=Jet) Sniper: flat grey -> authored metal + cyan emissive (VISUALLY VERIFIED) |
| Autobot Scientist "grey / missing portions" | the grey mass is its primary weapon (Burst Rifle) drawn with the fallback, same cause; the Air Raid (Jet4) body and its 4 materials compile and bind (Norm / SpecPwr / Grunge / CustAB / fractal / Metals cube; MIC Cust_Color_A grey 0.54, B red); frontend preview colours are the palette pick (ResetCharacterFromName randomises, so a grey roll is authentic). Other classes looked complete because their default weapons (EMP Shotgun, Ion Blaster) were among the few compiled | direct preview render vs character-select art; frontend CAC capture; WFC_CHASSIS=Jet4 before / after | CONFIRMED | same fix; Burst Rifle now textured with its cyan emissive |
| AssetTools audit (dc2d0ee) items | (1) TextureCubes not exported: the render data decodes CHR_Metals_CUBEMAP3D faces itself (tex/*_f0..5); (4) Normal_Map = None on 12 Cust MICs incl. Sideswipe (renders correctly): not the Scientist cause | — | noted | — |

## MILESTONE 41 — HUMAN PLAYTEST PASS: TITLE BLACK SHIPS, TITLE VIGNETTE OWNERSHIP (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Title ships / debris solid black | the title's 51 movable InterpActors (transports, debris; UI_FrontEnd_capture_VIG_m / UI_FrontEnd_m) author LightingChannels = {Dynamic} with their own enabled LightEnvironmentComponent (engine default bEnabled False). UE3 lights them with the Dynamic-channel lights (SkyLight 2.0 (142,173,210), PointLight_8444 15.0 cyan r 250 m); the renderer lit every non-lightmapped world submesh with the Static set only (one 5.0 / 40 m point light): near black. Albedo was intact (WFC_ALBEDO), lighting was missing (WFC_LIGHTINGONLY) | cooked components (props_authored lighting_channels_on, LightEnvironmentComponent_7904 bEnabled); debug views | CONFIRMED (data + UE3 channel overlap) | build_lighting component_flags dynamic_channel; such submeshes take the Dynamic-channel environment at their current position each frame. Title near-black 9.9 % -> 2.5 %; the M08b human frame's black transport is lit with its hull detail. Streets suite identical (its 8 Dynamic-only components unchanged on screen). Not a global ambient change |
| Empty-channel components | the title's 41 empty-channel components are glow spheres / light cones / debris cards / SpaceDome with bAcceptsLights False: emissive only is right | props_authored | CONFIRMED | unchanged |
| Title vignette side strips (HUMAN-CONFIRMED) | reproduced on M08b at 1280x720, 1920x1080, 2560x1440 windowed and 2560x1440 fullscreen: darkening covers 87.5 % of the width. The vignette is a GFx asset (FrontEnd_GFX_I24, black, alpha 58 at edges -> 0 centre, scale-9 clip screenSoftEdges_mc); FrontEnd / Hud stages are 1120x720 (14:9). The movie's own Stage.onResize stretches it to Stage.width x Stage.height; the GFx host reported the authored 1120 and sent onResize only to noScale movies | SWF headers, I24 pixels, renderer-only title frame has no vignette, Frontend's AS reading | CONFIRMED | ownership: Frontend GFx host (not renderer viewport / scissor / quad). Fixed in agents/frontend (showAll movies get the visible area as Stage.width / height + onResize; authored art unchanged); recheck pending on a merged preview |

## MILESTONE 35 — VECTOR-CHANNEL PROOF INHERITED BY INSTANCES (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| LogoAUT / LogoDEC_MATINST GLSL error (Integration M08b) | instances of ParticleBase_BW_MAT, whose graph is already proven to use VectorParameter channel outputs (vector_channel_proven.txt: AppendVector(UVandOffset.B, UVandOffset.B) is invalid as vec4 + vec4); matc tested only the instance path, so instances fell back to the full-vector reading (vec4(vec4, vec4)) | Integration m08b_soak wfc.log; matc trace | CONFIRMED | matc applies the proof to the instance's master / chain. Streets: only the two Logo instances change; 0 compile failures |

## MILESTONE 34 — PARTICLE ColorByParameter DefaultColor (per template) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Template colour | the Ion blue Systems used is MuzzleFlash / Tracer_AssaultRifle's own ColorByParameter DefaultColor, not a weapon tint (the weapon's EnergonColor parameter is (255,255,255,A=0) = no override); each template carries its own | Systems a1d38c5 notes; LOD stream bytes | CONFIRMED (Systems / data) | runtime ColorByParameter order: component InstanceParameter, caller colour, decoded DefaultColor, white |
| Decode | ARGB FColor after the module-order list (count = module records, bytes 0..n-1), a counted byte list and 20 bytes [HIGH: reproduces ff 33 19 ff on every AssaultRifle emitter]; other layouts: the stream's single A = 0xFF value after a zero int [MEDIUM]. Streets: 435 / 451 ColorByParameter emitters decoded, every template internally consistent (AssaultRifle (51,25,255), EMP shotgun (255,65,65), Sniper (255,12,12)); EMP red matches its editor thumbnail (the Sniper / AssaultRifle thumbnails are identical, generic) | build_map_fx.py default_color(); work/m34_thumbs.png | HIGH / MEDIUM | WFC_FXTEST without a caller colour: EMP red, Sniper red (was blown-out white), AssaultRifle Ion blue |

## MILESTONE 33 — MATINEE MATERIAL PARAMETERS (lobby faction emblems) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Faction emblems invisible | UI_CharacterCustomization's MaterialInstanceActors (8803 / 16331 Autobot logo / glow, 5865 / 1120 Decepticon) drive their MICs' Highlighted / Opacity through Matinee; compiled with the authored defaults (Opacity 0) the emblems were constant-folded to emissive 0 (additive: invisible) | materials_glsl.json; Frontend matinee evaluation | CONFIRMED | build_materials: every MaterialInstanceActor's MIC compiles with runtime parameters (no map names) + material_instance_actors.json; IRenderer::setFrontendMaterialParam(actor, param, value) routes to the MIC (held until changed; unset = authored). WFC_MATPARAM: Opacity / Highlighted 1 shows both emblems and glows, unset stays invisible (VISUALLY VERIFIED) |

## MILESTONE 32 — RUNTIME PARTICLE TEMPLATES (weapon muzzle / impact FX from data) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Where weapon FX live | the FX_<weapon>_p packages are empty stubs; every MP weapon template is cooked into each MP map's BASE package (Streets BASE: 130 ParticleSystems, 789 emitters, 784 fully assigned by pstream) | cooked export census (work/m32_ps_index_all.json) | CONFIRMED | build_map_fx.py library(): all map-package ParticleSystems join map_fx_runtime.json systems (Streets +131); their materials compile via build_render_data (@fxlib; Streets 325 -> 405 materials, existing GLSL byte-identical) |
| Spawn API | IRenderer::spawnParticleEffect(tpl, pos, forward, up, color) / spawnParticleEffectSegment(tpl, start, end, color) / setParticleEffectTransform / stopParticleEffect / liveParticleEffects. glTF metres; forward = template +X, up = +Z. Unknown template -> -1, logged once. One-shot effects release when finished; loops until stopped; all released by unloadMapRenderData | WFC_FXTEST (4 templates every 30 frames): handles, live count plateaus at 10 | VISUALLY VERIFIED (Streets) | — |
| Modules added | RotationRate, ColorOverLife (spawn + update), Acceleration, SizeScale, VelocityOverLife (bAbsolute assumed false: multiplier values), SubUV (linear / random by RequiredModule InterpolationMethod), PSA_Velocity sprites (up = velocity, right = view x velocity) | UE3 module semantics | HIGH (VelocityOverLife flag PARTIAL) | — |
| Not yet | Trail2 / Beam2 emitters (tracers, repair / drain beams) are skipped and logged; PMI_Gravity / PMI_Unknown have no decoded payload; ColorByParameter DefaultColor not decoded (the caller passes the weapon colour); LocationPrimitiveSphere StartRadius / VelocityScale undecoded on some templates; VelocityInheritParent = 0 (static spawns) | logs | PARTIAL / UNKNOWN | next |

## MILESTONE 30 — ROBOT / VEHICLE FORM ACROSS ALL MAPS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Optimus robot + vehicle, 10 MP maps, spawn 7 | 20 / 20 PASS (0 noProgram / noDepth / GL errors), vehicle form active in all 10 vehicle runs; materials, hover FX and lighting present on every map; luma p50 8-44 (graded range) | work/m30/sheet0.png, sheet1.png | VISUALLY CHECKED (no defect found) | — |
| Debris haze | strong brown cast from its WorldInfo CLUT (MP_OrbitalDebris_CLUT) + fog; no volume grade | captures | PARTIAL (no original frame) | — |
| Streets camera inside Ceiling_Arch (Integration M08 near-black frame) | the third-person camera can sit under the arch's curved underside: Ceiling_Arch_STAT's authored simple collision (RB_BodySetup_10993: slab + legs) has no curve, and the original camera trace never tests per-triangle (execTraceCamera -> SingleLineCheck, World | 0x0A000000 BlockCameras, no complex bit; RE via Gameplay). The frame-filling dark view is the original's behaviour; its VISUALCHECK "near-black" is a legitimate camera-in-geometry frame | WFC_SHOTLIST + WFC_PICK: render 0.74 m vs collision 20.9 m; RE | CONFIRMED (authentic) | none |
| Harness note | the agents/rendering exe has no WFC_MAP (map selection lives in the integration code): map audits must use the integration-based build, or every capture is Streets | first m30 run | — | — |

## MILESTONE 28 — TEXTURE LIFETIME FOR A PERSISTENT RENDERER (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| uploadTexture leak across matches | unloadMapRenderData freed uploadMesh slots but never uploadTexture textures: World::load map textures, Weapon / Vehicle FX textures, mesh material and lightmap textures. Frontend's persistent-renderer chain: +60..105 live textures per match (Streets 114 -> 716 after 9 maps). My M22 "71 textures = the game's own" were these: renderer-only runs never reloaded the World | Frontend chainP census; WFC_RELOADTEST pre-M28: 355 live after 5 reloads | CONFIRMED | unloadMapRenderData releases every texture uploaded since the previous unload unless setTexturePersistent (canvas font pages are). Released handles are never reused; updateTexture refuses them. New IRenderer::setTexturePersistent / releaseTexture / liveTextureCount. WFC_RELOADTEST=60,60: 71 released per cycle, 0 live; after 4 cycles the frame matches a no-reload run (PASS, diff = shot timing). WFC_M28_KEEPTEX = old behaviour (A/B) |
| In-process World reload hang | assets::loadSkinnedGlb loaded into the existing SkinnedModel: nodes.resize kept the old nodes and children / roots were appended, so every reload duplicated each skeleton edge and poseGlobals' DFS walked k^depth paths (reloads got slower, then never finished at reload 4-5). lldb on a RelWithDebInfo build: main thread in assets::poseGlobals <- Character::finalizePose <- World::tick | lldb backtrace work/m26/hang_bt.txt | CONFIRMED ROOT CAUSE (shared asset loader; only a reload into a used model, i.e. WFC_RELOADTEST or any chassis / weapon reload that reuses a model) | M29: a load replaces the model (m = SkinnedModel()). WFC_RELOADTEST=60,60: 9 cycles in 41 s, 71 released / 0 live each, PASS |
| Title reference | re-baselined after UI_FrontEnd's volume grade (M25 verdict above) | suite | — | work/ref_m21/title_scene |

## MILESTONE 26 — DEBRIS SIGN COMPILE, ION BLASTER TRACER SLABS (2026-10-05, overnight)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Debris Megatron_com_Mat GLSL compile error | Panner_1591's Time is a float3 (Clamp(Desaturation(...)) × 1); matc emitted vec3 * vec2. The cooked material carries no compile errors and hundreds of shipped materials rely on the same lenient vector coercion | expression trace; cooked tail has no error strings | HIGH | matc Panner / Rotator truncate a vector Time like HLSL (.xy / .x), counted as type_violations like binop. Only GLSL that previously failed changes. Debris: 0 compile failures |
| Ion Blaster grey slabs (Integration E2E) | WeaponFx (Systems) draws the tracer smoke ribbon at flat 0.35 opacity; the original Tracer_Smoke_MAT multiplies by an across-width mask (1 − 4(v−.5)²)² (0 at both long edges) and an end fade clamp(20u)·clamp(3(1−u)) | matc compile of TransGame FX_Materials_p.Materials.Tracer_Smoke_MAT; repro WFC_AUTOFIRE Streets s0 | CONFIRMED (material graph); GAMEPLAY/SYSTEMS code, not renderer | handed to Systems with the formula; renderer unchanged |

## MILESTONE 25 — POST-PROCESS VOLUME GRADES, CLUT PATH, HARNESS (2026-10-05, overnight)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Map grade | Seed, Berth, Rust, Complex, Gorge, Molten, BrokenHope and Remnant author their grade on one enabled PostProcessVolume that contains every player start, not on TnWorldInfo. Bloom_Scale is 0.10 there (as on Streets' WorldInfo); the renderer used the Default__WorldInfo 1.0, so these maps were over-bloomed and ungraded | AssetTools postprocess.json volumes[] + completeness (Gorge 115/115 starts inside, Berth 120/120, Remnant 4/4) | CONFIRMED (data); "inside" from player-start coverage, the brush is not exported: HIGH for gameplay views | build_lighting: the highest-Priority enabled volume's Settings (over the FPostProcessSettings defaults, FVector {X,Y,Z} -> list) and its CLUT strip become lighting.json postprocess. Streets / Debris (no volume) byte-identical. Non-Streets p50 drops into Streets' range (10-45). Tone formula unchanged (UberPostProcess microcode, CONFIRMED pass 7) |
| CLUT never loaded off the worktree root | lighting.json records the strip path relative to the build's cwd; the renderer opened it verbatim, so a player-route exe (cwd elsewhere) ran every map without its CLUT | log "image: decode failed work/render/.../clut.png" from work/m20/maps | CONFIRMED (renderer bug) | resolved against the map's data dir. Release path now applies MP_Streets_CLUT |
| Title scene desaturated after M25 (Integration M07) | UI_FrontEnd has no CLUT; its PostProcessVolume_15709 (UI_FrontEnd_m, bEnabled) authors Scene_Desaturation 0.5, Bloom_Scale 0.2, Shadows 0.01. Its brush Model_14105 is a box of ±33,294 UU about the origin, and every title camera (-6702, -15212, 237) is inside it, so UE3 gives the title view this grade | brush points read from the cooked Model; postprocess.json volumes[] | HIGH (UE3 volume semantics + geometry); no original title frame yet: HUMAN CHECK | unchanged, data-correct |
| Black-frame rule | Graded dark views (Molten lava cave, Gorge shaft) are 60-65 % below luma 10 while correctly lit; the 60 % rule FAILed them. No real regression was ever caught by it (depth / GL-error / legacy / near-black rules caught them) | 30-view audit + contact sheet | harness | threshold 80 % (a lost world is > 90 %); 30 / 30 audit views now PASS. Molten / Gorge darkness is PARTIAL (no original capture to compare) |
| M11 coverage | Frontend restores its GL state since a96f841, so WFC_M11_INHERITSTATE alone no longer reproduced the leak and the release-path check PASSed it | release_path_check with the switch: PASS | harness | the switch now also injects GL_DEPTH_TEST off; release_path_check: fix PASS (6/6), reproduction FAIL (6/6, noDepth ~1450-1810) |
| Preview body lifetime | bodies are CPU-only and survive scene unloads; nothing freed them | code | — | IRenderer::releasePreviewBody (slot reuse) + previewBodyCount; Frontend LRU-caps at 8, soak plateau 1246 -> 1253 MB |

## MILESTONE 24 — VERTEX LIGHTMAPS ON EVERY MAP (2026-10-05, overnight)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Black arches / pillars on Gorge, Rust, Seed | (1) build_lighting rejected FLightMap1D sample blocks whose alpha byte is not 255 ("alignment" check): Gorge / Rust samples carry alpha 0, so 0 of 561 / 635 vertex-lit components parsed. (2) The renderer bound a vertex lightmap only when ONE glTF section covered the whole cooked LOD0 buffer, so multi-section components never bound | header dumps (count × 12 = size, valid ScaleVectors, alpha 0); AssetTools vertex_lightmaps.json agrees byte for byte | CONFIRMED ROOT CAUSE (tooling + renderer) | parser validates size and ScaleVectors instead of alpha: every vertex-lit component of every map parses (Gorge 561, Rust 635, Seed 257, …). Sections index the shared samples at their cumulative offset: Gorge unbound 124 → 4, Rust 381 → 0. Streets lighting.json byte-identical, suite identical. Gorge view 0 black 37 % → 21 % (arch lit). VISUALLY VERIFIED |
| Remaining | one Gorge component whose glTF sections total 1616 vertices against 1576 cooked samples (export differs from the cooked buffer) | renderer log | PARTIAL (not bound rather than guessed) | — |
| Map colour grade | 8 of 9 non-Streets maps grade on a PostProcessVolume containing the player starts (e.g. Gorge clut_mp40 + Scene_HighLights 1.5), not on WorldInfo | AssetTools 992fbf1 postprocess.json volumes[] | CONFIRMED (data) | applied in M25 |

## MILESTONE 23 — FRONTEND SCENES OVER TIME, CUSTOMIZATION, RETURN FROM MATCH (2026-10-05, overnight)
Merge preview: agents/frontend 1af7e74 (Cust_Idle body, pawn hiding, GFx fixes) + agents/rendering 03a08c8, rebuilt render data.

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Title over time | t = 5 / 30 / 60 / 120 / 240 s: the orbit camera sweeps (different framing each), debris fields / ships move, bursts fire (240 s), no GL errors, no unknown actors. RE: the whole title is ONE synchronised 393.55 s loop (Orbiter + 1st / 2nd / 3rd QTR + Plane Fly By start together at FMV_intro Stopped), FOV fixed 45 | captures work/m23/run/t*.bmp, VISUALCHECK | VISUALLY VERIFIED |
| Customization class cameras | Scout / Scientist × Autobot / Decepticon: the class camera frames that faction's pawn, ground-snapped, other pawn hidden; idles resolve through the chooser groups (Sideswipe / Barricade / AirRaid → NAV_Idle, Starscream → Cust_Idle) | captures, preview-body log | VISUALLY VERIFIED |
| Return from match | game lobby 3D layer before vs after a Streets match: same camera, draws (8), materials (6), inherited GL state (depth on); pixel differences come from the authored time-animated BackdropSpaceDome_MAT_INST | WFC_SCENE_ONLY captures | CONFIRMED (no state carried over) |
| All MP chassis | 27 chassis × robot + vehicle all render (54 captures PASS). Warpath's vehicle (Tank3) looked garbled in the close sweep: its bind pose equals Transform_ToRobot_VEH at t 0, i.e. the vehicle form; the close camera cut through the long tank | fixed-time clip samples (WFC_SCENEPREVIEW anim@seconds) | VISUALLY VERIFIED |
| RandomSeed | WFC material uniform "RandomSeed", one float per mesh element (FMeshElement +0xBC) set in the material PS SetMesh 0x82E982E8; the proxy field that fills it is not identified | RE | CONFIRMED (what) / UNKNOWN (source); the wrecked-soldier prop keeps its fallback |

## MILESTONE 20 / 21 — MULTI-MAP FIDELITY: LIGHTMAPS, LIGHT COLOURS, MATERIALS, CHASSIS (2026-10-05, overnight)
Audit: all 10 cooked MP maps built through the generic pipeline (no map names in renderer source), 30 direct-boot captures
(3 spawn views per map) on integration 9e133bb + agents/rendering, WFC_VISUALCHECK.

| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| "Mostly black" maps (Gorge, Rust, Seed, Berth, Broken Hope, Molten, Complex) | build_lighting found each component's FLightMap2D coefficients by searching for the FIRST component's first light GUID; every component whose light list started with another light was dropped, so its static mesh got no baked light. Gorge 256 of 1137 records, Rust 127 of 1412 | lighting-only / albedo debug views (geometry present, walls unlit); record counts | CONFIRMED ROOT CAUSE (tooling) | FLightMap2D parsed structurally (type at 13, LightGuids.Num at 17, GUIDs, Owner, 3 coefficients, scale / bias). Streets records identical (1795 / 1795), lighting.json byte-identical. Lightmapped draws ×4–×10 on the affected maps; Gorge black 62–72 % → 15–37 %, Rust 35–50 % → 13–21 %. VISUALLY VERIFIED |
| Light / sky / fog colours | cooked FColor serializes DWColor big-endian; the reader lists [B, G, R, A]. build_lighting consumed it as RGB: every tinted light (Streets 260 of 268), every SkyLight lower colour and the height-fog colour had red / blue swapped (Streets fog (234, 91, 116) → actually (116, 91, 234)) | AssetTools authored.db field-named decode ({R:78, G:148, B:186} for the customization SkyLight; Streets fog {R:116, B:234}); RE customization facts; the original Streets level thumbnail's cool grey-violet tone | CONFIRMED | `fcolor_rgb` in build_lighting. Lightmaps unchanged; dynamic lighting / fog corrected. Streets suite re-baselined (work/ref_m21): 0.1–12.6 % per camera, cooler / neutral as the thumbnail |
| World materials | per map, every material the world glTF references is compiled, except Molten's floor (TextureSetSample) / rain puddle (SceneTexture) and the wrecked-soldier prop on Debris / Broken Hope (RandomSeed) | world.glb wfc_material audit | CONFIRMED | TextureSetSample added (TextureSet blueprint ENV_MainTexSet_BluePrint: Color_NormX = Color.rgb + Normal.x, Masks_NormY = Masks.rgb + Normal.y; alpha channels verified as a unit normal xy in the exported intermediates). Molten floors compile. SceneTexture added (M22: resolved opaque scene colour, copied once per frame at first use before translucency, unit 16; Molten rain puddles compile and draw; output mapping as a texture sample is HIGH). RandomSeed (WFC per-primitive value, no data; asked RE) remains [PARTIAL]. The other material failures in the build logs are Streets-only extras the map never draws |
| All MP chassis previews | 27 shipped MP chassis × robot + vehicle: 144 body materials, every MP one resolves to a compiled original. The 17 unresolved belong to six CAMPAIGN-ONLY bodies (generic car soldiers, Frenzy, Rumble, Laserbeak) | offline resolution + 54 renders in the customization room | CONFIRMED | contact sheets work/m20/chassis: correct orientation, floor contact, materials, faction colours |
| Preview idle via chooser groups | SetAnim remaps through AnimSet ChooserGroups, last set first; every playable chassis has a Cust_Idle group (→ Cust_Idle or NAV_Idle; Brawl weighted pair) | RE (xex SetAnim Function_82E3FF48, ChooserGroups from authored.db) | CONFIRMED | `tools/render/build_anim_choosers.py` → _ui/anim_choosers.json; loadPreviewBody resolves through it (weighted random pick: HIGH) |
| Map load / unload hygiene | 6 maps × 3 rounds in one process (renderer-only WFC_MEMCYCLE): live GL objects after every unload identical (71 textures = the game's own, 0 buffers / programs / framebuffers / VAOs); private memory peaks after Remnant (largest) and round 3 ≤ round 2 for every map (Streets 3723 → 3611 MB, Remnant 4238 → 4117 MB) | GL census, process counters | CONFIRMED (no GL leak) / HIGH (no CPU leak: allocator / driver high-water) | — |
| GL object census | `IRenderer::glObjectCensus()` (glIs* over names 1..131072) in WFC_MEMCYCLE | — | — | for the load / unload hygiene runs |

## MILESTONE 19 — PREVIEW PAWN POSE (2026-10-05)
| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Preview animation | TnCharacterScriptBinding.SetupPreviewAnim → IdleNode.SetAnim(PreviewAnim); class defaults IdleNodeName=IdleNode, PreviewAnim=Cust_Idle | decompiled script, cooked Default__TnCharacterScriptBinding | CONFIRMED | `IRenderer::loadPreviewBody(robotGltf, animSets, "Cust_Idle")` + `posePreviewBody(h, t, mesh)`: roster sets loaded, sequence found last set first (UE3 FindAnimSequence), looping |
| Which chassis have Cust_Idle | 13 sets (SequenceName): Jetfire, Optimus, Scattershot, Warpath, Zeta, Ironhide, Onslaught, Skywarp, Soundwave, Starscream, Thundercracker, Breakdown, Deadend. Arcee has only Cust_idle_active; the others (e.g. Sideswipe, Barricade) have none | cooked UI_PartyLobby_m (all MP sets cooked there) | CONFIRMED | VISUALLY VERIFIED: Warpath (12.3 s) and Starscream (9.9 s) animate in the customization room |
| Chassis without Cust_Idle | UE3 SetAnim on a missing sequence clears AnimSeq (warning) and the node outputs the reference pose | stock UE3 AnimNodeSequence; the rest of Robot_ANIMTREE above IdleNode not traced (asked RE) | HIGH (node) / UNKNOWN (tree result) | reference pose (logged) |

## MILESTONE 17 — CUSTOMIZATION CLASS CAMERA, IMAGE CHECK (2026-10-05)
Merge preview: agents/frontend 89df3bc + agents/rendering, -Map Standard render data. Route: party lobby → Create a
Character → first character → Autobot Chassis (clickclip chassisButtonA).

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Class camera chain | fscommand hideDecepticon → Chassis_To_Cam_ID_1 (slot 0, camera id 0) → Scout → "SCOUT - Autobot" (SeqAct_Interp_13491). CameraActor_2082 moves (784, 5252, 235) → (659, 5425, 195); FOV 69.75 → 60 | FLOW log, captures | VISUALLY VERIFIED |
| Preview pawns | Car2 (Sideswipe) / Car4 (Barricade) at the authored PathNodes with original materials, per-owner light environments | captures | VISUALLY VERIFIED (bind pose) |
| Pawn height | the pawns stand at PathNode z 179 with their mesh origin (feet) there. The room's floor is the invisible BSP slab (Invisible_MAT, x −1024..3072, y 3200..7296, top z 0), so the original's OnPreviewPawnTick FindGround puts the feet at z 0: currently 179 UU too high. The class camera's final shot crops the head at 179 and frames the whole body at 0 | bsp.glb, render at the camera's final pose | CONFIRMED (geometry). Renderer support: `IRenderer::sceneGroundHeight(x, y, zFrom)` traces down the level BSP (invisible brushes included): 0.0 under both PathNodes; a body placed there frames as the z 0 render (2 px difference). Frontend sets the slot height from it (FindGround) [PARTIAL until wired] |

## MILESTONE 12 — FRONTEND VIGNETTE SHIPS (2026-10-05)
| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Ship skeletal animation | none: the 17 HmSkeletalMeshActors (7 SoldierJetAut, 10 SoldierJetDec) have no AnimSets; their AnimNodeSequence serializes no properties (no sequence, not playing); the VIG / UI_FrontEnd Kismet has no animation action; no InterpTrackAnimControl in either level | cooked UI_FrontEnd_capture_VIG_m / UI_FrontEnd_m | CONFIRMED | reference (bind) pose is the original look. Bind pose = reference pose, verified per joint: for both ship meshes (SoldierJetAut 34 joints, SoldierJetDec 37), rest-pose world transform × inverse bind matrix = identity to 7.4e-6, so the drawn vertices are UE3's no-animation reference-pose result (M16) |
| Ship motion | InterpTrackMove (26 tracks) | cooked VIG | CONFIRMED | Frontend matinee → `setFrontendActorTransform` (done) |
| Ship / booster scale animation | 7 InterpTrackFloatProp DrawScale tracks: dec01, dec0203 (0.2), djDS01 (0.08 → …), megatronDS (0.02), thrust01, DSbooster, djDSboosters (0.1 → 0.6 / 1.0) | cooked VIG keys | CONFIRMED | `setFrontendActorScale` (relative to the authored DrawScale = gltf_matrix scale); emitters follow pose + scale. VISUALLY VERIFIED with the diagnostic (djDS01 0.47 → 0.08). Frontend's evaluator must export / evaluate the FloatProp keys [PARTIAL until wired] |
| Particle size under DrawScale | sprite / mesh-particle size × the emitter scale: UE3 FillReplayData `Source.Scale` = Component Scale × Scale3D × owner DrawScale × DrawScale3D, rendered as `Particle.Size * Source.Scale` | WFC xex (RE, Ghidra read-only): sprite FillReplayData 0x8300C678 and mesh FillReplayData 0x83052BD8 compute Component Scale(+0x178) × Scale3D(+0x17C) × owner DrawScale(+0x140) × DrawScale3D(+0x144) unless AbsoluteScale (+0xF4 & 0x100000); mesh gates: local-space (scale via LocalToWorld, as implemented) and an ignore-component-scale instance flag (unset in every TypeDataMesh of Streets / Berth / UI_FrontEnd). Matinee DrawScale is live: execSetDrawScale 0x82C48050 → AActor::SetDrawScale writes +0x140. The render-thread Size × Scale multiply: HIGH (stock UE3) | CONFIRMED | sprites: width × scale.X, height × scale.Y; world-space mesh particles: size × per-axis scale (local-space ones already through the instance rows); scale = instance row lengths, so it includes authored DrawScale and matinee scale. Streets unchanged (all 45 instances at scale 1); 41 scaled title / vignette emitters (0.04–1.5) now draw at their authored size. `WFC_FX_NOSIZESCALE` A/B |
| Camera FOVAngle tracks | Title / vignette: all 10 FOVAngle tracks have **no keys** (81-byte objects: PropertyName + TrackTitle), so the FOV is the bound camera's own FOVAngle (CameraActor_6585: 45, already sent). Customization (UI_CharacterCustomization_m, CameraActor_2082, authored 70): the 8 class camera matinees (Scientist / Soldier / Scout / Leader × Autobot / Decepticon) key 70 → 60 (Scout, Leader) or 65 (Scientist, Soldier) over 0.5 s, CIM_CurveAuto with zero tangents | cooked levels | CONFIRMED | `tools/render/build_scene_floatprops.py` → `matinee_floatprops.json` in every map's render data (keyed by SeqAct_Interp / InterpData / group / property); `IRenderer::frontendFloatTracks()` + `IRenderer::evalInterpCurveFloat` (UE3 FInterpCurve::Eval). Verified: Scout - Autobot 70 → 68.44 → 65 → 61.56 → 60 (`WFC_FOVTEST`). Frontend evaluates per frame and passes the FOV to drawFrontendScene [PARTIAL until wired] |

## MILESTONE 11 — REAL RELEASE PATH: MAPS DRAWN WITHOUT DEPTH TESTING AFTER THE MENUS (2026-10-04)
Input: the human recording of `Rebuild\build\release\bin\wfc_rebuild.exe` (M06b): Streets and Berth incomplete. The
human's session log is `F:\Transformers Rebuild\wfc.log` (account OhTruman, 20:50). Full evidence:
`docs/handoffs/M11_RELEASE_PATH_MAP_REGRESSION.md`.

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Root cause | `GLRenderer`'s 3D frame never established depth test / func / scissor / stencil / colour mask / polygon mode (set once in `init()`). The frontend GFx pass left `GL_DEPTH_TEST` disabled, so after the menus every map surface was drawn without depth testing | real Release exe + human profile, same pinned camera: direct boot vs frontend route submit identical draws (1900; 1468 world / 394 BSP); entry state depth=1 vs depth=0; 88 % of pixels differ | CONFIRMED ROOT CAUSE |
| Second owner | Frontend's GFx pass did not restore the state it changed (Experimental bisect b1fce97); fixed in agents/frontend a96f841 (save / restore). Rendering's frame now establishes its own state regardless | Frontend report, Rendering reproduction | CONFIRMED |
| Loading / data | human log: Streets 1,983,988 tris, 2,249 submeshes, 2,239 programs, 2,081 lightmap bindings; Berth 1,292,885 tris, 1,755 programs; render-data root and map dir correct; texture paths absolute | human wfc.log | CONFIRMED: not involved |
| Fix on the real path | integration 681fd29 + Rendering, Release layout, integration render data, human profile: frontend → Streets equals direct boot (apart from HUD / character); Streets → Berth → Streets: identical Streets counts (1641 / 357), Berth 1273 / 288, 0 opaque draws without depth test, 0 GL errors | captures `work/m11/cycle`, `rpc_*` | VISUALLY VERIFIED |
| Resolution / fullscreen | no correlation: cold boots at 1280×720 windowed, 1920×1080 windowed and fullscreen (desktop 2560×1440); runtime switch 1920×1080 → fullscreen → Streets; restart with persisted settings. All draw the full structure, 0 GL errors | `work/m11/r*`, `rtres*` | CONFIRMED (disproved as cause) |
| Long session | one process, 10 alternating Streets / Berth matches: all captures pass, 0 GL errors; loaded Streets 2,867 → 2,940 → 2,945 → 2,950 → 2,950 MB (one +73 MB step, then flat); Berth 2,725 → 2,687 MB | `work/m11/long` | HIGH CONFIDENCE (no growth / leak in Release) |
| Memory after Seed (Frontend: +460 MB, Debug) | reproduced as a one-time high-water mark, not a leak. Renderer-only cycle (WFC_MEMCYCLE): Streets x4 unloads 2,982 -> 3,119 -> 3,126 -> 3,133 MB; alternating Streets / Seed goes up and down and returns to the Streets plateau (3,127 MB). Allocator / driver pooling of freed memory. Fix: large world meshes no longer keep a CPU copy in the shader path (unused there; kept for WFC_PICK and small effect meshes). Load peaks are about 100 MB lower (Streets 3,291 -> 3,194 MB, Seed 3,604 -> 3,480 MB) | `work/m11/seedmem`, MEMCYCLE | HIGH CONFIDENCE (no leak) |
| AMD driver resets | no GL errors and no instability in any single-process run after the fix | runs above | UNKNOWN (not reproduced) |
| Validation | new: opaque draws without depth testing and GL errors FAIL the verdict. `tools/render/release_path_check.sh`: the player route with no data override, structural floors on submitted world + BSP submeshes (Streets 2801, Berth 2059 in every run). It FAILs the reproduced bug (`WFC_M11_INHERITSTATE`) on every capture, title included, and passes the fix. Black / flat heuristics now judge only levels (the lobby dome produced 98 false FAILs) | rpc_good / rpc_broken | CONFIRMED |
| Fullscreen size, HUD scale, movie bars | fullscreen ignores the saved resolution (desktop size); GFx stage scaling | captures | handoff (Frontend / platform) |

## MILESTONE 10 — M06 PLAYTEST VISUAL REGRESSION: ROOT CAUSE, GUARDS (2026-10-04)
Input: human recording of the integrated M06 executable, showing a black Streets world and malformed menu
backgrounds. Full evidence: `docs/handoffs/M06_PLAYTEST_VISUAL_REGRESSION.md`.

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Root cause | The Release executable (`build/release/bin`) resolved render data to `build/work/render`, which does not exist. Every map and frontend scene silently fell back to the legacy fixed-function renderer | integration 95edd7b rebuilt locally: human path with data correct; the same path without data reproduces every symptom in the recording | CONFIRMED |
| Render data | integration's Streets data equals Rendering's (geometry, lightmaps, LVV, decals, movers, FX byte-identical; materials and lighting differ only in absolute vs relative file paths) | byte compare | CONFIRMED |
| Fix | `renderDataRoot()` searches `work/render` up to 4 levels above the executable; root logged | Release-layout copy finds the data without env; Streets 0 px vs M08 | VISUALLY VERIFIED |
| Silent fallback | ERROR log, `renderDiagnostics().originalPath`, red screen frame when render data was asked for and missing | no-data run: FAIL with reason, red frame visible | VISUALLY VERIFIED |
| Scene transitions | title → lobbies → Streets → match end → lobby → Streets: both matches have identical draw / resource counts (2033 draws, 1641 world, 357 BSP, 161 materials, 0 drawn without a program), the same resources as a direct boot | merge preview (95edd7b + this branch), WFC_VISUALCHECK | CONFIRMED: no leak |
| Inherited GL state | the frontend / GFx host leaves depth test off, blend ONE / ONE-or-ONE_MINUS_SRC_ALPHA, a texture bound. The pipeline sets its own state; image and counts are unchanged | `gl_entry_state` per capture | CONFIRMED harmless (no blind reset added) |
| Title scene inputs | Frontend camera = authored CameraActor_6585 (−6701.84, −15212.47, 237.44; −0.61° / 72.42° / 0.34°; FOV 45). 88k matinee poses applied; 2 unknown names: a camera (not drawn) and Emitter_13640 (a laser emitter carried by a matinee track: map FX do not follow poses yet) | pose counters | CONFIRMED / emitter-follow PARTIAL |
| Character preview | Was not instantiated: no path existed on either side. Now `setFrontendSceneDraw` draws Gameplay-owned bodies in the lobby scene. A roster chassis (Bumblebee) renders with its original materials, the lobby's DirectionalLight_3388 and `setCharacterColors` overrides. Body data is present for all 33 chassis (66 glTF). Posing, animation and the PreviewGuy placement are Gameplay / Frontend's | preview capture, frame report | VISUALLY VERIFIED (draw path) / PARTIAL (bind pose, placement) |
| Lobby background | authored room: SpaceDome, props, 12-tri BSP. Its BSP was never generated (`build_lighting` numpy bug, Integration fix 7536ab1 now applied); generated now. The lobby scene drew `UI_CharacterCustomization` render data (54/162 materials, no chassis) instead of the persistent `UI_PartyLobby` (156/162): the persistent level is now preferred | material counts, scene capture | CONFIRMED / class cameras PARTIAL |
| UI render data | the five UI families were not part of any standard build (a tree without them shows black menus). `-Map Standard` now builds them; the shared character shader package `TR_AllShader_p.xxx` (standalone seek-free) is a material fallback, as the original loads it with the preview character. Streets output is unchanged (0 GLSL changes, suite refdiff 0) | build logs, suite | CONFIRMED |
| Visual regression guard | `WFC_VISUALCHECK`, `tools/render/visual_check.py`, `tools/render/visual_suite.sh` (5 fixed Streets cameras, title scene, human flow) | good build passes all captures; no-data build fails all | CONFIRMED |

## MILESTONE 09 — FRONTEND SCENES, LOADING, ROSTER READINESS, RE OVERNIGHT HUD (2026-10-04)
Inputs:
- RE `OVERNIGHT_2026-10-04_HUD_VEHICLE_FRONTEND_ROSTER_AI.md` (A HUD, D frontend backgrounds, E roster);
- RE `MILESTONE05_PLAYTEST_RE.md` §6 (loading);
- AssetTools `ui_scenes.json`, the UI level exports, and `mp_content/mp_characters.json`;
- Frontend lane requests (scene entry point, render data for the UI families).

| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Minimap / radar / compass | none in shipped MP | RE A9 (UnrealScript, every GFx pool, authored.db, xex strings) | CONFIRMED (absence) | none drawn; any future minimap is a labelled modern extension |
| Title / main-menu background | live level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m, Matinee camera (CameraActor_6585, FOV 45) | RE D, AssetTools ui_scenes | CONFIRMED | `loadFrontendScene` / `drawFrontendScene`: Cybertron orbit scene with energon rings, debris, station, nebula. VISUALLY VERIFIED from the authored camera. Matinee actors, Kismet-activated emitters and lens flares are PARTIAL (need the Frontend's sequence state) |
| Lobby / customization background | UI_PartyLobby_m / UI_Lobby_m stream UI_CharacterCustomization_m | RE D | CONFIRMED | the customization room is loaded and drawn (space dome from the default camera). The class cameras (15 Matinees), the preview pawn and the CybertronCard Kismet are Frontend-driven [PARTIAL] |
| Campaign lobby | UI_CampaignLobby_m SpaceDome | RE D | CONFIRMED / look UNKNOWN (AssetTools) | drawn |
| Multi-level composition | persistent + streamed levels together | RE D | — | one render-data map at a time (the family with the most scenery) [PARTIAL] |
| Loading movie during blocking loads | Bink on the rendering thread; closes on `CanCloseLoadingMovie` | RE PLAYTEST §6 | CONFIRMED (names) / HIGH (semantics) | `setLoadYield`: a cooperative present between bounded steps (longest 70 ms on UI_FrontEnd). Not a separate render thread [PARTIAL] |
| TextureSample output 0 | RGB float3 (mask R, G, B) | UE3 node outputs; `Append(TexSample, TexSample.A)` in EnergonRing compiles only so | CONFIRMED | matc fixed (EnergonRing materials failed). Streets 231/231 (now 325/325 with the roster) permutations match; sweep median 0 px |
| Character roster readiness | 33 chassis; 4 default classes | RE E, AssetTools mp_characters | CONFIRMED data | character materials from the roster (98, all verified); per-owner light environment / applier colours (`setDrawOwner`); the Optimus path is unchanged (idle robot / vehicle 0 px vs M08) |
| Map-generic render data | — | — | — | packages from map.json sublevels, LM per sublevel, no-BSP / no-fog maps; Streets output unchanged |
| Per-draw uniform cost | — | — | — | uniform locations cached per program: scene submit 4.9 → 3.9 ms (shared, loaded machine) |
| HUD (Hud_GFX) | layout, kill feed, announcements, popups, scoreboard, end message | RE A0–A8 | CONFIRMED | Frontend runs the movie (`docs/handoffs/FRONTEND_INMATCH_HUD.md` updated with the exact values); the Canvas markers are Rendering's |
| Brightness (profile GammaSetting) | DisplayGamma = 2.2 + Lerp(-0.95, 0.95, GammaSetting/100); default 50 → 2.2 | decompiled HmProfileSettings.GetGammaSetting, HmPlayerController → DisplayDataStore "Gamma" | CONFIRMED (mapping) | `setDisplayGamma` drives the scene resolve and Canvas tiles; default unchanged (0 px). Whether GFx / Bink / Canvas simple elements also follow DisplayGamma is UNKNOWN (the decoded Canvas simple-element shader has an InverseGamma×2.2 exponent, not yet applied) [PARTIAL] |
| Character jitter (M05) | — | M08 measurement | — | handed off to Gameplay (unchanged on agents/rendering; the patch is in `docs/handoffs`) |

## MILESTONE 08 — PLAYTEST REGRESSIONS, IN-MATCH HUD OWNERSHIP, CANVAS LAYER (2026-10-04)
Inputs:
- the human playtest of integration milestone 05;
- RE `MILESTONE05_FRONTEND_GAMEPLAY_BLOCKERS.md` §G / §H and `MILESTONE05_GAMEPLAY_UNKNOWNS.md` §5;
- AssetTools `frontend_hud.json` / `future_hud_handoff.json` / `frontend_loading.json`;
- a `git archive` export of integration/milestone-05, built in `work/m08/int05` for measurements. No merge.

| Item | Finding | Mark | Owner / action |
|---|---|---|---|
| Character "interlacing" (robot + vehicle) | Gameplay's obstruction camera (pass 20) stores a world camera position at the 60 Hz tick while the view rotation changes every rendered frame. Above 60 Hz the pawn swims on screen: 0.0095 screen units / frame at 144 Hz, 0 at 60 Hz (which is why lockstep tests passed). Not the renderer: skinning, frame loop, weapon attach and vsync are unchanged | VISUALLY VERIFIED (measured) | **Gameplay**: `docs/handoffs/GAMEPLAY_CAMERA_FRAME_PACING.md` + verified patch (0.00001 at 60 / 144 / 240 Hz, both camera models) |
| Vehicle rear propulsion "open / close" | the boost FX follows the Driving state, as authored (BoostFx). On M05, Driving drops to Hovering 15–31 times per 14 s with boost held; every drop is a PROVISIONAL hull probe's frontal block at floor level, then a 0.5 s drift and a re-boost (≈ 0.6 s cycle) | VISUALLY VERIFIED (logged) | **Gameplay**: `docs/handoffs/GAMEPLAY_BOOST_FX_FLICKER.md`. No renderer smoothing added |
| In-match HUD (clock, team / player score, health, ammo, crosshair, kill / score messages) | Scaleform Hud_GFX (`GameMessage`, `PointEvent`, `RewardAnnouncement`, `GameAnnouncement`; data stores + pushes). Spectate / respawn = MultiplayerRespawn_GFX, end = EndGameStats_GFX: already opened by the Frontend runtime in M05. Hud_GFX itself is not instantiated | CONFIRMED (RE / AssetTools) | **Frontend** GfxHost: `docs/handoffs/FRONTEND_INMATCH_HUD.md`. The renderer reticle stands down (`ReticleState.visible = false`) when Hud_GFX draws its crosshair |
| Kill feed layout / rows / lifetime / fade | Hud_GFX timeline + AS2 behaviour | CONFIRMED owner / details by running the movie | Frontend |
| Radar / minimap | no radar or minimap object in the authored data | UNKNOWN (RE verifying) | nothing drawn |
| Player / objective markers | Canvas `TnObjectiveMarkerTypeSprite.Draw` | CONFIRMED (RE) | **Rendering**: `render::HudMarkers` from the authored setups (`_ui/hud_markers.json`, 40 types; Versus ally 0.08 / 0.04, enemy 0.03125, focus 0.02734 × width, 1.0 s hysteresis, 3000 UU auto-focus, label colours). VISUALLY VERIFIED (`WFC_MARKERTEST`). Gameplay supplies the rule-visible markers |
| Canvas text | UE3 UFont: FFontCharacter table + CharRemap (cooked) | CONFIRMED data | `drawCanvasText`: MarkerFont 371 glyphs (USize advance, VerticalOffset, alpha coverage). Label centring PROVISIONAL |
| Collision report tooling | — | — | `WFC_PICK=1`: authored component / StaticMesh / material / distance / normal of the surface under the crosshair (`IRenderer::pickWorld`, diagnostic only), plus the movement collision world's hit on the same ray ("collision ok" / "differs" / "NONE") |
| Canvas simple elements (text, texture tiles) | engine PS: `oC0.rgb = exp(log(tex × ColorScale + ColorBias) × InverseGamma × 2.2)` (saturated), `oC0.a = tex.a × ColorScale.a + ColorBias.a` | CONFIRMED shader (sc_engine) / runtime `InverseGamma` value UNKNOWN | text drawn display-referred (= InverseGamma 1/2.2); HUD gamma stays PARTIAL until the Canvas gamma value is known |
| Menu background | the UI_FrontEnd_m 3D scene (orbit cameras, energon rings) | CONFIRMED source / not exported | the renderer loads it through `loadMapRenderData` once AssetTools exports it; no substitute |
| Regression | glass (9 views) and steam over time (12) identical to M07; validation captures (idle, walk, jump, fire, vehicle idle / move / boost, both transforms) | VISUALLY VERIFIED | — |

## MILESTONE 07 — STREETS CLEANUP, FRONTEND / NEXT-MAP READINESS (2026-10-03)
Evidence:
- RE-Workspace M05 notes (read-only): `MILESTONE05_GAMEPLAY_UNKNOWNS.md` §5 (Canvas HUD markers) and §6 (pickup factory
  mesh and spin), and `MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md`;
- the agents/frontend a55a4e9 handoff (map unload);
- the latest render_index (pickup_factory_visuals, including the objective rest meshes).

The renderer contract for the other lanes is in `docs/RENDERER_CONTRACT.md`.

| Item | Original (WFC) | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Pickup factory mesh placement | the template mesh is attached to the factory actor with zero offset; actor rotation and DrawScale apply | RE M05 §6 (script) | CONFIRMED (script) / HIGH (native attach) | mesh at the factory transform from render_index, authored rotation kept (was yaw from 0) |
| Pickup spin | the factory actor is PHYS_Rotating at PickupRotationRate only while available; it freezes when taken and resumes from that yaw | RE M05 §6 | CONFIRMED | spin accumulated per factory while available, frozen while hidden (was continuous from map start); local-space FX follow it. VISUALLY VERIFIED available / taken / respawn |
| Health / overshield | no PickupFactoryMesh, no spin | RE M05 §6 | HIGH | only their FX mesh particles draw; render_index "mesh particle" entries are not drawn twice |
| Flag / bomb rest mesh | Code of Power / MP bomb skeletal mesh at rest pose, +150 Z, spinning, CTF / EXT only | render_index (supersedes UNKNOWN) | CONFIRMED data | drawn and gated by the factory's game rule; hidden in TDM. VISUALLY VERIFIED (TDM none / CTF shown). Weapon materials not in the compiled set (glTF fallback) [PARTIAL, CTF/EXT only] |
| HUD markers | Canvas material tiles (`TnObjectiveMarkerTypeSprite.Draw`), materials in UI_HudMarkers_p, per-draw material params | RE M05 §5 | CONFIRMED | `IRenderer::drawMaterialTile`. All 15 marker materials compiled with runtime parameters and verified (231/231 permutations). VISUALLY VERIFIED: base marker friendly / enemy / off-screen arrow, enemy, death, health bar |
| MarkerAlly_MAT | empty expression tree | cooked material | CONFIRMED | draws nothing (ally tags = label + health bar) |
| Canvas gamma | — | canvas tile shader not decoded | PARTIAL | display gamma 1/2.2 applied as in the scene post |
| 2D composition | GFx / Bink / loading presented over the scene | Frontend handoff | — | `drawScreenTriangles` (blend modes, scissor, clamp), `updateTexture`. VISUALLY VERIFIED (fade + panel) |
| Level travel | — | Frontend handoff (renderer leaked the previous map) | — | `unloadMapRenderData` / `Pipeline::release`; in-process cycle VISUALLY VERIFIED (`WFC_RELOADTEST`) |
| Map-agnostic data | — | audit | — | asset roots from WFC_ASSETS / WFC_CONTENT; props, pickups and destructibles from render_index; tools take the map name, AssetTools manifest prefix, inline lightmaps from the ART package. Movers and map FX outputs identical to before |
| KOTH ring colour | `CaptureColor[team]` / `NeutralColor` vector params (MaterialParamNames) | RE M05 §3 | CONFIRMED (script) | not wired: needs a Gameplay hook; KOTH only [PARTIAL] |

**Regression of the playtest categories** (same cameras as M06; VISUALLY VERIFIED unchanged):
- glass: 9 views, 0 px different;
- steam over time: 12 views, 0 px;
- fog sheets: 138 views.

In the fog-sheet views the only differences are:
- the uncoloured steam emitter: the M06 scan predates ColorByParameter;
- one flag mesh, a regression caught and fixed by the rule gate.

Normal play (robot), vehicle form, transformation, firing, pickups and self-tests (shadows 32/32, DLE 20/20) were re-run.

**Still open (original-engine unknowns, documented):**

| Item | Mark |
|---|---|
| Translucent alpha output / blend state | PARTIAL |
| Darkening (modulate) blend with Opacity | UNKNOWN; no map material depends on it |
| HmParticleModuleGravity acceleration | UNKNOWN |
| Steam LOD 1/2 | DirectSet, unused |
| Totem and KOTH team colours | PARTIAL; need Gameplay ownership hooks |
| Canvas gamma | PARTIAL |
| Visible BSP without lightmaps | PARTIAL; RE orientation question open, not visible from gameplay viewpoints |

## MILESTONE 06 — HUMAN PLAYTEST: TRANSLUCENCY, SMOKE, GLASS (2026-10-03)
The human playtest of integration milestone 04 drives this pass. Evidence comes from the shipped Xenon shader caches
(`work/re/sc_streets_art.bin`, `sc_base.bin`, `sc_engine.bin`, disassembled with `tools/render/xenos_dis.py`), the
cooked material graphs and particle streams, and A/B runtime captures. Each A/B pair is pre-M06 behaviour
(`WFC_M05TRANS=1` + the pre-M06 material file) against the new behaviour, from the same camera.

Marks: CONFIRMED ORIGINAL (shipped shader / cooked data) / HIGH (stock UE3) / VISUALLY VERIFIED (A/B capture
inspected) / PARTIAL / UNKNOWN.

**Root causes of the reported defects**
1. **Translucency drawn mid-frame.** World translucent subs were drawn right after the world mesh's opaque subs, before
   the BSP, decals and characters. BSP floors and ramps behind glass or fog sheets then painted over them, so geometry
   under transparent panels appeared in front of them, and dark translucent cards showed through foreground structure.
   The scene-depth copy for DepthBiasedAlpha was also taken before the BSP existed, so soft fades saw no wall behind them.
2. **Additive opacity ignored.** WFC's additive base pass multiplies colour by Opacity, but the rebuild output colour
   unattenuated. The 46 FogSheet_DepthBiased cards (Blue / RED / Purple) draw their opacity from a 25-50 m
   depth-biased fade. Without it they drew at full strength over walls: the distance-dependent curtains.
3. **Particle soft fade collapsed.** The translator read DynamicParameter output 1 as `.x` (Desat1 = 0) and fed it to
   BiasScale. Steam therefore cut hard against geometry: the "static" slab of steam under the fallen panels.
4. **Steam colour missing.** ParticleModuleColorByParameter ('SteamColor') was not implemented, so the steam rendered
   grey-white instead of the authored blue-purple.

| Item | Original (WFC) | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Translucency pass | all opaque first; translucent primitives back to front by view depth of the bounds origin | stock UE3 FTranslucentPrimSet | HIGH | translucent subs of persistent meshes (world, BSP, props, FX meshes) and sprite batches are queued and drawn after every opaque draw, back to front; scene depth for soft fades is then complete. Decals stay in the opaque phase. VISUALLY VERIFIED (fog / glass / nav A/B) |
| Additive output | oC0.rgb = Color * fog * **Opacity** * SceneColorBiasFactor | Xenon PS of FogSheet_Parent_MAT (ART cache, line 28) and of ENV_ForceField glass (ART cache, lines 57-58) | CONFIRMED ORIGINAL | `wfcTranslucentOut`: additive colour *= Opacity |
| Translucent / additive kill | discard when Opacity < 1/255 | `kill_gt` against 0.00392 in both shaders | CONFIRMED ORIGINAL | applied for translucent and additive |
| DepthBiasedAlpha, unconnected Bias | 0.5 | FogSheet PS: `UniformScalar_3 * 0.5` | CONFIRMED ORIGINAL | matc default 0.5 (was 0) |
| DepthBiasedAlpha BiasScaleInput | uniform input replaces BiasScale; a per-vertex input is not compiled | FogSheet: ScalarParameter 'BiasScale' is the scale uniform; Steam_Mat: `(1 - SmokeBall.b) * 200`, the DynamicParameter is unused | CONFIRMED for both materials / general rule PARTIAL | matc: varying subgraph (DynamicParameter, VertexColor, textures, ...) -> BiasScale constant |
| DynamicParameter outputs | four scalars, output k = Param k+1 | 50 cooked nodes use outputs 0-3 only; ParamNames match (1 = 'DepthBiasScale' everywhere) | HIGH | matc fixed (was a 5-output reading shifted by one). 9 materials recompiled (FogSheets, Steam, Glow_Mod, Tracer_Smoke, muzzle FX) |
| Particle sprite size | corner = (TexCoord - 0.5) * Size: Size is the full width | sprite vertex factory VS (engine cache), lines 18-19 | CONFIRMED ORIGINAL | unchanged (already full width) |
| ColorByParameter | Color = BaseColor = PSC InstanceParameter 'SteamColor' (else DefaultColor white) | FName 'SteamColor' in the compiled Steam LOD stream (offset 298); 7 of 8 Emitters author it; CDO DefaultColor white | CONFIRMED data / FLinearColor(FColor) gamma-2.2 conversion HIGH | implemented; steam is the authored blue-purple, soft-edged and evolving (VISUALLY VERIFIED over 1.6 s) |
| FogSheet distance / angle behaviour | emissive * saturate(PixelDepth * 0.0005 / FadeDistance) * pow(saturate(CameraVector . N), DensityFalloff) | graph + PS | CONFIRMED ORIGINAL | authored: sheets fade out near the camera and when viewed edge-on. Not a defect, and left as is |
| FogSheet / BckSillouhetteSmoke animation | no Time / Panner in FogSheet_Parent_MAT; BckSillouhetteSmoke pans | graphs | CONFIRMED ORIGINAL | FogSheets are intentionally static art; the smoke cards animate through material Time |
| Glass bridge / floor panels | ENV_ForceField_ALL_MAT additive: fresnel + scanline + fence mask; Opacity = clamp(pow(1 - DBA, 4) * 5 + 0.2) | graph + PS | CONFIRMED ORIGINAL | faint 0.2 film with an intersection glow, composited after all opaque geometry, so nothing below can draw over it. VISUALLY VERIFIED |
| Translucent (alpha) output | PS writes oC0.w = SceneColorBiasFactor.y, colour premultiplied-like (Steam_Mat) | Steam_Mat PS | PARTIAL (blend state not in the PS) | unchanged: SrcAlpha / InvSrcAlpha |
| Modulate with Opacity | — | not decoded | UNKNOWN | unchanged (only weapon Glow_Mod uses it; map stains have Opacity 1) |
| HmParticleModuleGravity on steam | enabled (flagA 1); GravityMultiplier CDO 1.0; acceleration native | stream | UNKNOWN | not applied |
| DepthPriorityGroup / sort priority | all 1,952 components SDPG_World; no TranslucencySortPriority | props_authored | CONFIRMED | single world translucency list |

**Before / after (VISUALLY VERIFIED):** `work/m06/fog_ab.png`, `fogscan_top.png` (46 sheets x 3 distances; the worst
cases fs02 / fs40 / fs41 / fs04 show the flat blue veil before, and fs29 shows black polygons through a wall before),
`glass_ab.png` / `glass_crop.png`, `steam_t.png` (grey hard-cut slab -> purple soft steam, frame-to-frame motion),
`nav_ab.png`, `beam_ab.png` (light-ray cones and glow spheres change little), `play.png` / `play_vehicle.png`
(normal play, robot and vehicle).

**Diagnostic false positives (not defects):**
- Nav-node cameras placed on pickup factories sit inside the pickup beam and cube meshes.
- `WFC_ALBEDO` shows unlit additive glass as black (DiffuseColor 0).
- The dark VentWall near FFA start 15650 is authored lighting (lightmap scale 0.05).

## MILESTONE 05 — MP_IAC_STREETS NORMAL-PLAY MAP COMPLETION (2026-10-03)
Provenance: AssetTools **883b94b..da67634** (render_index.json: mode_visual_state, tdm_render_state, pickup_factory_visuals,
pickup_fx_components; STREETS_NATIVE_FIDELITY_M05), ReverseEngineering **940aa79** (flagA = bEnabled) and **fc05672**
(MILESTONE04_STREETS_FIDELITY_REMAINING), Gameplay **d122ef4** (map clock / mode / per-actor / pickup state API).
Marks: CONFIRMED / HIGH / PARTIAL / PROVISIONAL / UNKNOWN; VISUALLY VERIFIED = deterministic runtime capture inspected
(not a comparison with the original game). Slice mode is TDM: with no Gameplay rules set, every rule-gated actor is hidden.

**Correction to MILESTONE 04:** content glTFs (FX meshes, totem, KOTH ring, destructible states, ammo crate) carry the same
Y-up local vertex layout as world.glb meshes. Their placement is P*A_ue*P (P = y<->z swap) without a winding flip. M04 drew them
on their side, so the M04 destructible and totem captures were not valid. They are re-verified below.

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Content-mesh placement | UE matrix in UE axes; glTF content Y-up | AssetTools glTF convention; world.glb cross-check | CONFIRMED | `ueRowsToGltf` = P*A*P, no winding flip. Totems, KOTH ring, destructible, crate and FX meshes are upright (VISUALLY VERIFIED) |
| Map clock | Gameplay MapState seconds since GameplayStarted | Gameplay d122ef4 `setMapClock` | CONFIRMED API | movers, totem idle animation and pickup spin are evaluated at `mapTime()` (renderer clock only as fallback) |
| Domes x3 / SkyBeam x3 | 15 deg/s yaw; 9.0022 s Matinee sway (keys 0 / 4.5 / 9 s) | streets_movers.json | CONFIRMED | moving geometry VISUALLY VERIFIED (dome_t1/t2, sky_t0/t45) |
| Mode visibility (TDM) | 19 actors hidden: 4 bases (collide), 3 totems, 5 KOTH zones, 2 flag + 1 bomb factories (Disabled), 4 capture/plant points (no mesh) | render_index tdm_render_state | CONFIRMED | rule-gated plus `setActorHidden`. Audit: 4 bases / 3 totems intentionally invisible; flag/bomb effects intentionally invisible; nothing reported missing |
| KOTH active-zone ring | template ActiveMeshComponent0 pTorus1_STAT, zone DrawScale3D (1,1,2), CaptureZone_Reverse_MAT_INST | render_index | CONFIRMED | drawn only for the zone Gameplay unhides (VISUALLY VERIFIED koth_dm / koth_active) |
| Domination totems | NEU_EnergonTotem_SKEL + Conquest_Ring_MATINST (additive unlit, EnergonColor (1,1,1), intensities 5 / 2); StandBy idle animation | render_index; MIC | CONFIRMED | DOM only; upright and animating (VISUALLY VERIFIED). The saturated white ring is the authored neutral colour. Capture recolour (EnergonColor = CaptureColor[team], RE runtime semantics) needs a Gameplay DOM-ownership hook: not wired [PARTIAL, DOM only] |
| Pickup FX (45 particle components) | 8 Steam_Sm_FX + 37 pickup PSCs; 27 drawn while available, 24 in TDM; 10 Pickup_FX copies on health/overshield never attached | render_index pickup_fx_components; RE 940aa79 | CONFIRMED data / module semantics HIGH | flagA = bEnabled (RE HIGH) makes every emitter determinate, so all are simulated. Audit: emitters 32 drawn / 13 intentionally invisible / 0 unknown, matching AssetTools' 24 + 8 / 10 + 3 |
| Pickup beam | SetPickupVisible -> ActivateSystem, SetPickupHidden -> Deactivate | decompiled script | CONFIRMED | `setMapEffectState("<factory>|custom/highlight")`; ammo light-volume beam measured (pixel diff with FX on vs off) and VISUALLY VERIFIED available -> taken -> respawn |
| Ammo crate mesh | TnAmmoCratePickup.MeshComponentA PROP_NEU_AmmoPickup_STAT, CullDistance 8000, yaw 10000 UU/s, CastShadow false | render_index pickup_factory_visuals | CONFIRMED mesh / attach offset UNKNOWN | drawn at the factory origin and hidden with the custom effect; spin phase continuous from map start [PROVISIONAL]; native attach offset [UNKNOWN, AssetTools] |
| Health / overshield | HealthPickup_FX / OvershieldPickup_FX mesh emitters | decoded systems | CONFIRMED data | energon cube / shield mesh emitters drawn with the mesh material (bOverrideMaterial without a material: fallback rule UNKNOWN, mesh material used) |
| Flag / bomb factories | at-rest visual | — | UNKNOWN (AssetTools) | not drawn (Disabled in TDM anyway) |
| Placeholder content | — | — | — | graybox pickups (WFC_GRAYBOXPICKUPS) and the weapon-test dummy box (WFC_DAMAGETARGET) are no longer spawned in the slice |
| DefaultMaterial BSP | 62 surfaces / 87 nodes explicitly reference EngineMaterials.DefaultMaterial | AssetTools M05 §1; RE fc05672 §1 | CONFIRMED data | drawn as authored. The partly visible ceiling (BRUSH_11694) contributes 255-812 px at luminance 4-7 in its views; the BRUSH_9457 wall is mesh-enclosed from the tested cameras. The DefaultMaterial texture is null in the cooked material [PARTIAL]; no substitute |
| Arch black vertex-lit patch | 30/522 samples zero in all three coefficients; sibling 15855_SMC zero at 28 of the same | RE fc05672 §2 | CONFIRMED authentic | left as authored |
| MonitorScreen family | two panned ScreenText layers, Time-only, Screen_Color, Edge_Burn; no render target / movie / Kismet | RE fc05672 §3 | CONFIRMED | translation unchanged. Verifier fixed: the compiled texture-expression block is at offset 2 mod 4 and is now scanned. Materials 216 / 216 match (was 213). Active and Purple screens scroll (VISUALLY VERIFIED) |
| BSP without lightmaps | 616 nodes LightMapType 0; RE: 26 nodes (~3,694 m2) visible, west perimeter x = -3840 facing -X plus 2 +Y faces at Y = -26624 | RE fc05672 §6 | PARTIAL | runtime: the gameplay viewpoints nearest to them (west team starts x = 19.9 m, north pickups z = -284 m) see placed meshes there, not BSP. Viewed from their front side, the faces render near-black. Question for RE below |
| Culling | no cull distance / LOD / volumes / streaming | AssetTools M05 §2, §6 | CONFIRMED | baked-placement frustum cull vs WFC_NOFRUSTUMCULL: 60 nav views, 0 px different. Back-face (WFC_NOCULL) diffs are only back sides of single-sided light planes / sky dome seen from inside geometry. Winding vs normals agree 99.95% on all 1,952 instances (mirrored min 0.948) |
| Dark spawn wall (FFA start 15650) | VentWall_7168 lightmap scale 0.05 on all coefficients | world.glb extras + lighting.json | CONFIRMED authentic | authored dark lighting, not a defect |

**Questions for RE / AssetTools (exact):**
1. Unlit BSP (fc05672 §6): the 26 "visible" west-perimeter nodes have a front normal of -X in glTF and render only from x < -38.4 m. Every nav/start viewpoint is at x >= 19.9 m. Which viewpoint sees their front side? Is the orientation in glbray's visibility test front-facing?
2. TnPickupFactory.SetPickupMesh native attach offset for the ammo crate (render_index placement PARTIAL).
3. Pickup spin phase: does the factory yaw reset on respawn, or run continuously?
4. Flag / bomb factory at-rest visual (only needed for CTF / EXT).
5. Mesh-emitter bOverrideMaterial with no material set: does it use the mesh material?

## MILESTONE 04 — MP_IAC_STREETS LIVING-WORLD PRESENTATION + CORRECTED MAP (2026-10-03)
Provenance: AssetTools **a23c675** (complete-map manifests: streets_rebuild_diff / movers / kismet / lighting_audit,
render_index, map_fx) and **8d8195e** (StaticMeshCollectionActor component transforms S(Scale*Scale3D)*R*T*Parent,
1906/1906 validated, 478 mirrored); Systems **8dcb861** (pickup script flow, RE d50c2a9 P2); ReverseEngineering
earlier commits for lighting/shadows unchanged. Marks: CONFIRMED / HIGH / PARTIAL / UNKNOWN; VISUALLY VERIFIED =
deterministic capture inspected (not a comparison with the original game).

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Corrected map geometry | 1906 SMCA members with component scale / mirroring | AssetTools 8d8195e | CONFIRMED (data) | render data regenerated from the corrected world.glb; nothing compensated in Rendering; 24-start sweep + 5 targeted areas VISUALLY VERIFIED as coherent |
| Mirrored / non-uniform instances | det < 0 winding, normals by inverse-transpose | engine convention | CONFIRMED | winding restored at bake (existing); normals now cof(M)*sign(det) (were M*n: wrong under non-uniform scale) |
| Decal receivers on SMCA | receiver world matrix = component * parent | 8d8195e props.json | CONFIRMED | decal builder uses the corrected per-component matrix (was the native parent only) |
| Lightmap inventory | 1795 texture (1794 bound + destructible), 28 vertex, 133 none | streets_lighting_audit | CONFIRMED | builder now also scans the BASE package (StaticInterpActor_5249 / 8037 texture, SkyBeam + SMCA_4920 vertex); 1795 / 28 match per component; BASE inline atlases copied from the slice export |
| Lightmap attachment after the geometry fix | bindings unchanged (1755 SMCA) | 8d8195e validation | CONFIRMED | join keys unchanged; no misattachment found in the sweep; one hard-edged black vertex-lit patch (Arch_4096_STAT, SMCA_7201 / 2542_SMC, 22 of 522 authored samples black) left as authored [VISUALLY UNVERIFIED] |
| Rotating domes (3) | PHYS_Rotating 2730 UU/s yaw (15 deg/s) | streets_movers.json | CONFIRMED | per-actor delta M(t) M0^-1 on the baked world (UE3 FRotationMatrix + UE->glTF map reproduce every world.glb node matrix exactly); VISUALLY VERIFIED |
| SkyBeam Matinee (3 actors) | SeqAct_Interp 9.0022 s loop from GameplayStarted, IMF_RelativeToInitial, quat slerp, CurveAuto position | streets_movers.json | track CONFIRMED / UE3 evaluation HIGH | implemented; VISUALLY VERIFIED |
| Objective bases (4) | bHidden; Kismet UnHide under TnGameRules_SingleFlagCTF / ScoreBombingRun | streets_kismet.json | CONFIRMED | resident, hidden unless Gameplay's active rules include either; VISUALLY VERIFIED (DM hidden / CTF shown) |
| Domination totems (3) | NEU_EnergonTotem_SKEL, DeactivatedLoopAnim EnergonTotem_StandBy; Conquest only | render_index.json; mode rule per directive | CONFIRMED data / rule HIGH | drawn + animated only under TnGameRules_ScoreDomination; VISUALLY VERIFIED; lighting via generic dynamic env [PARTIAL] |
| Destructible WallPanelSign | state 0 Base mesh + texture lightmap; 1/2 Chunk02 stump + chunks / debris / FX | streets_destructibles.json | CONFIRMED data | state 0 intact (with its lightmap) and states 1/2 stump VISUALLY VERIFIED at the authored off-map location (placed meshes with authored components were frustum-culled on untransformed bounds: fixed); physics chunk / debris / destruction FX not presented [PARTIAL]; Gameplay drives setDestructibleState |
| Steam_Sm_FX x8 | auto-activate, loop; Steam_Mat; decoded modules | map_fx.json + pstream | CONFIRMED data / UE3 module semantics HIGH | simulated from the decoded stream (Location x2, Lifetime, Size, SizeMultiplyLife, Rotation, Velocity + radial, ColorScaleOverLife, DynamicParameter); VISUALLY VERIFIED. Gravity (WFC HmParticleModuleGravity, native) PARTIAL (not applied); DynamicParameter slot order PARTIAL |
| Pickup FX | Pickup_FX (highlight), HealthPickup_FX, OvershieldPickup_FX | streets_pickup_fx.json | CONFIRMED data | drawn only emitters whose look is invariant under both plausible readings of the unproven pstream flagA / flagB (build_map_fx.py flag_analysis): Health / Overshield GlowMut + the health energon-cube mesh. UNKNOWN (not drawn): ammo highlight beam (SizeMultiplyLife flag), GlowADD x2 (ColorScaleOverLife flag), overshield mesh (SizeMultiplyLife flag) |
| Pickup state | spawn available (ammo highlight active, RE P2); SetPickupHidden: custom SetHidden+Deactivate, highlight Deactivate; highlight attached only for ammo crates / objectives | decompiled script via Systems 8dcb861 | CONFIRMED | setMapEffectState("<factory>|custom/highlight", active, hidden); VISUALLY VERIFIED take / respawn with WFC_PICKUPTEST |
| MaxPeakCount | WFC emitter field, CDO 1 | authored + PeakActiveParticles | HIGH | active-particle cap; 1800-frame runs bounded (33-41 sprites, 9 meshes) |
| Material permutations | — | verify_permutations.py | — | 211 / 214 match: MaterialInstanceConstants without their own static permutation are now verified against the master's compiled resource; unnamed parameter expressions attributed. PARTIAL: MonitorScreen family (3 materials, 11 components) |
| VectorParameter output channel | outputs 1..4 = R/G/B/A | UE3 material types | HIGH | applied only where the full-vector reading violates UE3 type rules: ParticleBase_BW_MAT (2 violations -> 0); 21 other affected materials compile validly either way and keep the current reading [PARTIAL] |
| DeadBodies_Mat_INST | static switch permutation | MIC native | CONFIRMED | verifies (fixed by the earlier numbered-FName switch decode); the AssetTools wrong-material flag predates that fix |
| Map audit (state-aware) | — | audit_map.py | — | static meshes 1937 correct / 4 rule-hidden / 11 unknown; emitters 8 correct / 24 unknown / 13 intentionally invisible; LVV, destructible (intact), decals 25/25, fog, post correct |

## MILESTONE 03 PASS 5 — NORMAL-PLAY CHARACTER SHADOW RUNTIME (2026-10-02)
Native provenance: RE-Workspace `notes/MILESTONE03_RENDERING_CHARACTER_SHADOW_RUNTIME.md`, ReverseEngineering commit
**7033f18** (builds on 13c0953 / pass 4, Rendering checkpoint 566a87f). This pass supersedes the pass-4 rows "Projection
gates" (bit 0x4 is the SUBJECT's view relevance, not a light flag), "shadowFactor -> projection strength" and "Resolve /
blur". Character shadows now run in normal play without any opt-in.

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Synthetic projector | one synthetic shadow light per light environment; never the scene light | 0x82DE6598, vtable 0x822DD770 | **CONFIRMED** | `ShadowProjector` per DirectLightEnv (robot, vehicle) |
| Projector update | every proxy build (full and incremental updates): type (1 dir / 2 point / 3 spot), LightToWorld, Radius, ShadowFalloffExponent, cones, Min/MaxShadowResolution, ModShadowColor = shadowFactor; overwritten in place (no crossfade, no second projector) | 0x82CCE0A8, 0x82DDD128 / 0x82DDD240 / 0x82DDD368 | **CONFIRMED** | **APPLIED** (`updateShadowProjector`) |
| Directional projector values | Radius 327680 UU, ShadowFalloffExponent 2.0, cones (0, −1, −1, 0) | 0x82DDD128 | **CONFIRMED** | **APPLIED** (no Streets directional light casts a composite shadow: diagnostic only) |
| No record | shadowFactor within 1e-4 of white (and the earlier 0.05 drop) -> slot flags cleared, no shadow | 0x820A1AE0, 0x82DE6598 | **CONFIRMED** | **APPLIED** |
| Slot creation / registration / interaction linking | how the synthetic light is allocated, added to the scene and linked to the environment's primitives | — | **PARTIAL / UNKNOWN** | structural stand-in: the projector lives in the environment state and is cast for that environment's drawn form |
| Strength | FadeAlpha = 1.0 always; ShadowModulateColor = lerp(1, ModShadowColor, 1) = shadowFactor; all fading inside shadowFactor (0.2 luminance gap x smoothed visibility); no distance / resolution fade | 0x83056A20, 0x82D077F8 | **CONFIRMED** | **APPLIED**; the pass-4 authored-ModShadowColor opt-in substitute and WFC_SHADOWFADEALPHA are removed |
| Mod projection combine | out = lerp(lerp(1, SMC, atten), 1, PCF), atten = spot² (1 − saturate(|d/R|²)^ShadowFalloffExponent) | spot variant microcode | **PARTIAL** (directional / point variants not decoded) | decoded form for every type; directional uses the projector origin as the light position |
| Creation: light side | projector on, ModShadowColor not white (1e-4), light in at least one view | 0x83057028 | **CONFIRMED** | **APPLIED** (point / spot: light sphere vs view frustum; directional: always) |
| Creation: subject side | interaction PROJECTED for CastShadow ∧ bCastDynamicShadow; no ShadowParent; relevant ((relevance & 7) != 0) or visible; initializer type 1/2/3 | 0x82DBFA80, 0x83056A20 | **CONFIRMED** | **APPLIED**; Optimus robot / vehicle mesh components: CastShadow True (MeshComponent), bCastDynamicShadow True, bCastHiddenShadow False (authored.db) |
| IsShadowCast (relevance 0x4) | CastShadow required; hidden / owner-see cases -> bCastHiddenShadow; else within MaxDrawDistance (LODDistanceFactor) | 0x82DD8D58 | **CONFIRMED** | **APPLIED** (`shadowViewRelevance`); CachedCullDistance 0 -> unlimited; hidden forms are not drawn -> no shadow |
| DPG relevance | bit 5 + DPG from the proxy: DepthPriorityGroup, or ViewOwnerDepthPriorityGroup when bUseViewOwnerDPG and the view's owner matches | 0x82C8BCF0 | **CONFIRMED** | **APPLIED**; Optimus meshes: SDPG_World, no view-owner DPG (authored) -> World pass |
| Occlusion / preshadow | per-view shadow occlusion query; a preshadow when the subject is visible (static receivers from the light's static list) | 0x82DEA630, 0x83056A20 | CONFIRMED (existence) / PARTIAL (synthetic light's static list unknown) | not reproduced (no occlusion queries; preshadow casters unknown) |
| Shadow children | ShadowParent children folded into the parent's single shadow | 0x83056958 | **CONFIRMED** (mechanism) | the Ion Blaster mesh has its own LightEnvironment and CastShadow True; whether script sets its ShadowParent is UNKNOWN -> weapon not a caster (unchanged) |
| Skeletal proxy early-out | proxy 0x82C9E5C0 returns 0x8242 (foreground DPG, no shadow bit) under unverified conditions | — | **PARTIAL** | not reproduced |
| Shadow space | perspective for every type; origin = light position (point / spot) or B.Origin − (2R + 300 UU)·axis (directional); pulled back to √2·R; MinLightW 0.1 UU; MaxLightW = Radius or 2(2R + 300); DynamicShadowDepthBias input (0 on both forms) | 0x82E22A38, 0x82DBF678, 0x82DBF728 | **CONFIRMED** | **APPLIED** |
| Frustum fit / ScreenToShadowMatrix | VMX128 bounding fit of the projected corners; texel / half-texel terms | 0x8301CD08, 0x82CFB598 | **PARTIAL / UNKNOWN** | sphere-tangent perspective, depth clamped to [MinLightW, MaxLightW] [PROV] |
| Shadow resolution | clamp((int)(1.0·ScreenRadius), min(Min, Buf − 10), min(Max − 10, Buf − 10)); ScreenRadius = max(0.5·SizeX·P00, 0.5·SizeY·P11)·R / max(clipW, 1 UU); light 0 -> 128 / 1024; Buf 1024; 5-texel border | 0x83056A20 | **CONFIRMED** | **APPLIED** (Streets lights author no Min/MaxShadowResolution) |
| Blur | only if a shadow was drawn; H then V; 6 bilinear clamped taps at ±0.5 / ±1.5 / ±2.5 texels; weights {4, 2, 1}·(4 − s), normalised; horizontal output squared | 0x82DDB070, 0x82E0C8A0, Engine ShaderCache_10303 | **CONFIRMED** | **APPLIED**; GPU == closed form (max error 0 / 255) |
| Blur tie branch | (s(−B) == s(−A)) ∧ (s(+B) > s(−B)) alternative result | decoded swizzle uncertain | **PARTIAL** | off by default; WFC_BLURTIE=1 enables the decoded form |
| Transform | each environment owns its projector; only the drawn form casts | 13c0953 §4, 7033f18 §5 | CONFIRMED mechanism / environment survival across transform UNKNOWN | one projected shadow per frame through r2v / v2r; the hidden form's projector is stale but casts nothing |
| Normal-play result | — | — | — | spawn 18 robot (baked PointLight_4177_LC, ShadowModulateColor 0.098), spawn 21 robot (PointLight_14841_LC, 0.825), vehicle at 18 (0.105); spawns without a composite light: projector off, mask 1 |

## MILESTONE 03 PASS 4 — NATIVE CHARACTER SHADOW MASK, DLAC, BIAS / PCF (2026-10-02)
Native provenance: RE-Workspace `notes/MILESTONE03_RENDERING_CHARACTER_SHADOW_PATH.md`, ReverseEngineering commit
**13c0953** (supersedes the pass-3 UNKNOWN rows for the shadow-mask write path and the DLAC formula). Authored data:
AssetTools commit **7a69756** (`manifests/fineaim_hud.json`, `streets_pickup_fx.json`, `streets_destructibles.json`).

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| ShadowMask target | A8R8G8B8 at the scene buffer size SizeX/f x SizeY/f, f = (SizeX > 960) ? 2 : 1 (640x360 at 720p) | 0x83037390, 0x8302AE28 (factor read from the image this pass) | **CONFIRMED ORIGINAL** | **APPLIED** (RGBA8 + D24S8 stencil buffer) |
| Mask clear / convention | cleared once per build to (1,1,1,1); stores visibility (1 = lit) | 0x83010218 (`*0x8370AE88`) | **CONFIRMED ORIGINAL** | **APPLIED**; no projection -> mask stays 1 (neutral texture) |
| Projection region | z-fail stencil volume of the 8 frustum corners (front zfail Inc, back zfail Dec, Always), then the box drawn where stencil != 0; stencil cleared after each light | 0x8304EF70 | **CONFIRMED ORIGINAL** | **APPLIED** (GL_INCR_WRAP / DECR_WRAP, depth clamp, back faces once per pixel) |
| Mask combination | blend Src = DestColor, Dst = Zero (rgb), alpha Src = Zero, Dst = One: mask.rgb *= projection.rgb, alpha untouched; every shadow multiplies into one mask | 0x82CF8048 | **CONFIRMED ORIGINAL** | **APPLIED** (BlendFuncSeparate) |
| Resolve / blur | resolve, then BlurShadowMask (horizontal, vertical) only if something was drawn | 0x82DEC190, 0x82DDB070 | placement **CONFIRMED ORIGINAL** / kernel **UNKNOWN** | resolve implicit; blur NOT run (no invented kernel) |
| Character read | ShadowMaskTexture .r at screen UV + ShadowMaskTexelOffset (0.5 / mask size) | 0x82DDAEF0 + uber-shader microcode | **CONFIRMED ORIGINAL** | **APPLIED** (point sampled; GL pixel-centre screen UV) |
| DirectLightAmbientContribution | rgb = CubeSum(LightsSH) / (CubeSum(AmbientSH + LightsSH) + 0.001), w = 0; LightsSH = overflow lights + (1 − f) of the crossfaded light; per environment, every update | 0x82CCE0A8, 0x82CED7F8, 0x82CED980, 0x82E2EDA8 | **CONFIRMED ORIGINAL** (copy to +0x1D0 HIGH) | **APPLIED** from the env's ambient cube (Streets: no SH probes; cube vs SH basis PARTIAL as before); measured 0 … 0.95 across spawns |
| ShadowDepthBias | (Res · MaskedShadowsDepthBias / ShadowFilterRadius)² = (1024 · 0.2 / 6)² = 1165.08, parabolic; no slope-scaled / receiver bias; DepthBias (0) unused | 0x82CFB598 | **CONFIRMED ORIGINAL** (CPU) | **APPLIED** in the decoded projection bias term |
| Shipped shadow constants | MaxShadowResolution 1024 (Res clamp [1, 2048]), MaskedShadowsDepthBias 0.2, ShadowFilterRadius 6, DepthBias 0, ShadowFilterQuality 0, bEnableBranchingPCFShadows True | Xe-TransEngine.ini, 0x830101E0 | **CONFIRMED ORIGINAL** | **APPLIED**; shadow buffer Res x Res, per-shadow viewport |
| BranchingPCF | 4 edge taps rotated by RandomAngleTexture, then 12 refining taps when lit is fractional; offsets = native tables x 6 / Res | 0x83711DFC, 0x83711EC0, 0x82CF1A00 | **CONFIRMED ORIGINAL** | **APPLIED** (exact tables) |
| Projection gates | light render flags bit 0x4 and the DPG bit (bit 5 + DPG) | 0x8304EF70 | **CONFIRMED ORIGINAL** (which lights set the bits UNKNOWN) | **APPLIED**; all lights carry both bits; WFC_LIGHTRENDERFLAGS test override |
| Composite light eligibility | baked lights can be the composite light; robot and vehicle identical path | 13c0953 §4 | **CONFIRMED ORIGINAL** | baked PointLight_4177_LC selected at spawn 18 (robot, vehicle, transform, walk) |
| shadowFactor -> projection strength | ShadowModulateColor = lerp(1, ModShadowColor, FadeAlpha) confirmed; the link from the composite record's shadowFactor to that light / FadeAlpha not proven | 0x82D077F8, §1.6 | **PARTIAL / UNKNOWN** | not wired; opt-in path uses the light's ModShadowColor, FadeAlpha 1 (WFC_SHADOWFADEALPHA) |
| Projection shader bias / offset use | in-shader use of ShadowDepthBias / offsets (decoded here earlier from engine microcode; 13c0953 leaves it PARTIAL) | — | **PARTIAL** | decoded microcode form |
| ScreenToShadowMatrix | VMX construction not decoded | 0x82CFB598 | **UNKNOWN** | inverse view-projection + subject-fit matrix [PROV] |
| Projected-shadow creation gates | distance / resolution / cast-flag cuts not traced | — | **UNKNOWN** | none added |
| Downsampled depth for the mask stencil | which depth the mask-sized stencil test uses | 0x83010218 binds SceneDepth | **PARTIAL** | point-sampled scene depth |
| Normal-play status | — | — | — | character projection stays opt-in (WFC_CHARSHADOWS=1) until shadowFactor, the shadow matrix and creation gates are recovered; the default mask is the native "nothing drawn" (1,1,1,1) |
| Ion Blaster fine-aim HUD | no scope / ADS overlay; mc_crosshairIonBlaster in both states; prongs move by spread x 300 stage px, 0.2 s easeout, first value instant; controller at (0.2, 0.2) px; FineAimSpreadModifier 0.5 | AssetTools 7a69756 fineaim_hud.json | CONFIRMED AUTHORED DATA (spread combination HIGH) | first value instant, +0.2 px anchor, HUD spread uses the fine-aim modifier |
| Pickup FX materials | LightVolume_WepPickup_MAT, Glow_Mod_Depth_MAT, ParticleBase_BW_MAT, Basic_Particle_Add_MAT, EnergonCube_Circuits_MATINST, Overshield_MATINST, WEP_Crates_MAT (cooked in TransGame) | streets_pickup_fx.json | CONFIRMED AUTHORED DATA | compiled from TransGame.xxx (fallback package); all match their shipped permutations; emitters are Systems-owned |
| Wall-panel destructible | TnStaticDestructibleActor_14465 at (896, 89968, −352), ~139k UU from the play space; WallPanelSign_MATINST / _EMISSOFF_MATINST states | streets_destructibles.json | CONFIRMED AUTHORED DATA | state materials compiled; actor not moved / not drawn (outside the playable area) |
| TR_AllShader_p.Textures.bubbles | not cooked in the Streets packages; TransGame copy: Texture2D DXT1, SRGB False | TransGame.xxx | CONFIRMED | ENV_EngergonGlass_MAT_INST now uses the real flags (was defaulted SRGB True) |

## MILESTONE 03 PASS 3 — NATIVE LIGHT VISIBILITY, CHARACTER LIGHTING, SHADOW STRENGTH, COLOUR (2026-10-02)
Native provenance: RE-Workspace `notes/MILESTONE03_RENDERING_LIGHTVIS_SHADOW.md`, ReverseEngineering commits
**b52dca9** (LightsVisibilitiesVolume + DirectLightEnv), **c95dadd** (gather, spot intensity, update queue,
transitions), **31f9a9b** (DynamicShadowLuminanceScale shader consumption). Marks: CONFIRMED ORIGINAL / HIGH /
PARTIAL / UNKNOWN.

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| LightsVisibilitiesVolume layout | u8 bHasOctree; recursive node {8 x i32 corner (low 16 bits), i32 hasData, u8 hasChildren, 8 children}; centre, root half, u32 +0x18, finest half; GUID table; u16 corner remap; per-sample count bytes; u32 pair pool | 0x82DF5748 / 0x82DF5590 | CONFIRMED | **APPLIED** (render/LightVisibilityVolume.*) |
| Streets blob | 18153 nodes, centre (19072, −43008, −72448), root half 26729.2, finest half 512, 268 GUIDs (256 bound to level lights by LightGuid), 17487 corners, 3096 samples, 7679 pairs, indices strictly ascending, bit 7 never set | lvv_decode.py / runtime self-check | CONFIRMED (data) | validated at load |
| Count-byte halving | archive flag halves count bytes on load; Streets pool only matches with halved counts | blob arithmetic | CONFIRMED for Streets / PARTIAL flag identity | applied when the halved counts match the pool |
| Octree +0x18 | 0x45800000 (4096.0 as a float); not read by the query | blob | UNKNOWN meaning | ignored (as the query) |
| Count bit 7 | honoured as "invalid" on the face path only | 0x82DDDA08 | PARTIAL meaning | reproduced literally |
| Query | strict root test; child (x>c)<<2|(y>c)<<1|(z>c); corner bit0 X; remap; plain trilinear (0-pair corners renormalized away) or T-junction face blend (6 faces x 4 corners, weight 1/3, finer neighbours); 16-bit fixed-point merge with truncation; renormalize 1/(1−emptyW); outside / empty leaf -> no data | 0x82DDE200 / 0x82DDDA08 / 0x82DDD798 / 0x82E03238 | CONFIRMED | **APPLIED**; C++ == independent Python port at 400 points (163 outside, 130 empty leaves, 77 trilinear, 30 face-blend) |
| Baked vs unbaked | baked lights (bound in the table): volume visibility at the bounds origin, no rays, 0 outside / empty; unbaked: N = clamp(round(15·√lum(colour·Imax) + 0.5), 1, samples) sample rays, 0.03 / 0.01 thresholds, stagger mod 5, ray gate CastShadows && CastStaticShadows, ray ends 15 UU short | 0x82CE1918 / 0x82CC2700 | CONFIRMED | **APPLIED** (no rays for baked lights) |
| Gather | LightAffectsEnv: enabled; light function only with composite shadow; CompositeDynamic -> Dynamic; shared channel; special channels ⊆ env; point/spot RadiusOfInfluence + bounds radius; spot sphere-vs-cone (clamped outer) | 0x82DF5068 / 0x82CC2608 / 0x82DD3EF0 / 0x82E2CDF8 | CONFIRMED (cone test arithmetic HIGH) | **APPLIED**; env channels robot Dynamic+PlayerOnly, vehicle Dynamic (HIGH) |
| IntensityAt | point B·max(0, 1 − (d/R)²)^F; spot point · cone², cones clamped (inner [0, 89]°, outer [inner + 0.001, 1.5543429] rad); directional B | 0x82DC1F60 / 0x82E2CFE0 / 0x82DBEAC0 | CONFIRMED | **APPLIED** |
| Ranking | direct: lum(LightColor/255 · I) · vis (0.3/0.59/0.11), cap TotalLightCount 2, boundary crossfade into ambient; composite: lum without visibility, cap 1, 0.2 runner-up fade, × smoothed visibility, factor 1 − fade, drop within 0.05 | 0x82CDDE70 / 0x82CCB110 / 0x82CD4540 | CONFIRMED | **APPLIED** |
| Updates | mode 1 when the bounds origin moves > DetailScale[DetailMode 2]=3 × 30 UU (×100 squared after 0.1 s unrendered) or settles; mode 2 incremental every tick; full updates through the global queue (deadline frame + 2, FIFO 2 ms budget); first update after attach immediate | 0x82DD17C8 / 0x82CD2538 / 0x82CDD438 / 0x82DBEF90 | CONFIRMED (budget availability PARTIAL) | **APPLIED** |
| Transitions | visibility moves linearly at clamp(|v|·0.002, 0.2, 1)/0.5 per second inside updates only; no between-update interpolation for WFC pawns | 0x82CDDE70 / 0x82CD3840 | CONFIRMED | **APPLIED** |
| DirectLightEnv self-test | 20 checks (RoI boundary, channels, spot cones, queue deadline, first update, transition) | WFC_DLETEST | — | 20/20 PASS |
| DynamicShadowLuminanceScale shader use | mask = ShadowMaskTexture.x; S = (1 − mask)(1 − DSLS); character uber pass: ambient·(1 − DLAC·S) + direct·(1 − S), one mask for the summed lights; per-light pass light·material·(1 − S); before SceneColorBiasFactor, linear, no clamp | Xenos microcode (31f9a9b) | **CONFIRMED ORIGINAL** | **APPLIED** in the uber pass |
| DSLS shipped value | 0 (BSS; no config sets it) -> full native dynamic-shadow strength | 0x8382DDF4 | CONFIRMED | 0 (WFC_DSLS test override, clamped [0, 1]) |
| DirectLightAmbientContribution CPU calculation | SH ratio in proxy build 0x82CCE0A8 (HIGH), formula not recovered | 31f9a9b | **PARTIAL / UNKNOWN** | 0 (inert while the mask is 1); WFC_DLAC test override |
| Shadow-mask write generation | how the composite shadowFactor reaches ShadowMaskTexture | — | **PARTIAL / UNKNOWN** | neutral mask 1.0 (= the unshadowed original); WFC_SHADOWMASKTEST test hook |
| DSLS validation | DSLS 1 bit-identical to the unmasked render; world pixels untouched; DSLS 0 < 0.5 < 1 on characters; default render bit-identical to the previous checkpoint | lockstep matrix (robot/vehicle bright+dark, transform, walk) | — | PASS |
| Projected shadow stages | caster depth VS, branching-PCF projection PS, RandomAngles texture, frustum-bounded projection | engine/BASE microcode | CONFIRMED | implemented; opt-in WFC_CHARSHADOWS (native DepthBias / ShadowDepthBias / PCF offset tables UNKNOWN) |
| Texture gamma | UE3 SRGB textures use Xenos gamma formats: PWL degamma (64/96/192 segments, trunc correction, /1023) | xenia (Source X360GammaToLinear + D3D9 disassembly) | HIGH | **APPLIED** (RGBA16 linear upload; WFC_SRGBCURVE A/B) |
| Lightmap textures | LightMapTexture2D inherits Default__Texture SRGB=True -> PWL | Engine defaults | CONFIRMED | PWL |
| Vertex lightmaps | exp2(log2(max(|c|, 1e-4))·2.2)·LightMapScale[k] per vertex | vertex-lightmap VS microcode | CONFIRMED | **APPLIED** |
| CPU colours | FColor -> FLinearColor through pow(i/255, 2.2) table (light colours, ModShadowColor) | PowOneOver255Table 0x82251110 | CONFIRMED | pow 2.2 |
| HUD textures | UI_GFxHud_p textures SRGB False, composited raw | cooked flags | CONFIRMED | raw |
| Weapon muzzle light | TnWeaponMesh.MuzzleFlashLight = IonBlaster PointLightComponent (Radius 3000, FalloffExponent 75, RadiusOfInfluence 504.7, Brightness 10, colour 255/143/140, bCastCompositeShadow, ModShadowColor (3, 0.05, 0.04)), all light channels | WEP_IonBlaster_p archetype | CONFIRMED data | not driven (Systems/Gameplay owner) |
| DeadBodies_Mat_INST | static switch array with a numbered FName and a stale entry | MIC native tail | CONFIRMED | decoded; permutation now matches (189/203) |

## MILESTONE 03 PASS 2 — RENDERING (2026-10-02, branch agents/rendering)
Marks: **CONFIRMED ORIGINAL** (cooked data / Xenon microcode / shipped ini) · **HIGH** (standard UE3 semantics on
confirmed data) · **PROV** · **UNKNOWN**. Deterministic captures: `WFC_LOCKSTEP=1`; audit set
`bash tools/render/capture_audit.sh` (frame reports in docs/rendering/audit/).

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Distortion accumulate | s = 4·Distortion.xy; kill if dot(s,s) − 0.1 < 0; clamp ±255; ×1/255; RG = max(s,0), BA = abs(min(s,0)) | Xenon PS of Ring_Distort_Add_MAT (BASE shader cache; literals 1.25 / −0.15 / 0.1 identify it) | CONFIRMED | **APPLIED** per-material variant, additive, scene-depth tested |
| Distortion apply | uv + (acc.rg − acc.ba)·(0.25, −0.25) (D3D v-down), SceneColor fetch | engine PS (AccumulatedDistortionTexture / SceneColorTexture) | CONFIRMED | **APPLIED** before post |
| Distortion RT | 8-bit UNORM implied by the ±255 / 255 encode | microcode encode | HIGH | RGBA8 |
| Distortion order | after translucency, before post | UE3 frame order | HIGH | applied in endFrame |
| Hover / ram emitter materials | base_glow → Glow_Mod_MAT (modulate), rays_Dup → Trail_Distort_MAT (distortion), ram dust → Distortion_Cloud_01_MAT | ParticleModuleRequired.Material | CONFIRMED | all compiled with distortion / modulate paths; **emitters not spawned (Systems)** |
| Vehicle slots | slot 0 RB_OptimusPrime_Cust2_Mat_INST (9728 tris), slot 1 InteriorAlt_Energon_MAT_INST (1923) | cooked slot table (umodel) | CONFIRMED | matches; full audit docs/rendering/vehicle_material_audit.md |
| Vehicle normal map | VH_Optimus_NORM DXT5; graph reads .a (X) and .g (Y); unpack −1; UseReconstructedNormal = True | texture + graph + MIC switch | CONFIRMED | matches |
| Energon colour | compiled permutations of both Optimus MICs embed only the red EnergonColor default (1.25, 0.05, 0.05) | FMaterialResource | CONFIRMED | red |
| BSP polygon winding | cooked vertex order consistent with the surface normal (Newell: 0 of 889 polygons need flipping) | cooked FModelVertexBuffer + surface normals | CONFIRMED | **FIXED**: 44 polygons had been flipped by a degenerate first-triangle test and culled from above, showing the fog-coloured clear (flat pink / lavender floors) |
| Hidden actors | bHidden InterpActors (RepairNodeB ×2, Base_D, Base_A) | props_authored.json (effective values) | CONFIRMED | not drawn (WFC_SHOWHIDDEN to inspect) |
| No-light components | bAcceptsLights False / empty LightingChannels: 123 unlit FX meshes + AutobotSign ×2, CityBackdrop ×2, SpaceDome | props_authored.json; UE3 channel overlap | CONFIRMED data / HIGH semantics | zero light environment (emissive only) |
| Unlit props without baked lighting | 124 MLM_Unlit FX meshes (light planes, beams, glow spheres, stains) | materials | CONFIRMED | intentionally none |
| Dark spire wall (spawn 10) | baked lightmap at map percentile 42 (dark blue) | lightmap texels | CONFIRMED | authored appearance |
| Character light visibility | LightEnvironmentComponent NormalizedSampleOffsets: robot (0,0,.9) (0,0,−.7) (.7,.7,.7) (−.7,−.7,.7) (.7,−.7,−.7) (−.7,.7,−.7); vehicle (0,0,.7) (±.8,±.8,0); TotalLightCount 2; UpdateDistanceThreshold 30 UU | TransGame Default__TnRobotForm / TnVehicleForm | CONFIRMED data | **APPLIED**: visibility = fraction of samples at bounds origin + offset·extent with a clear path (HIGH semantics); form chosen by material package (_ROBO_p / WEP_ → robot, _VEH_p → vehicle) |
| Lighting channels | robot mesh adds PlayerOnly; Streets lights: 266 all-channel, 2 dynamic-only, none PlayerOnly | TransGame + level lights | CONFIRMED | no change needed |
| Character shadows | modulated projected shadows: SceneColor ×= lerp(lerp(1, ModShadowColor, atten), 1, lit²); 4-tap PCF, receiver depth clamp 0.999; atten = spot² · (1 − sat(abs(d/R)²)^ShadowFalloffExponent); ini ShadowFilterQuality 0, Min/MaxShadowResolution 128/1024, ShadowTexelsPerPixel 1, ModShadowFadeDistanceExponent 0.2, LightEnvironmentShadows True; ~35 lights bCastCompositeShadow | engine PS microcode + Xe-TransEngine.ini + light props | CONFIRMED (projection / filter) / UNKNOWN (shadow light selection) | **NOT IMPLEMENTED**: the WFC LightEnvironmentComponent shadow-light (composite) selection is in default.xex (ReVa request) |
| Light visibility volume | LightsVisibilitiesVolume_2224 (Location (19072, −43008, −72448), DrawScale3D 26729): 744772-byte native blob = octree head (714056 B: BE int32 child indices, 32-byte light bitmasks per node) + 7679 (u16 light index 0..267, u16 visibility in 1/20 steps) pairs, preceded by per-cell even byte counts | cooked native data | CONFIRMED fragments / UNKNOWN cell → pair mapping | **NOT USED**: node record layout + light index order need default.xex (ReVa request); runtime traces kept |
| First-shot hitch | no renderer resource is created during combat on this branch (first-use log empty; render span 6.2 ms on shot frames); the M02 hitch (programFor → texture → decodeImage) is now prewarmed at load | WFC_RENDERSTATS first-use / spike log | CONFIRMED (measurement) | prewarm of all unused compiled materials after frame 1 |
| Build configuration | M03 perf numbers were Debug builds; Release: skinned vertex rebuild 3.13 → 0.24 ms, idle submit 5.75 → 1.93 ms | measurement | CONFIRMED | measure with Release (build/release) |

## MILESTONE 03 — RENDERING FIDELITY (2026-10-02, branch agents/rendering)
Verification tooling: `tools/render/verify_permutations.py` diffs every translated graph's parameter reads against
the parameter list of the material's COMPILED FMaterialResource (uniform expressions in the cooked native tail):
30 → **187 / 202 materials match** (remaining: 10 unknown = unnamed "None" parameters or textures the original
compiler eliminated; 1 differs = DeadBodies_Mat_INST, static switches not decoded). `tools/render/audit_map.py`
writes `work/render/<Map>/map_audit.json` (EXPECTED from cooked levels + AssetTools exports vs ACTIVE from the
renderer's upload dump `WFC_AUDIT_DUMP`).

| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| CameraVector / ReflectionVector space | `CoordinateSpace = CS_World` on 11/9 nodes (vehicle, robot, world reflections) | expression props | CONF | **FIXED**: world-space vectors (were tangent) |
| Cube reflection LOD | WFC `TextureSampleParameterCube.LODBias` input (Reflection_LOD_Scale) | expression props | CONF | **FIXED**: texture(…, bias) |
| Fresnel exponent input | WFC `Fresnel.Exp` input overrides Exponent | expression props | CONF | **FIXED** |
| Static switch decode | MIC native tail arrays validated against real StaticSwitchParameter names | cooked MICs | CONF | **FIXED** (bogus `_DialogEventManager` records rejected) |
| Lazy function inputs | unused material-function inputs emit no code (switch-dependent reads) | compiled permutations | CONF | **FIXED** |
| DepthBiasedAlpha | Alpha·saturate((SceneDepth−PixelDepth)/max((1−Bias)·BiasScale,.001)); WFC BiasScaleInput | UE3 semantics + props | MED | **APPLIED**: scene-depth copy (blit when opaque drawn), view-Z depths |
| PixelDepth | view-space Z (UE units) | UE3 semantics | MED | **FIXED** (was eye distance) |
| ScreenPosition (bScreenAlign false, all 16 nodes) | clip-space position; UE3 infinite-far: w = view Z, z = view Z − near | UE3 semantics | MED | **FIXED** (was fragcoord, z = 0 ⇒ Boostermaterial_02 near-fade made the flame black; light-volume materials affected too) |
| Blend-mode fog | additive: c·fog.a; modulate: lerp(1,c,fog.a); others c·fog.a+inscatter | UE3 base pass | MED | **APPLIED** (additive surfaces no longer gain fog colour) |
| Vehicle / weapon FX materials | emitter materials (ParticleModuleRequired.Material) of bumble_boost_small1_FX, CarHover_A_01_FX, Truck_ram_FX, weapon FX: 27 graphs | cooked ParticleSystems | CONF | **COMPILED** (`tools/render/fx_materials.txt`); mesh emitters + sprites shaded by them (`ParticleBatch.material`, drawMeshFx) |
| MeshEmitterVertexColor | compiled as VectorParameter "MeshEmitterVertexColor" = particle colour | compiled permutations | CONF | particle colour → vertex colour |
| Ram_model_MAT rim | WFC ShaderCode `saturate(pow(saturate(abs(dot(CameraVector(CS_World), Normal))*1.5),2))` | expression props | CONF graph / PROV semantics | reproduced literally (Normal node = tangent normal, as for all other users); brightness depends on view elevation — confirm with microcode |
| Actor-placed props | StaticMeshActor / StaticInterpActor nodes carry only `actor` in world.glb | world.glb, lighting records | CONF | **FIXED**: actor → its single StaticMeshComponent ⇒ 38 submeshes now use their baked lightmaps |
| Vertex lightmaps (LMT_1D) | 24 components (Arch_4096, Core_RoutingWallA, PowerTubeCircle, CoolantSurfaceHole, declogo): bulk FQuantizedDirectionalLightSample (3 × FColor A,R,G,B; per-channel normalized to 255) + ScaleVectors[3] | cooked StaticMeshComponent native tail | CONF layout / PROV gamma | **APPLIED**: decoded pow(b/255, 2.2)·scale through the 3-coefficient directional formula (vertex count matches LOD0 for all 24) |
| BSP elements without lightmaps | 360/540 elements: LightMapType 0, no IrrelevantLights / ShadowMaps / light GUIDs | cooked FModelComponent | CONF data / PROV runtime | dynamic light environment (assumed UE3 uncached interactions) — audit status `unknown` |
| HUD crosshair (Ion Blaster) | Hud_GFX.gfx `mc_crosshairIonBlaster`: 3 × bitmap 400 (32×16, fill-stretched to 30×12) at 0/120/240°, anchor stage (560,360) of 1120×720; prong `_y` eases (0.2 s) to −300·WeaponSpread; tint by target type 0x50B5D5 / 0xFF3333 / white | GFx tags + AVM1 (sprite 404, 621) | CONF | **APPLIED** (`IRenderer::setReticle`); scale mode + easeout curve PROV |
| Fine aim presentation | `NotifyFineAimChanged`: scope only for HeavyPistol/BurstRifle/SniperRifle; Ion Blaster → `hideScope` (crosshair unchanged; prongs follow WeaponSpread) | AVM1 sprite 621 | CONF | matches: no scope for the Ion Blaster |
| Transformed-vehicle / robot materials (mottled grey-pink vehicle, wrong energon after transform) | each form's own MICs | renderer bug | CONF | **FIXED**: dynamic-mesh program cache was keyed by Material* address; the character pose buffer reuses storage across forms, so the vehicle got the robot's programs (robot textures on vehicle UVs). Now keyed by material content |
| FX GL1 stand-ins | Systems' per-mesh intensity (LightCylinder 0.1 = DustPower) double-applied once the real graph runs | — | CONF | `IRenderer::evaluatesFxMaterials()`; VehicleFx drops the stand-in when true (hover light cones visible again) |
| Missing TexCoord channels | UE3 FLocalVertexFactory binds the last available texcoord for missing channels; Light_Cylinder_STAT is cooked with NumTexCoords = 1 | cooked vertex buffer | CONF | **FIXED**: loader duplicates UV0 into UV1; material TexCoord[1] now reads the raw channel (was the lightmap-transformed UV). Hover light-cone plumes (LightVolume graph samples TexCoord1) now render as depth-faded downward plumes |
| Energon colour (Optimus) | UseAutobotEnergonColor = True → A branch EnergonColor (1.25,0.05,0.05) red; blue (0.2,0.1,1.5) is the False branch | MIC static switches + compiled permutations of both Optimus MICs embed the red default only | CONF | red is correct with the all-zero runtime colour set; no Gameplay call site exists (setCharacterColors never called) |
| Debug box in transform stills | World debug overlay (capsule wireBox + aim ray) toggled by an interactive 'B' press during a scripted run | Application input | CONF | interactive toggle ignored in WFC_SMOKE_FRAMES runs (WFC_DEBUGDRAW opt-in) |
| Mesh-particle light environment | UE3 mesh emitters share the particle system's light environment | UE3 design | MED | small dynamic meshes (< 0.5 m) share a per-1 m-cell env, refreshed every 60 frames |
| Light-env visibility traces | per-light traces from the object (static lights/world) | UE3 light env | CONF | memoized per (light, 0.25 m cell); unlit (FX) programs skip the env entirely |

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
