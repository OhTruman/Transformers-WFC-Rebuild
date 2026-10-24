# Smart AI: design and effort plan (Gameplay lane)

User decision: "I want intelligent AI not bots that are fixed to a path." New AI is the default. The current faithful AI stays as a "Classic AI" setting, byte-identical.

## 0. Ground rules

- **Two AIs, one switch.** A per-match `BotAi { Smart, Classic }` comes from the lobby setting (Frontend owns the UI) or `WFC_BOTAI=classic|smart`; the default is Smart. The dispatch points are botThink, botPathUpkeep, botSteer and botAimAndFire in tickBots. Classic calls today's functions unchanged. Smart lives in new files (`BotSmart*.cpp`, `BotTactics.*`, `BotSquad.*`) and reuses shared pieces (nav, path search, fire, ability calls, transform gating) through the same World APIs. No edits inside Classic's decision code.
- **Classic is byte-identical.** Check it with a lockstep `WFC_SIMHASH` comparison: the build before Smart against the build after, with Classic selected, in 3 seeds × TDM / CTF, at 10 v 10 and 64p. Every step hash must be equal. This runs as a gate on every Smart commit.
- **Determinism stays.** All randomness comes from each bot's seeded rng (simRand). There is no wall-clock input. Shared state (the squad director, the team blackboard and the threat field) is written only in serial passes. The parallel per-bot tasks write only their own brain. Iteration order is fixed. DETERMINISMTEST, plus a 64p lockstep pair, gates Smart.
- **No cheating.** Bots act only on what they perceived: sight with LOS, hearing, team call-outs with a delay, and their own damage feedback. They never read hidden enemy positions directly.
- **Budget.** At most 0.03 ms per bot per step on average, on worker threads: about 2 ms of CPU per step for 64 bots, spread over 3–6 workers, so under 0.6 ms of wall time. p99 stays under 6 ms. Thinking is staggered (at most 1/8 of bots think per step). Tactical queries have a per-think cap and a global per-step budget (a FIFO like the path search). The steady state allocates nothing. The 32 v 32 fps rows must not move beyond noise.

## 1. Architecture: three tiers

| Tier | Rate | Thread | Job |
|---|---|---|---|
| Team / squad director | 2 Hz per team | serial | Forms squads, assigns roles and objectives, picks focus targets and rally points, merges call-outs into the team blackboard, updates the threat field |
| Bot brain (utility planner) | 4 Hz, 8 Hz in combat, staggered | parallel | Updates perception memory, scores options, picks an action plus its parameters (a position, a target, a route mode) |
| Execution | every step | parallel | Steering, aim tracking, fire discipline, ability / grenade / transform triggers for the current action |

**Utility planner.** Each option's score is the product of its considerations (response curves over inputs such as health, ammo, threat count, distance, cover quality, squad order and objective state). That product is multiplied by personality weights and given difficulty-scaled noise. Selection is a softmax over the scores; its temperature falls as difficulty rises. Hysteresis (a commitment bonus) prevents dithering.

Options:
- **Engage:** fight from the current or a nearby firing position.
- **Take cover / peek.**
- **Flank:** route around the threat's facing.
- **Hunt:** investigate a last-known position or a sound.
- **Retreat:** to heal, or to a pickup.
- **Objective:** capture, hold, escort, intercept, plant or defuse, depending on the mode.
- **Support:** repair, buff, deploy a sentry or barrier.
- **Reposition:** high ground, a chokepoint hold.
- **Travel:** vehicle form.
- **Regroup.**

## 2. Perception and memory

- **Sight.** Keep the current FOV, range and LOS checks, under a per-think ray budget. Per-enemy memory holds the last seen position and velocity, time, confidence, the last known weapon / form / HP band, and whether that enemy is engaging this bot. Confidence decays, and the bot predicts the enemy's position from its last velocity for a short time.
- **Hearing.** A per-step event list (serial append, read in parallel) covers gunfire, explosions, transforms, footsteps / engines (by speed) and pickups. Each event has a loudness radius (weapon data, form). Hearing gives a position with uncertainty proportional to distance, plus a team-relevance check. No wall dampening at first; walls attenuate later if measured worth it.
- **Damage awareness.** The hit direction from damage history creates a threat bearing even with no sight. That turns the bot, triggers cover-seeking, or tells it a flanker is near.
- **Team call-outs.** A blackboard per team holds sightings that bots post. Others read them after a 0.4–1.5 s delay that depends on difficulty, with the same decay. Squads also share their focus target.

## 3. Tactical analysis

