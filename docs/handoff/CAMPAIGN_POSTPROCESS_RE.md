# Campaign post-process: volume chains, LUTs, fog, Kismet / Matinee changes (D5_OME, A1_IAC) (RE, 2026-10-23)

**Labels:** C = authored.db / next_map Kismet / config; H = stock UE3 runtime semantics.

**Other chapters:** `tools/campaign_pp_extract.py <CH>` prints the same tables.

Companions: CAMPAIGN_PPV_HEIGHTFOG_RE (fog volumes), HDR_SCENE_PATH_RE (the bloom / tonemap path), CAMPAIGN_LIGHTING_RE.

## 0. Rules (C config + H stock)
- **Chain:** `[Engine.Engine] DefaultPostProcess = PostProcess_XENON.PostProcessChain_0` (Xe-TransEngine.ini). There's no
  per-level chain override in these chapters. `SeqAct_SetPostProcessChain` exists in data (2) but neither D5 nor A1 uses it.
  - A1's base WorldInfo sets `SemiTransparentPostProcess = ENV_AllShader_p.PostProcess.Hologram_PostProcess` (the hologram material
    chain).
- **Which settings apply (H, stock UE3):**
  - each frame, the **highest-Priority enabled PostProcessVolume containing the view point** (PPV_HEIGHTFOG §1: the pawn's location
    when there is one);
  - outside every volume, the **persistent level's `WorldInfo.DefaultPostProcessSettings`**.
  - UE3 has no "unbound" flag. The world default is the unbound case.
- **Transitions:** blending to the new volume's settings over each group's `*_InterpolationDuration` (Bloom / DOF / MotionBlur /
  Scene). It's **1.0 s** where unset (CDO).
- **Unset fields take the CDO:** bloom on, scale 1.0, threshold 1.0; DOF off; motion blur off; scene effect on; DOF kernel 16, max
  near / far blur 1.0, focus falloffs 2000; LUT none.

## 1. D5_OME (the Omega Supreme fight)

**World default:** **the D5 base level sets no DefaultPostProcessSettings**, so outside volumes it's the plain engine default (bloom
1.0, no DOF / MB, no LUT). The arena is covered by volume 12453 (H: its brush spans the play space).

| Volume (D5_OME_ART_m) | Priority | Start | Bloom | DOF | Motion blur | Scene (desat / highlights / midtones) | LUT | Interp |
|---|---|---|---|---|---|---|---|---|
| **PostProcessVolume_12453** | 1 | **on** | scale 0.1 | on; far falloff 35000, far blur 0.5, near 0 | 0.25 | 0.1 / 0.9 / 0.9 | **desaturation40_CLR** | 1 s (default) |
| PostProcessVolume_15759 | 2 | off | 0.1, threshold 1.2 | as 12453 | 0.25 | – / **(0.6, 0.9, 1.5)** / **(0.8, 1.0, 1.1)**: a cold blue grade | desaturation40_CLR | **10 s** (scene, bloom, MB, DOF) |
| PostProcessVolume_4638 | 1 | off | 0.2, threshold 1.2 | on; **focus distance 50**, far falloff 20000, **kernel 64**, near 0 | 0.25 | 0.1 / **(0, 0, 0)** / **(1.36, 1.25, 1.08)**: warm, crushed highlights | none | **2 s** |

**Kismet switches (C, D5_OME_ART_m Main_Sequence; SeqAct_Toggle inputs: 0 on, 1 off):**
1. **Omega reaches Critical:** `TnSeqEvent_OmegaStatus "Critical"` → RemoteEvent **OMEGA_Critical** → Delay (Duration default
   **1.0 s**) → **12453 off** → **15759 on**. The view blends to the blue grade over **10 s**.
2. **RemoteEvent `TriggerFinalPostProcess`** (or console "EndOn") → **15759 off** → **4638 on**: the warm finale grade, 2 s blend.
   - **No Kismet sender of TriggerFinalPostProcess exists.** H: a Matinee event track in the outro, or unused.
   - Console "EndOn" is debug.

**Height fog:** HeightFog_15037 and _8983 (ART) have **Density 0** (explicitly authored), so they're **invisible** (start 8000 / 9000;
light colours warm / cool). There's no PPV fog in D5.

**Fades (Matinee InterpTrackFade; 1 = black):**
- **CHECKPOINT_FADE_IN** (base): black **2 s, then fade in over 2 s** (keys 0 → 1, 2 → 1, 4 → 0). It runs at checkpoint starts.
- **CHECKPOINTS_:** black until 1.01 s, clear by 1.5 s.
- **MISSION_COMPLETE:** holds black (2 s).
- **CINE_LevelIntro:** fades from black over 0.5–1.38 s.
- **CINE_Outro:** fade to black 9.37 → 14.21 s.

**Per checkpoint:** volume 12453 is active from every start. The Critical blue grade appears only after Omega's Critical status in
the fight. **A checkpoint restore reloads the levels**, so the volumes return to their authored on / off. H: after a restart past
Critical, the grade comes back only if the status event fires again.

## 2. A1_IAC (Iacon)

**World default** (A1_IAC_Base_m, outside every volume):
- bloom **0.35**;
- DOF on: far falloff **200000**, far blur 0.25, near 0;
- **motion blur on** (amount CDO);
- desaturation **0.2**;
- **LUT desaturation40_CLR**.

