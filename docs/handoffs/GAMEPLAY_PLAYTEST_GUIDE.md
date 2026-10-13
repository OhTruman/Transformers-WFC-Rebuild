# Smart vs Classic bots: playtest guide

**Goal:** play the same map twice, back to back: once with Smart bots, once with Classic.
Decide whether Smart feels smarter and fair. The Smart AI milestone stays open until you've done this; automated results are not sign-off.

## Setup
- **Lobby row "Bot AI":** **Smart** (the default) or **Classic** (the original game's behaviour). It applies to every bot in the match.
- **Suggested match:** TDM, 5 v 5 (the original default), difficulty **Hard**. All automated duels ran on Hard.
- **Maps, in order:**
  1. **Molten**: the biggest Smart edge in the duels.
  2. **Streets**: compact lanes, easy to watch flanks and cover.
  3. **Gorge**: long sightlines. The edge is smaller here, so it's a fairness check.
  4. Optionally **Debris**: the smallest edge (54 %).
- **Bot overlay:** **F12** toggles a label over every bot (F9 cycles the Ray Tracing mode, F11 the debug wire boxes). It works in every build.
  - Label format: `name [Smart|Classic difficulty] goal ACTION tgt <id> hp <n>% wp <i>/<n>`.
  - ACTION is `HUNT`, `RETREAT`, `TO-COVER` or `IN-COVER` (Smart only). `STUCK` appears when a bot is stuck.
  - Lines point to each bot's target and to where its action is headed.
  - Play a round without it too: the point is how the bots *feel*.

## Keys for the graphics A/B (same build)
- **F9** cycles the Ray Tracing mode live: Off -> Faithful -> Realistic -> Off. It only works on a map that was loaded with the
  Settings row **Ray Tracing** set to Faithful or Realistic (set the row BEFORE loading the map; on a map loaded with the row
  Off, F9 only takes effect from the next map). F9 changes the renderer, not the saved setting: the Settings row keeps showing
  the value you chose, and it is not saved.
- **HD textures** (Settings row, Off by default): Off / Faithful / Enhanced. Enhanced also turns on the Modern materials. Needs a
  map reload.
- **F11** = debug wire boxes (ignore), **F12** = the bot overlay below.

## What to look for

| Behaviour | Smart should... | Classic does... | Rough rate in duels (Smart, per 8v8 match) |
|---|---|---|---|
| **Cover** | duck behind cover when 2+ enemies see it, when hurt or reloading, or in ranged fights; peek and return | no cover logic | ~59 cover moves; in cover ~8 % of fighting time |
| **Retreat / heal** | back off below 25 % health, break line of sight to regenerate a health segment, fall back to team mates when outnumbered | no retreat logic | ~57 retreats, ~20 regen breaks |
| **Flanks** | take a side route around a fight instead of the straight lane | follow fixed routes | ~17 flanks |
| **Hunting by memory and sound** | chase an enemy it lost sight of; react to gunfire / explosions through walls and footsteps within 15 m | no memory-driven hunting | ~44 hunts (~4 from sound) |
| **Squads** | move in groups of 4 behind a leader, who slows or waits when members fall behind | move individually | leader distance is loose: median ~26–38 m, often 40 m+ just after respawns |
| **Target choice** | prefer hurt enemies, spread fire across targets, hold fire when a team mate is in the line | shoot the nearest | holds ~390 shots per match for team mates in the line |
| **Aim** | **same aim as Classic** (user rule: "smarter, same aim") | the original aim | hit rate per clear shot is equal by design |

## Things that would be bugs (please note map + time)
- A bot labelled `STUCK` for more than a few seconds. Bots stranded off the walkable map for 30 s kill themselves and respawn by design, in both AIs.
- A bot in cover that never peeks or fires, or retreats forever at full health.
- Smart bots shooting through team mates, or holding fire with nobody in the way.
- Squads walking single file into one choke every life (no flanks at all).
- A bot that reacts to you through walls without any noise from you (it should only hear shots, explosions, engines, and footsteps within 15 m).

## Numbers to compare against
- With the 2026-10-11 line-of-fire fix in: Smart wins **61.5 %** of 8v8 TDM duels against Classic (104 matches, 4 maps).
  - Per map: Streets 65 %, Molten 65 %, Debris 54 %, Gorge 62 %.
  - That is inside the 60–63 % band you accepted.
- Smart's kill share is ~52 %. Smart wins by choosing better fights, not by aiming better.

## After playing
Tell Integration (or Gameplay) per map:
1. Did Smart feel smarter?
2. Was it fair?
3. Anything that looked dumb or broken (with map + time)?

Tuning stays paused until your feedback.
