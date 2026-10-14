# Cross-branch observation: origin/agents/systems d86b863 (read-only)

Built in isolation with `ab.ps1 -Ref origin/agents/systems -Measure` (git archive into `work/ab`,
this harness overlaid, nothing merged, no other worktree touched). Same tools as the milestone-02
baseline.

| file | content |
|---|---|
| harness.json | `wfc_fidelity --map`: 265 PASS / 0 FAIL / 33 KNOWN / 329 INFO (milestone-02: 262/0/36) |
| audio.json, offending_cues.txt | audio-attach (11 scenarios): 100 PASS / 0 FAIL / 0 KNOWN; no sound stops following its owner |
| perf_attribution.csv, hot_functions.txt | sustained fire 11.2 ms/frame (milestone-02 298.7); idle 6.33 |
| counters.json, counters_*.csv | exact per-frame calls (lockstep): firing 57 visibility rays, 60 segmentHit, 6.65 light envs, 14 mesh draws |