**Volumes** (the chapter's look is uniform: bloom 0.1, DOF on with near 0, desaturation 0.2, desaturation40_CLR, unless noted):

| Level | Volume | Prio | Notable differences |
|---|---|---|---|
| **Elevator_Art (the level start, intro lift)** | 16321 / 3189 (0), 1853 / 8916 (1) | 0–1 | DOF far falloff **75000**, far blur 0.4–0.5; **motion blur 1.0** on 16321 / 3189 |
| Strts0 | 8146 | 2 | far falloff 50000, far blur 0.5 |
| Strts1 | 3724 | 0 | far 200000, blur 0.4 |
| SideAlley | 5178 | 1 | far 200000, 0.25 |
| HGHWY2 / _End / _Trans | 7884 (0), 9526, 13503, 15216 (1) | | far 50000–200000 |
| HGHWY3 | 11152 | 0 | far 100000, **blur 0.65** |
| HalfPipe | 12617 (2), 1951 (3) | | far 200000 |
| Intr1 | 2056 (1), 8942 (2) | | far 25000, blur 0.4; **no desaturation** |
| Intr1_Trans | 1175 | 1 | **LUT A1_Brute_CLUT**, desat 0.1, scene interp **1.5 s** |
| Terminal1 / _Shared | 7082 (3), 10694 (2) | | slight warm / cool grades (highlights 1.0 / 0.9 / 0.8; 0.9 / 1.0 / 1.0) |
| Thumper_Art | 11723 | 2 | no desaturation |
| DecoFoyer / DecoPlaza / DecoInt / DecoEnd / Armory | 1097, 13364, 3124, 10560, 1759, 2044, 699 | 0–2 | far 15000–20000; DecoInt highlights (1.0, 0.9, 0.9) |
| DecoEnd | 14745 | 2 | **red / orange grade**: highlights (1.0, 0.8, 0.7), midtones (1.5, 0.8, 0.5), bloom 0.5, far 6000 |
| CommandCenter | 12072 (1) | | **LUT A1_CommandCtr_CLUT_RedControl** |
| CommandCenter | 14745 (5) | | the same red / orange grade as DecoEnd 14745, far 6000 |
| **Thumper1_Design (Dynamic)** | DynamicPostProcessVolume_2045 | 3 | **bloom 2.0 / threshold 0.5**, green grade (highlights 0.8 / 1.0 / 0.7, midtones 0.9 / 1.2 / 0.8), **0.5 s** blends |

**Kismet:**
- the Thumper fight (`STAGE2_Beam_MissileAttacks`): RemoteEvent **Thumper_BeamAttackPainOn** → **DynamicPPV 2045 on** (+ its
  DynamicPhysicsVolume) → collision change;
- **Thumper_BeamAttackPainOff** / the sequence input "Toggle Initial Beam Off" → **off**;
- so the green over-bloomed "beam pain" look exists only while the beam volume is on, with 0.5 s blends.
- No other Kismet post-process changes in A1.

**Height fog (C):**
- placed HeightFogs: DecoInt (Density default 5e-5, brightness 4.0), DecoPlaza (**Density 0.0002**, 0.9). Elevator, Global and
  IntroVista are Density 0 or disabled.
- PPV fogs: 31 volumes, mostly **Density 0** (invisible). Visible ones: CommandCenter_1118, Intr1_15131 and Strts1_13074 use the
  default density 5e-5; DecoEnd_11981 is 1e-4.
- The light colours and brightness per volume are in the extractor output.

**Colour scale / fades (Matinee):**
- **IntroVista_Vig** (the opening vista): **ColorScale flashes ×100 → ×1 in 0.35 s** and 1.54–2.32 s; ×50 at 5.28 s; fade from
  black over 0.5 s.
- **Elevator_Design Orbital_Cannon:** ColorScale ×29 at 1.0 s back to ×1 at 1.75 s (the cannon flash).
- **Intro_Setup** (the level start):
  - fade tracks: black → clear at 1.58–1.78 s;
  - a dip to black 7.12–7.61 s;
  - clear by 8.33 s;
  - a 3 s fade from black.
- **Cutscene fades:** Ironhide_Intro, Soundwave_Cameo, Starscream mini-boss, Thumper_FPS, CollapseBridgeMovie: short in / out
  fades of 0.15–0.75 s at their starts and ends.
- **Tube_Destruction.Teleport_Players:** black, then clear over 1.5–3.5 s.
- **Base Streaming:** black → clear 1.5–2.0 s on stream-in.

**Per checkpoint:** volumes come with their levels (CAMPAIGN_STREAMING). At **01 - Level Start** the lift's Elevator_Art volumes plus
the base default apply. Each later checkpoint gets the volumes of its visible levels. There's no checkpoint-specific post-process.

## 3. For Rendering ("Off" = the original)
- Implement **highest-priority containing volume, else the persistent WorldInfo default**, with per-group interpolation durations.
- Then:
  - **LUTs:** desaturation40_CLR almost everywhere, A1_Brute_CLUT, A1_CommandCtr_CLUT_RedControl;
  - **scene shadows / midtones / highlights / desaturation**;
  - **bloom scale / threshold**;
  - **DOF** (focus distance, falloffs, kernel 16 or 64, max blurs; near blur is 0 nearly everywhere);
  - **motion blur amount** (0.25 typical; 1.0 on A1's lift).
- **Kismet-toggled volumes** (D5 Critical 10 s blue grade; A1 Thumper beam) must honour `bEnabled` toggles.
- **Matinee Fade / ColorScale tracks** are full-screen multipliers on the final image.
- **"Realistic" deviations should be measured against these values.**
