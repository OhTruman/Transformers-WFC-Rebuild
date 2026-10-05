# Systems M08c audio handoff: playtest fixes (agents/systems)

Three human-playtest issues. All Systems-side code is on `agents/systems`; Integration needs to wire the per-form vehicle signals (item 3).

## 1. Extras → Movies → back: frontend audio gone

There were two causes, both fixed in Systems. No Frontend change is needed.

**Cause a: the movie mute stuck on.**
- `startMovieAudio` raised the movie mixer preset (CINE_MUTE_FOR_BINK), but only the frontend's `setMoviePlaying(false)` lowered it.
- The frontend never sends that for a GFx script movie (Extras): its flag covers the intro chain and the loading screens.
- Result: the mute stayed on until a later loading screen toggled it.
- **Fix:** the preset is held while the caller's flag *or* a movie sound is up. Movie end, skip and back all release it. The intro chain still holds it between its movies.

**Cause b: long sounds dropped from management.**
- SoundCues retired every one-shot instance 10 s after its last event, even while its voice was still sounding.
- The 380 s title music therefore became an unmanaged voice 10 s in. The mute, stops, fades and the "same track: no restart" rule no longer reached it.
- **Fix:** the 10 s bound now applies only to backends that cannot report voices.

**Verified on the real device** (`tools/systems/movie_audio_probe.cpp`):
- Extras cases tested: natural end, skip, back, and consecutive movies.
- After each: the same music instance comes back, nothing is duplicated, and streams and voices return to 0.
- The front-end reverb tail decays under the movie in about 2 s.
- Then lobby → match → return → repeated intro chains: all OK.

## 2. One French announcer line at match start

**Cause** (RE TARGETED_PASS4 §C, verified):
- Dialogue waves exist only in `<Map>_LOC_int` / `_LOC_FRA` twin packages, with identical object paths. The engine loads the GLanguage twin.
- The merged extraction kept one twin per path, arbitrarily. The TDM start line's waves came from `_LOC_FRA`: the merged file is byte-identical to the FRA twin.
- Later lines happened to resolve to int rows.

**Fix:**
- AssetTools now extracts both twins to `content/_LOC/<int|FRA>/<group>/<name>.wav` (d71cd07).
- Systems tags each localized wave with its merged copy's twin, then plays the GLanguage twin (`WFC_LANGUAGE`, default INT → `int`).
- A wave with no twin for the selected language is not played (logged once); another language is never substituted.
- All 261 match/announcer waves resolve to `_LOC/int`.
- 136 French-owned campaign character dialogue waves have no int twin, so they stay silent (AssetTools data).

## 3. Vehicle audio incomplete

**Audit:**
- Every form clones its blueprint's own HmPlayerVehicleAudioComponent (`TnVehicleForm.Initialize`). Systems already uses each chassis's component.
- Missing in Systems, now ported from HmVehicleAudioComponent:
  - the **SpeedSound** loop (started on attach, speed parameter, stopped on detach: Bumblebee / Starscream `*_BOOST_BYS`);
  - the **TireTread** loop;
  - **BoosterSound as a loop** with **StopBoosterSound** and **BoosterAmount**;
  - Ascend-stop, Descend / Descend-stop, Roll and OneEighty events;
  - Enter / Exit.
- Detached now stops the speed, tread and squeal loops with fade 0 (script).
- **Optimus is unchanged:** the A/B probe shows the same 57 starts; only the three exit stops change, to fade 0.

**Integration / Gameplay: feed `VehicleAudio::Input` per form, from the form state that the original form class uses:**

| Form (class) | Signal → field |
|---|---|
| Car / Scout (TnCarForm), Truck / Leader (TnTruckForm) | Hovering: `loadState` from stick, `onGround` = hover sim. Driving (boost): `boosting`, `onGround` = car, `wheelSlip` = SlipAngle. Hovering.DoDash → `booster`. Hovering.UpdateFx → `boosterAmount` = largest thruster contribution. Jump and Driving.UpdateRolling → `ascend`. Truck only: StartNitro → `nitro`, ram → `World::notifyRamHit` |
| Tank (TnTankForm) | `loadState` / `onGround` always. Tank-sim boosting → `boosting`. Jump → `ascend`. Special move → `oneEighty` |
| Plane / Jet (TnPlaneForm) | **Hovering:** PlayHoverFx / StopHoverFx → `booster` / `boosterStop`; UpdateFx → `boosterAmount`; UpdateDashing up / down → `ascend` / `ascendStop`, `descend` / `descendStop`; UpdateRolling → `roll`. **Flying:** PlayBoostFx / StopBoostFx → `boosting`; UpdateRolling → `roll` |

Today Integration feeds only the truck-shaped subset (`booster` from dash, `ascend`, `nitro`, `boosting`). The jet hover / ascend / descend / roll sounds and the tank 180 sound need those fields.

**Not ported:** CustomLoopingSound (ram alert, turret rotate, overdrive). No TransGame script calls PlayCustomLoopingSound.

## Not in this pass (noted)

- **Profile volume sliders** (Experimental P1-3): FX / Dialogue / Music are saved but not applied. A Systems volume entry point is still to come; the movie FX slider already exists (`setMovieFxSlider`).
- **End-of-match / KOTH announcer rules** (RE MP sweep §S4/§S5): `TnVersusGameOverMessage` (win / forfeit / tie / FFA) and the KOTH 3 s start suppression are recovered. MatchAudio still to be updated.
- **`BL_LVL_HUD_INTERFACE.FRONTEND_WHSH_REVEAL`** has no wave (AssetTools data gap).
