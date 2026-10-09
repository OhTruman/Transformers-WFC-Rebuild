# Leak sweep - 09c 942cbe2 (2026-10-09; tools/fidelity/leak-sweep.ps1)

Diagnostics: WFC_ALLOCPROF=64 + LIVE=64 + EVERY_S=3 (15 for the long match), WFC_TEXTRACE, WFC_GLTRACE, WFC_GFXMEM=30.
Private MB is judged on SETTLED lobby samples (nav.check 20 s after each return; match.unloaded keeps dropping for 10-20 s).
GL growth is judged per creation site over the LATER HALF of the dumps (first-use caches fill on the first pass, then stop).

| scenario | result |
|---|---|
| frontend: party lobby -> Create a Character (enter, browse, back) x 30 | PASS - private MB, GL textures, CaC preview meshes flat (checks land in the party lobby, not the title) |
| scoreboard: ui:Select toggled x 60 in one match | PASS - GL textures 835 -> 836 (PARTIAL by 1 name) |
| gcsafety: 3 x 2 x 60 s 64 p real play, WFC_GFX_FORCEGC + GCCHECK | PASS - 0 guard hits, 0 crashes |
| long: one 20 min 32 v 32 match (PLAYERBOT) | no steady growth (C++ heap ~ +1.7 MB/min over 15 min); a one-time +236 MB heap / +356 MB private step at 61 s left = the final-stretch music cue decoding all 6 of its waves (Systems, fixed on agents/systems 5d3f5fe) |
| maps: 8 TDM maps x 2 passes, 32 v 32 | C++ heap at lobby FLAT (383 -> 402, later half +0.3 MB); GL textures plateau 75, programs plateau 1504 (Rendering's map-program cache, cap 1500 + UI); framebuffers / renderbuffers / VAOs constant. Settled private: first pass 2935 -> 3963 (new maps), second pass ~3950 +- 50 (later-half slope 9.5 MB/match - borderline); revisit deltas on warm maps 100-215 MB |
| modes: TDM / CTF / DOM / KOTH x 3 on Streets, 10 v 10 | C++ heap FLAT (377 -> 378); GL programs 707 flat; settled private 2764 -> 2932 (later-half slope 31 MB/match) - growth OUTSIDE operator new (Systems: audio-decode arenas fix pending); GL textures +3 once at match 10 (52 -> 55) |
| all match scenarios | ~~GlCensus program leak~~ RETRACTED: ui::GlCensus::begin creates its probe program through the traced glx::CreateProgram and deletes it through a raw f.deleteProgram the trace cannot see (same for its probe buffers) - a trace artefact, not a leak (Rendering, verified in src/ui/gl/GlCensus.cpp). No renderer GL leaks. |

Harness notes: 505 BrokenHope / 506 Remnant are Escalation-only (CompatibleGameTypes=SV) - forcing TDM there by map id left the
local player without a spawn point (harness-only; the lobby filters maps by mode). Match cycles wait for the lobby return, not
ui=InGame. CTF needs a reachable PointsToWin to end.
