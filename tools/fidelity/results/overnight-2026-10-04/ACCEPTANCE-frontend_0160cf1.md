# Playtest acceptance (Release)

exe `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fe5\build-release\bin\wfc_rebuild.exe`

**PASS 10 / FAIL 2 / KNOWN 0 / INFO 1 / SKIP 1 / HUMAN 4 / UNKNOWN 0 / WAITING 0 / PARTIAL 0**

| status | check | owner | evidence |
|---|---|---|---|
| PASS | intro.video | Frontend | intro movies with decoded frames 4/4; frame at 5 s near-black 82% |
| PASS | intro.audio | Frontend/Systems | intro movies whose audio started on the Systems device (movie.audioStart with a handle): 4/4; decode failures 0. Audible balance / sync is a human check. Human playtest: the intro has no sound. Frontend documents movie audio as PARTIAL (10 mono FLAC tracks, layout unidentified; MoviesToAlwaysPlaySound lists the logos) - owner Frontend for decode, Systems for playback on the one audio device, AssetTools / RE for the track layout |
| HUMAN | background.title | Frontend/Rendering/AssetTools | title: near-black 25%, flat untextured grey 4%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| HUMAN | background.main_menu | Frontend/Rendering/AssetTools | main_menu: near-black 22%, flat untextured grey 4%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| FAIL | background.party_lobby | Frontend/Rendering/AssetTools | party_lobby: near-black 0%, flat untextured grey 34%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| FAIL | background.host_options | Frontend/Rendering/AssetTools | host_options: near-black 0%, flat untextured grey 34%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| HUMAN | background.frontend_after_return | Frontend/Rendering/AssetTools | frontend_after_return: near-black 84%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m again after travel back (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| INFO | title.background_summary | AssetTools/Frontend/Rendering | title frame near-black 25%, main menu 22%. The original shows the UI_FrontEnd_m 3D scene (orbit camera, Cybertron, fireworks; RE 1.2 Kismet). Documented gap: UI_FrontEnd_m not exported (AssetTools) / not rendered (Frontend / Rendering) |
| PASS | title.start_advances | Frontend | Start (keyboard F3) -> ShowDeviceSelectionUI + main menu: True; A (Enter) before it advanced: False. CONFIRMED original: the title reacts to buttonStart only (FrontEnd_GFX inputEvent), so Enter doing nothing is original; keyboard Start is F3 in the rebuild's PROVISIONAL key map - usability decision for Frontend, not a fidelity defect |
| HUMAN | controller | Frontend | XInput is mapped (Win32Window: A/B/X/Y/Start/Back/D-pad/shoulders/triggers/thumbs -> UiKey) but pad input cannot be injected safely; play the whole route with a controller |
| PASS | nav.back_mode_list | Frontend | B in the mode list returns to the party lobby root: frame difference to the root 0.89 luma (focus highlight may differ) |
| PASS | nav.back_host_options | Frontend | B in the host options returns to the mode list: difference 0.5 |
| PASS | nav.back_twice | Frontend | B twice from the host options returns to the party root: difference 1.36 |
| PASS | nav.back_party_root | Frontend | B at the party root -> Game.QuitToMainMenu 1 -> travel to UI_FrontEnd_m 1 (BLK A3) |
| PASS | nav.back_sound | Frontend | BUTTON_BACK requested 5 times for 4 B presses (BLK A4) |
| PASS | nav.flow_completed | Frontend | navigation script completed |
| SKIP | loading.updates_during_load | Experimental | window capture unavailable (control frame after the load uniform or missing); 20 captures in the load window. Run with the window visible on an unlocked desktop |
| PASS | loading.overlay_gone | Frontend/Integration | in-game frame 2 s after InGame: near-black 3%, mean luma 18.1 (the loading underlay was composited over the match until Integration 7a11ce2) |
