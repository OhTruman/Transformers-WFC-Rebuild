# Threaded-sim determinism, 09c cda46cc (Gameplay's worker-thread simulation)

Seeded lockstep direct-boot TDM on Streets, 8 v 8 bots, 3600 frames (60 s of simulation), walk + strafe + jump input.
The signature is every bot's once-a-second state (WFC_BOTLOG: position / cell / form / goal / target / hp / ammo / shots /
hits / ...) plus the XP event stream.

| seed | thrA vs thrB (repeatable) | thrA vs serial (WFC_SIMTHREADS=0) | lines |
|---|---|---|---|
| 123 | identical | identical | 746 |
| 124 | identical | identical | 757 |

Negative control: seed 123 vs seed 124 differ from the first line, so the signature does detect divergence.
**Verdict: the threaded sim is deterministic and reproduces the single-threaded result.**
