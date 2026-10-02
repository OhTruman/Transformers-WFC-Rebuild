# HUMAN CHECK: next playable integration (Milestone 03, after Gameplay Pass 14)

A machine PASS does not tick these. Each item is a human observation for side-by-side comparison with
the original, **not permission to retune code**. Any finding goes to native RE / Gameplay with a capture.
Paths are relative to the gate output (`work/fidelity/gate/<stamp>/`) or
`tools/fidelity/results/xbranch-gameplay-6dfaf0d/traces/`.

- [ ] **Hover vehicle weight.**
  - Measured: the truck rests with its COM at 1.287 m.
  - The springs are native: K 10000, B 4000, m = M/4, damping ratio about 0.8.
  - Does it feel as heavy and planted as the original?
- [ ] **Vertical suspension response.**
  - Measured: the springs act over about 1 s (`drop10.recovery_time` 0.98 s).
  - A 1 m drop settles with a few mm of overshoot.
- [ ] **Body pitch/roll.**
  - The nose lifts 4.0° over a 0.25 m riser and 8.1° over a 0.5 m riser (springs only).
  - There is no upright control on the ground.
  - Compare the lean on slopes, kerbs and strafing.
- [ ] **Sideways inertia.**
  - Strafe and forward both stop in 0.5 s (one shared 3000 UU/s² clamp).
  - After a 90° camera turn the velocity re-aims in 0.70 s.
- [ ] **Recovery after drops.**
  - A 10 m drop lands at −18.3 m/s and dips to a COM of 0.60 m. That dip is PROVISIONAL (hull
    contact not recovered).
  - It is back at rest height in about 1 s.
- [ ] **0.25 m / 0.5 m obstacle response.**
  - At 15 m/s the hull rides up over the step (COM excursion 0.28 / 0.52 m), no snap.
  - Does it bump or scrape like the original?
- [ ] **Hover jump.**
  - +12 m/s up with horizontal speed kept, a nose-up kick and an apex of about 3.8 m.
  - Height and hang time vs the original.
- [ ] **Boost jump.**
  - +13.5 up, +6.4 forward, nose-up pitch rate about 1.8 rad/s.
- [ ] **Dash.**
  - Fires along the body/camera forward even with the stick held sideways.
  - 30 m/s for 0.5 s, then snaps to 15 m/s forward.
  - Does the snap read correctly?
- [ ] **Camera / velocity relationship.**
  - The heading locks to the rendered camera yaw (vehicle camera turns 90% in 0.10 s); velocity follows
    over about 0.7 s.
  - Check the vehicle camera feel.
- [ ] **Transform continuity.**
  - Both meshes overlap R→V 0.40–0.90 s and V→R 0.12–0.68 s (native notify times).
  - The arm mesh shows while no weapon is drawn.
  - V→R vehicle clip parts sit far below the floor inside the visible window. Confirm they are never
    visible above ground.
- [ ] **Fine Aim camera feel.** The camera moves 2.0 m back (orbit-space), with no sideways shift, FOV 45.
- [ ] **Visual hover gap.** The lowest point of the vehicle mesh sits about 1.3 m above the ground at
  rest. Is the visible gap the original's?
- [ ] **Boost / nitro rise.** Boost reaches 30 m/s in a few seconds. Nitro peaks at 43.5 m/s in its 3 s
  (cap 45). The rise depends on the PROVISIONAL tire model.
- [ ] Carried over: transform audio follows Optimus; landing audio; ambient Streets audio; sustained
  firing smooth; previously black/flat Streets surfaces; rotating/moving props.
