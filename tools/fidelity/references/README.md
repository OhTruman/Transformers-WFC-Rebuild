# Presentation references

`streets_spawn_m05_1e14900/` - 13 product frames (direct boot, fixed cameras from `streets_spawnviews.txt`: 5.5 m behind
and 2.6 m above 12 evenly spaced Streets team starts plus TnTeamPlayerStart_12481, looking along the start's yaw),
rendered by `integration/milestone-05` 1e14900 Release with its own render data. Identical within noise to M04 a03d7f7
(the human-playtested full-map Streets build) and to the Rendering lane (187e6e9, 314bd06): the last known-good Streets.

`presentation-gate.ps1 -Parts reference` renders the same cameras on the build under test and compares structure
(gradient correlation) and world detail per view. Regenerate only from a build a human has confirmed as good.
