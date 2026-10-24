# FINAL_BASE_ITEMS 26 + 27 (Systems): the README sections to append after 25

## 26: the body cue sets / energon colour preload + the background body load (feaf20bd -> 6ddb0abe)

`26_hitch_loads.patch`: `git diff --binary feaf20bd 6ddb0abe` over the same paths (src, data, CMakeLists.txt; not src/render/gl,
src/render/d3d12, src/ui). It covers SYSTEMS_HITCH_LOADS (World.cpp, World.h): a first-time body load mid-match runs its CPU part
on a worker thread and finishes on a later tick, the same data at the same step; the body's CharacterAudio cue set and energon colour
load with it. It also carries the frontend data resolver merged in between (19bc0841, src/frontend/DataPaths.cpp). Apply it after 25.

## 27: the async body load's thread contract (6ddb0abe -> 4098c267)

`27_hitch_threads.patch`: `git diff --binary 6ddb0abe 4098c267` over the same paths. SYSTEMS_HITCH_THREADS (World.cpp, World.h,
ChassisDef.cpp): rosterPackage's first init is thread-safe; a body finished off the main thread is cached GPU-pending and does its
texture uploads, prewarm and presentation preloads at the join, so no GL call leaves the main thread. Apply it after 26.

Verified so far (2026-10-24):
- On a fresh full af4d0731 with feaf20bd's base paths in place of 01-25 (equal by Gameplay's rule: from 22 on, each item's tree is its
  merge commit), 26 then 27 apply clean with git apply, and all 326 base-path files equal 4098c267's (ignoring CR).
- The full build fails on items before 26, not on these two: 8c670e2f (in 22-25) makes config::kAssetRootDefault a function, which
  package 6's src/render/gl, tests/ and tools/fidelity still use as a constant (10 errors). World.cpp, ChassisDef.cpp and DataPaths.cpp
  compile. The fresh full af4d0731 + 01-27 build and the headless SIMHASH check run once 22-25 land with that fixed.
