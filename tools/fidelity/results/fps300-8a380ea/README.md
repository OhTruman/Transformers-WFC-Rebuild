# 300 fps baseline: Streets 32 v 32, 09c 8a380ea (Experimental PERF window 02:19-02:4x, no other instance)

Uncapped (VSync 0, FrameLimit 0). Frontend-launched private TDM with ExtendedPlayers, 32 + 32 bots via WFC_LOBBY_OPTIONS,
TimeLimit 60 s, two matches per process, one process per resolution. The scripted player walks / strafes / jumps.

| resolution | representative match | p50 ms | p90 ms | p99 ms | frames <= 3.33 ms | CPU submit / GPU wait (RENDERSTATS run) |
|---|---|---|---|---|---|---|
| 1920x1080 | match 2 | 20.84 | 24.04 | 26.58 | 0 % | 14.77 / 1.09 ms of 20.66 |
| 2560x1440 | match 1 | 21.75 | 27.07 | 30.83 | 0 % | 13.67 / 1.26 ms of 20.05 |
| 2560x1440 | match 2 | 21.60 | 25.36 | 28.44 | 0 % | (same) |

**Verdict: 300 fps NOT MET; ~46-48 fps median at 32 v 32 on Streets.** The game is CPU-bound on render submission:
- scene submit is ~14 ms per frame;
- GPU wait is ~1 ms;
- resolution barely changes the result.
Simulation: one 60 Hz step costs ~4-5 ms at 64 participants (sim p99 7-9 ms with 1-2 steps per frame); bot AI ~0.86 ms average.

Caveat - view dependence: the 1080p first match ran mostly at 1.5-9 ms (p50 2.99). Frame cost follows what the camera sees (draw
submission). The scripted player sometimes faces a wall, so its low numbers are a best case; the steady ~21 ms of the
other three matches is the representative figure. A fixed overview camera would make future runs comparable.

Also observed: on the time-limit end, MATCH end reason is empty ("reason= ... t=70.02"; glue line "reason=other (gameplay
reason )") -> Gameplay. Audio at 64 participants: voices capped at 96-98, dropped voices climb to the hundreds per match ->
Systems.
