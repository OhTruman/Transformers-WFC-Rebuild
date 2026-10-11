
### Correction (2026-10-11, untimed log analysis)
- The "broken" bot in Gorge real play 4K r2 is p12 Kickback, not the off-mesh p40.
- p12 was frozen ON the nav mesh at (-199.6, 32.0, 27.3), cell 4486, in vehicle form, goal Roam, input forward, stuck 0-2, until the
  match end. It got there by climbing in robot form.
- It is the only on-mesh freeze in the 16 rows.
- Off-mesh no-way traps (the stranded suicide recovers these after 30 s):
  - T1 (-162, -12, 18), 3 runs;
  - T2 (-22, -26, -45);
  - T3, a pit floor at y -8.5, x -44 to -80, z 123-127, entered from cell 6154.
- Passed to Gameplay via Integration.
- The capacity-stress bots result now names each frozen bot and its spot.