- **Tactical points, precomputed per map at nav load.** Candidate points are sampled on nav cells: cell centroids, plus edge points next to walls and props. Each point stores:
  - 16-direction blocking at 1.0 m and 2.6 m, meaning full cover or a peek-over;
  - height relative to its 25 m neighbourhood (high ground);
  - openness;
  - a chokepoint score (from nav-graph cut size).
  At about 4–6k points × 32 rays, that is ~150k rays: about 0.1–0.2 s at map load with the current segmentAnyHit. The result can be cached next to bot_nav.json if it shows in load times.
- **Threat field, per team at 2 Hz.** Each nav cell's exposure is the summed threat strength of known enemies (memory plus call-outs) that have probable LOS to it. LOS is approximated from tactical-point cover directions against the threat's bearing, and confirmed with a few real rays for the top candidates only. Only cells within ~60 m of threats are updated (incremental).
- **Queries.** A cover position is chosen within radius R, scored by:
  - protection from the current threat set;
  - a firing line to the target (peek);
  - path distance;
  - not being claimed by a teammate (claims live on the blackboard);
  - a height bonus;
  - personality range preference.
  The same machinery yields flank positions (bearing to the target over 70° from its facing, in cover) and hold points (chokepoints facing the expected approach).

## 4. Routes: not fixed to a path

- **Threat-weighted A\*:** cost = length × (1 + w_exposure · exposure(cell) + w_front · inFrontCone(cell)), using the same incremental search and FIFO budget. Smart may take 2 searches per step if the measured queue waits grow.
- **Route variety:** small per-bot seeded cost noise, plus a "different corridor" penalty on cells of a squadmate's active route. A flank action uses a large front-cone penalty.
- **Re-plan triggers:** a new threat, the current route becoming exposed, the action changing, a teammate's call-out on the route, or being stuck. Each re-plan is rate-limited.
- **Kept from Classic:** the off-mesh recovery and barrier steering, which are shared helpers.

## 5. Squads and modes

- **Squads.** Squads of 3–5 per team form at spawn proximity and are rebalanced on deaths. Roles follow the WFC classes:
  - **Leader:** point, Warcry on engagement, the Barrier on the threat axis.
  - **Scout:** flanker, harass.
  - **Soldier:** anchor or assault.
  - **Scientist:** support, Repair Ray on teammates, the Sentry at holds or flanks.
