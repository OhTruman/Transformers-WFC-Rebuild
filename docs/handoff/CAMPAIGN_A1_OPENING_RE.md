# A1 opening: LevelBegin → intro lift → control (RE, 2026-10-23; blocking answer)

**Labels:** C = A1 next_map Kismet / authored.db.

**Source:** A1_IAC_Elevator_Design_m `Main_Sequence.Intro_Setup` and its Dialogue / COMMAND_CENTER sequences.

## Likely cause of our 60 s black (C)
- `SeqAct_Switch_11696` ("make sure all players in world and gameplay has started", LinkCount 2) is fired **twice**:
  1. by **PreGameplaySetup_8905** → Link 1 (nothing);
  2. then by **LevelBegin** (from GameplayStarted_7189, bound to Checkpoint_14845) → **Link 2 → StartIntroLift + Establishing_Shots**.
- That relies on the Switch **advancing**. **`Engine.Default__SeqAct_Switch` has IncrementAmount 1 and Indices [1]**
  (CAMPAIGN_KISMET_WFC_CLASSES §44: fire Link i, then i += IncrementAmount).
- **Our Kismet.cpp defaults IncrementAmount to 0** when it isn't authored (`asInt(bIncrement ? 1 : 0)`). So the Switch fires Link 1
  forever, the establishing shot never starts, and the screen stays black. The hold-black matinee below is never released.
- **Fix:** default IncrementAmount = 1; Index starts at 1.

## The sequence (C)
1. **PreGameplaySetup_8905** (bFireForAllCheckpoints, MaxTriggerCount 0):
   - → Switch (Link 1);
   - → Delay 0.1 s → Gate_3903 → **ToggleCinematicMode_13909 Enable** on the player. CDO: **HUD hidden, movement / turning / input
     disabled**, incidental dialog off; this one has `bHidePlayer` false;
   - → Delay 0.1 s → Gate_7210 → **Interp_7326 "hold black"** (InterpData_424, 0.5 s, a Fade track at 1.0 throughout). Its event
     **"Pause" at 0.25 s** feeds its own Pause input, so **it holds full black until stopped**.
   - At the other checkpoints, RemoteEvents GameplayStarted_OrbitalGun_Checkpoint / _PreFirstPlazaFight_Checkpoint **close** both gates
     (input 2) before the 0.1 s delays fire, and destroy the establishing-shot actors.
2. **PreGameplaySetup_475** (bound to CP 14845) → CLIENT_LevelBegin → **Interp_14968** (client-side, 1.79 s): black until **1.58 s**,
   clear at **1.78 s**.
3. **GameplayStarted_7189** → LevelBegin → Switch **Link 2** → **StartIntroLift** (MoveToObjectives' toggle) + **Establishing_Shots**.
4. **Establishing_Shots** → ToggleCinematicMode_9325 Enable, then:
   - **Interp_2297, the establishing shot (11.99 s):**
     - **director cuts camera1 (0 s) → camera2 (7.62 s)**;
     - **Fade:** black at 0 → **clear by 1.49 s**; a dip to black 7.12 → 7.61 → clear 8.33; **to black 11.5 → 11.99**;
     - its event **"stopFade" at 1.69 s** → RemoteEvent stopFade → **Interp_7326 Stop**, which releases the hold-black;
     - other events: Cam_Shake 2.15 / 2.52, Destroy_Jets 4.45, music stings 2.01 / 3.56, LIFT_AUDIO 7.37, RUMBLE 9.48;
   - + START_INTRO_ESTABLISHING_SHOT / START_AMB_FIRE.
5. **Interp_2297 Completed** → **Interp_15149, the lift ride (21.22 s)**: groups ElevatorPlatform, **Intro_Camera**, Optimus /
   Ratchet / Bumblebee, Light.
   - **Director: Intro_Camera at 0 s → DirGroup at 0.15 s** (the director group itself, i.e. back to the player's view);
   - **Fade 1 → 0 over 3 s** (event stopFade at 3.0);
   - Camera_End 5.0; STOP_AMB_FIRE 16.0;
   - **End at 19.72 s**.
   - Also on Completed: MX_INTRO_ELEVATOR_DOWN, Dropship_Crash, STOP_INTRO_ESTABLISHING_SHOT, the lift sounds, and destroy the
     establishing-shot props.
6. **Interp_15149 "End"** (19.72 s) → StartIntroElevatorVO → **SeqAct_Conversation_3189 "Elevator Ride to Hell"** → its finish →
   OpenDoor → TnSeqAct_TransformWorldProp_5195 (the door) **Finished** → StartJetfireTVScreenVO → **Delay 0.75 s →
   ToggleCinematicMode_13909 Disable: control returns** → Delay 1.25 s → DisplayHealthInfo.
   - Also at End: TransformWorldProp_14256, Toggle_9245 off, the lift sound off, Delay 2.8 s → DoorGlowOn material param.

**Totals:** about 12 s of establishing shot plus about 20 s of lift ride (in the player view, inputs locked), then the conversation,
the door, then **+0.75 s** to control. Roughly 35 s plus the VO length.

**There is no SetPostProcess, letterbox op or PPV toggle in this chain.** The lift's Elevator_Art PPVs apply (DOF far 75000, motion
blur 1.0; CAMPAIGN_POSTPROCESS §2). ColorScale is used only by the separate Orbital_Cannon matinee (×29 flash).
