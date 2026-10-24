# Rendering's FINAL_BASE_ITEMS entries (2026-10-24, provisional numbers until Gameplay's 22+ are fixed)

All three are cut on a fresh full af4d0731 + FINAL_BASE_ITEMS 01-21 (head a46a87a8), applied in this order with `git apply`, each
diffed on top of the previous:

- **NN_a_campaign_skeletal.patch:** src/game/Campaign.h, src/game/WorldCampaignMatinee.cpp. It carries:
  - D1_GPUSKIN's game half (the matinee skeletal actors on the GPU skin path);
  - SKEL_VIS (drawn only while their unit is Visible and they are not hidden / destroyed);
  - SKEL_PRELOAD (every distinct model loaded at chapter load, shared).

  Its Campaign.h equals the head's.
- **NN_b_data_roots.patch:** new src/core/DataRoots.h, plus WorldCampaignMatinee / WorldCampaignDestructibles /
  WorldCampaignRuntime.
  - The hard-coded F: data paths become core::extractedRoot() / assetToolsOut().
  - One pawn-clip line of the head's patch belongs to Systems' clip preload, which is not in the chain yet; it joins with that item.
- **NN_c_asset_root_default.patch:** core::config::kAssetRootDefault becomes core::config::assetRootDefault(), in new
  src/core/AssetRootDefault.cpp.
  - Every user is renamed, including the base's package-6 renderer files (GLRenderer.cpp, WfcPipeline.cpp / .h), because the
    constant is gone.
  - CMakeLists.txt adds the file to wfc_frontend_tests / wfc_gfxdump, and tools/fidelity adds it to the optional list.

**Verification:** the full tree (af4d0731 + 01-21 + a + b + c; third_party dxc / ffx / agility copied in) configures and builds
wfc_rebuild.exe in Release.

**Not mine:** wfc_fidelity.exe fails to link in this chain (undefined game::MatchSettings::forMode). The base's
tools/fidelity/CMakeLists.txt (package 6) lacks the head's src/game/Match.cpp in its optional list. My entries only renamed one
constant in Rig.cpp.
- **NN_d_esc_mech_game.patch:** the DLC Escalation mechanism presentation, game half (Integration: yes, 2026-10-24).
  - Files: src/render/Renderer.h gains the setMatineePosition default no-op virtual; src/game/World.h / World.cpp add the hooks
    (svMechPresent in syncMapPresentation, svMechDrawSentries in draw, svMechEnsureModels in ensureAbilityModels); new
    src/game/WorldSurvivalMechPresent.cpp is the head a46a87a8 file.
  - On package 6's renderer the matinee positions are no-ops. The PPV / emitter toggles and the sentry turrets draw.
  - Verified: on af4d0731 + 01-21 + a + b + c + d, wfc_rebuild.exe builds.