- **Coordination.** The director picks one focus target per squad: low HP, high threat, the flag or bomb carrier, or whoever is attacking a weak teammate. Squads regroup at a rally point after taking losses before pushing again; better coordination at higher difficulty. They trade (follow up on a teammate's engagement) and avoid stacking in one cover spot.
- **Modes.** These build on the existing botModeGoal / botObjectiveGoal targets:
  - **TDM / FFA:** hold power positions and high ground, trade kills, hunt from sounds. FFA has no squads; personalities dominate.
  - **CTF:** carrier escort with an escort formation, interceptors, base defenders with sentries and barriers at the flag, and a retrieve-the-flag priority.
  - **KOTH / DOM:** a capture / hold / contest split driven by score state and time; Domination point rotations.
  - **EXT (bomb):** planter plus escorts, defenders on chokepoints, defuse priority.

## 6. Forms, weapons, abilities, pickups

- **Form.** Vehicle for travel over ~40 m with no threat nearby, for chasing a fleeing target, or for escaping at low HP. Robot for fights and holds. The existing transform gating and room checks are kept.
- **Weapons.** Chosen by range and target type from AiWeaponData. The bot reloads while in cover, switches rather than reloading under fire when the switch is faster, and fires in bursts at range for spread.
- **Grenades.** Thrown at two or more enemies within the blast radius, or at an enemy behind cover (last known position), using an arc solve with ballistic checks against collision. Flashbangs before a push; heal grenades for a clustered hurt squad.
- **Abilities.** The barrier goes between the squad and the threat axis at chokepoints, never boxing in the bot itself (a check against walls, from the Debris lesson). The sentry goes at a defended objective or flank route. Shockwave when enemies are close, Warcry when the squad engages, the Repair Ray on teammates below 60%. Killstreaks when they pay off: P.O.K.E. in close fights, turrets at holds.
- **Retreat and pickups.** A retreat threshold comes from personality and difficulty. The bot breaks LOS, heads to known health / ammo / overshield pickups (with remembered respawn timers), then rejoins its squad.

## 7. Human-like aim

- **Reaction time** from first sight to first shot: Easy 550 ms, Normal 350 ms, Hard 220 ms. Add ±30% from personality; it is reset when the target leaves sight for more than 1 s.
- **Tracking:** the aim point follows the perceived position through a second-order lag that scales with difficulty. Error grows with the target's lateral speed and distance, and falls while the bot keeps tracking (settling).
- **Leading:** projectile weapons lead the target, with prediction-quality noise.
- **Sources:** the bot aims at the perceived position, never the true one. That matters right after the target leaves cover or teleports.

## 8. Personalities

Each bot gets a profile seeded from its identity:
- aggression, caution, teamwork, patience;
- preferred range, form preference, accuracy bias, ability eagerness.

Profiles multiply the utility weights, and stay within bounds per difficulty so a Hard bot is never incompetent.

## 9. Difficulty scales decisions, not just aim

| | Easy | Normal | Hard |
|---|---|---|---|
| Perception | FOV 120°, sight 50 m | 140° / 60 m | 160° / 70 m |
| Memory decay | 3 s | 6 s | 10 s |
| Call-out delay | 1.5 s | 0.9 s | 0.4 s |
| Cover use / flanking | rare / never | often / sometimes | almost always / often |
| Squad coordination | none | focus fire | focus fire, regroup, trades |
| Utility temperature | high (more mistakes) | medium | low |
| Ability / grenade use | basic | situational | situational and combo |
| Aim | slow reaction, large error | | fast, small error |

## 10. Test plan

1. **Classic identity:** lockstep SIMHASH comparison of the pre-change build against the post-change build with Classic selected (3 seeds × 2 modes × 2 sizes). The gate is that every step is equal.
2. **Smart determinism:** DETERMINISMTEST, plus a 64p lockstep ref-vs-ref pair (Experimental's harness). The gate is that every step is equal.
3. **Strength:** a WFC_AIDUEL harness puts Smart against Classic in team matches (5 v 5, 16 v 16, 32 v 32) at equal difficulty. It runs 20 seeds per size with sides swapped, and reports win rate, K/D and objective score.
   - **Targets:** Smart wins at least 70% at equal difficulty, and the Smart ladder is monotonic (Easy < Normal < Hard).
4. **Behaviour metrics,** logged per match: the share of engaged time in cover, flank kills (attacker over 90° off the victim's facing), focus-fire share, mean reaction time, grenade hit rate, ability uses by type, retreats and heals, idle %, path-search queue waits, and environment deaths.
5. **Stuck rate:** the WFC_STUCKWATCH 64p per-map sweep (8 maps × TDM / FFA). The gate is no worse than Classic, which today has 1 or fewer per run, all in pits.
6. **Cost:** BOTTEST's AI-cost check extended to Smart; ms per bot per step, average and p99, at 10 / 32 / 64 bots. Experimental's 32 v 32 1080p / 4K fps rows must stay unchanged within noise.
7. **Mode suites:** BOTOBJ and TDMTEST pass with Smart in every mode.
8. **Playtest checklist for the user:** bots take cover, flank, push together, use abilities sensibly, retreat when hurt, and react to gunfire they hear; difficulty feels different.

## 11. Effort plan (Gameplay lane; each milestone is gated by 1, 2, 6, BOTTEST and BOTOBJ)

| M | Content | Est. agent time |
|---|---|---|
| S1 | AI switch and dispatch; the Classic identity harness; a Smart skeleton that reuses Classic execution; perception memory, hearing events, team blackboard; the metrics log | 1 day |
| S2 | Tactical-point precompute; cover / flank / hold queries; threat field; threat-weighted A* with route variety; the Engage / TakeCover / Hunt / Retreat / Travel utilities | 2–3 days |
| S3 | Aim model, personalities, difficulty tables; the WFC_AIDUEL win-rate harness | 1–2 days |
| S4 | Squad director: roles, focus fire, regroup, trades; objective play in all six modes | 2–3 days |
| S5 | Forms, weapon choice, grenades with arc solve, abilities, killstreaks, pickups and retreat | 2 days |
| S6 | Tuning loop against win rates and metrics; perf pass; the per-map stuck sweep; the playtest build | 1–2 days |

That totals about 9–13 agent-days of Gameplay-lane work, plus Frontend's lobby setting (small) and Experimental's harness runs.

Smart becomes the shipped default once S3 passes the gates: by then it beats Classic, is deterministic, within budget and no stucker than Classic. Until then the default stays Classic, so playtests are never worse.

## 12. Questions for the user

1. **Lobby setting:** "AI: Smart / Classic" in the match lobby, or in game options? Frontend's call on placement. Should it also apply to the original 5 v 5 default lobbies? (Proposed: yes, Smart everywhere by default.)
2. **Hearing through walls:** may bots hear gunfire and footsteps through walls (a human player can)? Proposed: yes for gunfire and explosions, footsteps only within 15 m.
3. **Difficulty labels:** keep the original three (Easy / Normal / Hard), or add an "Expert" tier above Hard for Smart?
