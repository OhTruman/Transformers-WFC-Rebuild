# Next playtest (Milestone 07): what only a person can judge

Play the Integration **Release** exe from where it was built (`<tree>\build\release\bin\wfc_rebuild.exe`), at your
monitor's native resolution and refresh, with keyboard / mouse and an Xbox controller.

The gate (`M07-GATE.md`) has already rejected black, blank, untextured and missing-world frames, soft locks and wrong
selections. What it cannot tell is whether things look and feel like War for Cybertron. Mark each line **OK / WRONG /
UNSURE**, with a note; a screenshot or recording helps for every WRONG.

| # | area | look for | the gate measured (so you don't have to) |
|---|---|---|---|
| 1 | **Title animation** | the Cybertron orbit / camera, fireworks, battle vignette: smooth, correct timing, nothing malformed or popping | scene region textured, no giant untextured / noise surfaces, title stable over 60 s |
| 2 | **Menu backgrounds** | party / game lobby and the character-customization scene look like the shipped game; nothing grey, stretched or missing | not black, no blank slab, no stale overlay |
| 3 | **Character preview framing** | Create a Character / Choose Character: the model is in frame, lit, posed (Cust_Idle), the right size | model loaded for the preview (yes / no) |
| 4 | **Selected character** | pick each class; the robot **and** the vehicle you play are that character (not Optimus); right faction colours; the expected weapons | UI = Gameplay = loaded body = vehicle body = class; OPTIMUS FALLBACK flagged |
| 5 | **Robot feel** | walk, strafe, turn, jump, melee, fire while moving: weight, camera, animation smoothness at 144 / 240 Hz | no displacement spikes, frame pacing numbers |
| 6 | **Vehicle feel** | each vehicle class: handling, hover height, collisions, camera | hover / boost speeds vs RE, no pass-through on the probed paths |
| 7 | **Boost / nitro** | strength, duration, cooldown, sound and FX | boost / nitro values vs RE |
| 8 | **Transformation** | robot ↔ vehicle on flat ground, slopes, mid-boost and mid-air: snap, camera, sound, no falling through | no fall below the floor / KillZ on every map |
| 9 | **Map visuals** (every map you can pick) | architecture complete, lighting / fog / sky right, no floating objects, no black holes or flat grey surfaces | world coverage per map through the lobby, draws vs direct boot, dark-map list (Gorge / Rust / Remnant) |
| 10 | **Collision oddities** | invisible walls, getting stuck, walking through solid things, ramps that stop the vehicle | per-map stuck / under-map / hard-stop lists with locations (`maps/MAPS.md`) |
| 11 | **HUD** | health / overshield / ammo widgets size and position at your resolution; kill feed; score; clock; respawn timer | HUD drawn, widget scales 720p → 1080p, nothing over gameplay |
| 12 | **Audio** | intro sync, music by screen and match state, announcer, weapon / vehicle / transform sounds, map ambience; nothing doubled after a second match | cue sequence, no doubled cues, audio released after every match |
| 13 | **Game modes** | each mode you can start: objectives visible and readable, scoring and the end make sense, results screen | setup / timer / score / respawn / results / lobby return per mode |
| 14 | **Movies** | intro and Extras movies: aspect right at your resolution, no menu visible beside the picture | content box 16:9 and black bars at 16:9 / 4:3 / 16:10 windows |
| 15 | **Anything that does not look like shipped WFC** | free play | — |

Stability: note any display-driver reset ("display driver stopped responding") with the time. The gate records Windows
Display 4101 events per run, but only a single renderer on the machine can be attributed.
