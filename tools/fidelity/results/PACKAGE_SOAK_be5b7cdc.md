# Package rows - playtest_slim_be5b7cdc (new, 27.83 GB, Systems 07:0x) (2026-10-23 07:11)

The package exe (be5b7cdc) headless from the package's own paths: mapped to M: (my letter; W / X are Systems'), Play.bat's env (WFC_ASSETS / CONTENT / RENDER_DATA / MANIFESTS / DDS_ROOT / FRONTEND_DATA / CAMPAIGN_ROOT on M:) + WFC_HD_ROOT = the package's AssetTools/out/hd + WFC_NULLRENDER / MUTE / NOINTERACT; cwd and logs in Experimental's work/pkgsoak (the package is read only); an exe-present check before every run. Data loaded from M: (no F:/Transformers Rebuild path in any log). (The first try on 07:02 was cut by a W: mapping collision - harness only, discarded.)

| Row | Result |
|---|---|
| Stability: 10 maps x TDM x 300 s x seeds 7 / 1234 (20 runs) | 20 / 20 exit 0, all end at 300 s, no crash / assert / hang. **64 ERROR lines on 3 maps, every one a missing file in the package** (below) |
| Escalation BrokenHope + Remnant x seeds 7 / 1234 (default: nav extras gated off) | 4 / 4 exit 0; weapon kills only (4-6 per run), 0 suicides, the player killed in wave 1 (as the dev head) |

## Package gaps (present in the dev tree, absent from the package)

| Missing file (package-relative) | Who asks for it | Error lines per run |
|---|---|---|
| AssetTools/manifests/maps/transforming_actors/IAC_InteriorDoor_SKEL.glb | Rust (1), BrokenHope (13) | skinned glb: open failed |
| AssetTools/manifests/maps/transforming_actors/IAC_TubeDoor_SKEL.glb | Rust (1) | skinned glb: open failed |
| AssetTools/manifests/maps/transforming_actors/ORB_InteriorDoor_SKEL.glb | Remnant (12) | skinned glb: open failed |
| AssetTools/manifests/maps/transforming_actors/NEU_BridgeStraight_SKEL.glb | BrokenHope (5) | skinned glb: open failed |
| AssetTools/out/campaign_anims/TR_SoldierTankDec_ROBO_p/Shared_ROBO_ANIM.anim.gltf | Escalation horde (Soldier tank, both ESC maps) | anim gltf: open failed |

The package has no AssetTools/manifests/maps/transforming_actors/ folder at all (dev: 7 files) and no out/campaign_anims/. In play these doors / the bridge would load without their skeletal mesh (static or invisible) and the horde Soldier tank without its shared robot anims - visible in a windowed run. Logs work/pkgsoak/stab/, esc/.

## Package v3 (playtest_slim_be5b7cdc_v3, Systems: + transforming_actors + campaign_anims) - 2026-10-23 07:13-07:16, M:

| Row | Result |
|---|---|
| Stability 10 maps x TDM x 300 s x 2 seeds | **20 / 20 exit 0 at 300 s, 0 ERROR lines** (was 64), no crash |
| Escalation BrokenHope + Remnant x 2 seeds | 4 / 4 exit 0; weapon kills only (5 / 7 / 43 / 5), 0 suicides; BrokenHope seed 7 reached wave 3 (43 kills); **2 ERROR lines** (data): 'chassis ESC_CQCSpecialist_Decepticon / ESC_Soldier_Rocket_Decepticon: energon_default missing in its character.json export (vehicle FX colour unset)' |

**PASS for the package** except the two ESC chassis exports (AssetTools / Gameplay data). M: unmapped after the run.
