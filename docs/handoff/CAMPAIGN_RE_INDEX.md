# Campaign RE index (entry point for the campaign code phase)

WFC RE lane, 2026-10-18. Every doc is in `RE-Workspace/work/handoff/`; Integration mirrors them in `docs/handoff/`.

**Labels:** CONFIRMED = read in WFC code or data; HIGH = stock UE3 behaviour on an identical WFC schema; PARTIAL = known in shape,
detail missing.

## Docs

| Doc | One line |
|---|---|
| CAMPAIGN_STREAMING_RE.md | Per-level target states, SetStreamingState Started / Finished / Aborted, map-load blocking flush, checkpoint restart (in place vs URL travel), player-only save data; corrections to CAMPAIGN_STREAMING_RUNTIME.md |
| CAMPAIGN_KISMET_HANDLERS_RE.md | Exact handlers for the 15 High Moon Kismet classes: MissionComplete, AiMoveTo, TacticalVolume, UseTurret, DisableShooting, DownRevive, Togetherness, Jump, and the event output sources |
| CAMPAIGN_MATINEE_RUNTIME_RE.md | SeqAct_Interp outputs and skip (Aborted), director cuts, cinematic mode, move / anim / FaceFX tracks; WFC enum corrections |
| CAMPAIGN_HUD_OBJECTIVES_RE.md | ToggleCinematicMode per flag, objective bank and HUD (no completion visual), checkpoint and mission-complete presentation, campaign HUD callbacks and GFx symbols |
| CAMPAIGN_AI_LAYER_RE.md | Tactical volumes, buddy rules (never downed, instant respawn, no revive, AwayFromBuddy cost), AI sets per type, downed values, Kismet factory pacing |
| FACEFX_BONE_BLEND_RE.md | The FaceFX bone blend: product of per-link slerp / lerp (0.55), linear position, no clamp, Y-flip, additive |
| DEATH_PICKER_AND_LIGHT_RE.md | Death-entry picker order and the death point light |
| DEATH_CAMERA_RE.md | Death camera (inherited orbit) and the gibbed camera shake |
| SKELETAL_GIB_INIT_RE.md | Skeletal gibs: one per death, ref pose, spawn rotation, velocity |
| HOVER_RIDE_HEIGHT_RE.md | Hover probe springs, AI vs player righting, robot-form hover |
| HDR_SCENE_PATH_RE.md | 7e3 scene colour, exponent bias / resolve, bias constant, ceiling 3.984 |
| KILLER_CAM_RE.md, MP_TRANSFORMING_ACTORS_RE.md | MP-side; listed for completeness |

The older pass notes (`notes/TARGETED_PASS5_2026-10-05.md` §12 death pipeline, `notes/MILESTONE_E_OBJECTIVE_MARKERS.md`,
`notes/MP_PROGRESSION_SCORING_AI_2026-10-06.md`) remain the detailed sources these docs cite.

## Open items across the campaign docs

| Doc | Item | Label |
|---|---|---|
| Streaming | Native `GenerateCheckpointData` packaging (script shows player-only save data) | PARTIAL |
| Streaming | Native notify ids 0x32–0x38 (loading-screen and travel phases); one optional "named level made visible" step in the in-place restart | PARTIAL / H |
| Streaming | The per-frame streaming update internals (AddToWorld time-slice budget) | HIGH |
| Kismet handlers | Native FMV playback behind SeqAct_MoviePlayer | PARTIAL |
| Kismet handlers | TnSeqEvent_Together (togetherness touch / all-in / all-out events) not traced | PARTIAL |
| Matinee | FInterpCurve evaluation, MoveFrame RelativeToInitial, LookupTrack, AnimControl slot playback (native code not traced) | HIGH |
| Matinee | Director-termination restore blend (a 0.1 s parameter) | H |
| HUD | Subtitle canvas placement (Engine SubtitleManager) | PARTIAL |
| HUD | Which anchor clip each campaign HUD element sits under | PARTIAL |
| AI | Stock ActorFactory stepping (SpawnCount / SpawnDelay / PointSelection) | HIGH |
| AI | TnTogethernessVolume event timers; buddy BuddyLead emote finder details | PARTIAL |
| FaceFX | Master-bone quaternion M = ref⁻¹ | HIGH |
| FaceFX | Face-graph node value clamping upstream of the bone pass | PARTIAL |

**Next (in progress):** CAMPAIGN_SETPIECES_RE.md: Omega Supreme (D5), Trypticon (A5), tank / flight / rail sections, detpacks.
