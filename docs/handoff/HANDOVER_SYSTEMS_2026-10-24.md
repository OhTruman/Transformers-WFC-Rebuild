# Systems hand-over, 2026-10-24

What the Systems lane did for the user's playtest 2 (USER_PLAYTEST_2026-10-23b.md) and the everything-build. Each item is
**fixed** (Systems commit -> integration merge where known) or **open**. All tests are headless or two-process loopback runs; no
item has had a windowed / real-controller / real-microphone run yet, unless it says so.

## Playtest 2 items

| Item | Status | Commit (merge) | Notes |
|---|---|---|---|
| 7 Voice channel switch on the fly | fixed | SYSTEMS_QC_VOICEKEY ca8ff641 (merged) | Keyboard N (Settings > Audio "Voice Channel Key", rebindable), pad D-pad Right (unbound in both original MP schemes); Team -> All -> Proximity -> Radio + Proximity, saved, HUD line; only in a match, no menu / scoreboard. PTT stays V (the user's pick; V is also jet Hover Down, the PttConflict hint shows it). |
| Audio / Voice settings audit | fixed | SYSTEMS_QC_VOICE efa38d7a (merged) | OK as found: FX / Dialogue / Music Volume (device-wide, survive map change), Language, Voice Chat mode, PTT key, Voice Volume. Fixed: Proximity Volume (did nothing), All Radio (was radio + proximity), Input Device (listed only Default), Mic Test (always 0), Voice Chat turned on mid-match (stayed silent). |
| Subtitles (no display existed) | fixed | SYSTEMS_QC_SUBTITLES bf51d6e2 (c51e9a3f) + Frontend draw 7f0efe32 | World::currentSubtitle(): the campaign line on screen now, dialogue language, the original's per-line show flag. Timing checked on A1: 169 lines, 0 wrong. Font packaging: build_slim ebfcb915. |
| 6 Menus at 60 FPS | not Systems | Rendering RENDERING_MENU_CAP2 (74e41849) | No Systems share. |
| Pad -> match actions (nothing fired on a controller) | fixed | SYSTEMS_QC_PADACTIONS be7508b6 (1e4b6130) | The original's authored MP layout; X = Interact + Reload (RE: tap = interact else reload, hold = revive / pickup); both profile remaps (SwitchRightThumbstickAndB, UseAlternateControlScheme); seat 2's pad too. The tap's interact-else-reload context: Gameplay 9c94b50d (fixed). |
| Seat routing (controller ownership) | fixed | SYSTEMS_QC_SEATS 24966b02 (d95ebc91) | A lone pad on any XInput slot drives the menus; the only pad never becomes seat 2; a mirrored virtual pad is refused. Not the "Scout cannot move" cause (that was Gameplay's wire seat). |

## Split screen (the user's request for this build)

| Part | Status | Commit (merge) | Notes |
|---|---|---|---|
| Split input + audio | fixed | SYSTEMS_SPLITSCREEN_1 b4c35e7d (5d6454b9) | Seat 2 = the next other pad pressing Start; nearest-listener audio for two views. |
| Two locals online | fixed (net side) | SYSTEMS_TWO_LOCALS_ONLINE 85c4cd06 | A peer owns a second lockstep seat (commands in a CMDS trailer); UdpSession setSecondLocal (0x57) + owner lists in the launch. Two processes: 2,401 ticks status 0, incl. 10 % loss; the real session path too. Frontend lobby hook (joinSeat2 online -> setSecondLocal): 070f6705 (fixed). |

## Load times (performance pass, lossless gates)

Headless null render, `WFC_LOADSTAGES` (SYSTEMS_LOADSTAGES 2ab92af1, merged a7cde068).

| Change | Commit (merge) | Gate | Streets | Remnant | D1 |
|---|---|---|---|---|---|
| Before | - | - | 2.21 s | 2.45 s | 1.22 s |
| WIC PNG decode + worker threads | SYSTEMS_TEXTURE_DECODER 7cec56b8 (ee98ab39) | WFC_IMAGECMPTEST: 60,458 / 60,458 shipped PNGs byte-identical (base + campaign + DLC; HD pack is DDS) | 1.83-1.92 s | 1.96-2.07 s | 1.15 s |
| Parallel animation files | SYSTEMS_ANIM_PARALLEL 96472657 (4d338915) | WFC_ANIMHASH: per-file data hash set equal serial vs parallel | 1.87 s | 2.03 s | 1.10 s |

Open: the windowed load row (the renderer's GPU part, not in headless) joins the slot before the final gate. Next lossless lever
after the build: a binary animation cache (Streets' 15 animation files = 95 MB, 0.44 s; Integration's decision: after the build).

## DLC level audio

| Map | Status | Commit |
|---|---|---|
| District, Fortress, Havoc, Horizon | fixed | 22866ce3, a70d9779, 54c419f6 (District regenerated from its redone art shard) |
| Pulse, Metropolis, Sector | fixed | 7296f605 |
| Tranquillity | fixed: 12 -> 50 Kismet ops, pools 0 -> 15, zones 21 -> 27 | 54c419f6 (from AssetTools' re-extracted package: the STFS boundary bug, not shipped damage) |
| Static | fixed: 118 ops | 54c419f6 |
| DLC wave lookup (content roots) | fixed | SYSTEMS_DLC_AUDIO_1 873a3392 + _2 fcdd7e96 |

Every DLC map loads with 0 localized waves unplayed.

## Packaging (build_slim, by file)

| Rule | Commit |
|---|---|
| Campaign chapters + manifests, `_` shared folders, source dedup, lens flares | 30312683, f879c525, b8940af3, 123d3e7f |
| DLC content / maps / render (DLC root moved) | 8b3027e6, 78f429a6 |
| `_ui/fonts` whole (SubtitleFont) | ebfcb915 |
| Head data folders recursive (data/frontend/previews) | 74d56e27 (3552cf3d) |

The everything-build refresh is staged: `SPLIT_HD=1 NOPROMOTE=1 bash work/refresh.sh <sha> "F:/Transformers
Rebuild/AssetTools/out/hd/push_full_snap_20261016_b0-31/pack" D`, then `work/verify_everything.ps1 -Head <sha>` (W: base, HD off;
X: base + HD pack). All 9 DLC map folders have data.
