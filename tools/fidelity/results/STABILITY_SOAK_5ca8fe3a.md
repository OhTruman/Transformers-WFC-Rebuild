# Stability soak - 5ca8fe3a vs ab855fe7 (2026-10-23 02:00)

Headless (WFC_NULLRENDER, lockstep, CPU only), 10 maps x TDM / CTF / KOTH (Power Struggle) / DOM (Conquest) x 300 s, 4 v 5 Smart bots (BotsFriendly=4, BotsEnemy=5), seeds 7 + 1234: 80 runs per build. Collected per run: exit code, end, ERROR / WARN lines, assert / access-violation / exception / crash lines, minidumps, a 240 s wall kill for a match that never ends. Runner work/stabsoak/run.sh; per-run table work/stabsoak/<set>/runs.csv.

| Build | Runs | Non-zero exit | Crashes / dumps / asserts | Never ended (killed) | ERROR lines | WARN lines | Ends |
|---|---|---|---|---|---|---|---|
| 5ca8fe3a (head) | 80 | 0 | 0 | 0 | 0 | 562 | 79 at 300 s, 1 score win (seed 1234 Seed KOTH 400-2 at t 268.4) |
| ab855fe7 (checkpoint) | 80 | 0 | 0 | 0 | 0 | 559 | 80 at 300 s |

**PASS: nothing to replay.** No crash, assert, dump, hang or non-zero exit on either build; 0 ERROR lines. The WARN lines are one class on both builds - 'weapon fx: <muzzle / impact / tracer> template ... was not spawned by the particle runtime', i.e. the null render has no particle runtime (9 per run on the 8 versus maps, 0 on Remnant / BrokenHope where nobody fires); same templates, same counts within 1 % between builds.

- Modes really ran: launch lines 'match: launched <map> CTF / KOTH / DOM'; CTF flag captures (TnFlagMessage(captured)), KOTH / DOM objective scores (e.g. Rust seed 7: 407 KOTH and 189 DOM score lines).
- **Remnant / BrokenHope are Escalation-only**: 4 player starts, 0 spawn clusters ('World: DM spawn'); in all four versus modes there are 0 deaths / respawns and 0 objective scores in 300 s, on both builds (and in the #18 soaks). Not a regression; the versus maps are the other 8. Escalation itself is not in this soak.
