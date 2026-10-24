## Systems: the platform half (code reading on 48e42af6; no measurement yet)

Target: campaign and Escalation at >= 300 FPS minimum at the original settings, i.e. every frame <= 3.33 ms. The platform half is
whatever the main thread does around the renderer: present, input, audio / voice, and the loader work that can land mid-level. Each
change is load scheduling or a faster equivalent only (no visible difference), with an identity check like the load-time pass
(WFC_IMAGECMPTEST / WFC_ANIMHASH) and before / after frame-time rows.

### 1. Present path at VSync 0 / FrameLimit 0

- **GL path** (Win32Window::present): a bare SwapBuffers; with FrameLimit 0 the limiter (Sleep to the deadline, then yield) is
  skipped, and timeBeginPeriod(1) is only set while a cap is on. wglSwapIntervalEXT(0) with VSync off. No other wait on this path.
- **D3D12 interop path** (D3D12Presenter::d3dTail): WaitForSingleObjectEx on the swap chain's frame-latency waitable, then
  throttle(bi) = a fence wait on that back buffer's previous frame (at most `buffers_` frames in flight), plus the GL -> D3D12
  semaphore signal / glFlush before it. That is normal pacing, but it is exactly where a GPU-bound frame turns into a CPU stall:
  if the GPU takes > 3.3 ms, the present blocks here.
- **Risks to measure:** the latency waitable's depth (1 frame = the CPU waits on every GPU frame; 2-3 = more queue, fewer
  stalls), the frame-gen (FSR 3 FG) path's extra work in this function, and DWM composition in windowed mode (borderless /
  exclusive fullscreen avoids the compositor's queue).
- **Tool already in the tree:** WFC_PRESENTTRACE=<ms> hooks SwapBuffers (and the win32k calls) and dumps the previous frames
  around every stall >= that threshold. Run it at 3 ms on a campaign chapter and an Escalation wave.
- **Plan:** (a) measure per-frame present time and stall counts at 3.3 ms (PRESENTTRACE + WFC_LOADSTAGES-style timers) on the D3D12
  and GL paths, with each stall's GPU ms printed next to it (Rendering 2026-10-24: a stall on a frame with a high GPU time is a GPU
  problem, Rendering's; a stall with a low one is ours); (b) if the latency wait dominates, try a 2-frame latency (a setting, not a visual change) and measure input
  latency with WFC_LATENCYPROBE at the same time; (c) exclusive / flip-model fullscreen at native resolution only (the
  2026-10-12 EDID incident: no non-native mode switches).

### 2. Per-frame input and audio on the main thread

- **Input:** Win32Window::pump polls GetAsyncKeyState for every match action + every UI binding (about 100 calls a frame) and
  XInputGetState for each CONNECTED pad (an empty slot is probed once a second only - a disconnected XInputGetState costs
  milliseconds). Expected well under 0.1 ms; to confirm with a timer, not to change blindly.
- **Audio:** Win32Audio mixes on its own thread (pump every 2 ms); the main thread runs SoundCues::tick + LevelAudioHost::tick
  (cue scheduling, zones, pools, Kismet audio ops) and IAudio::update. Expected small; an Escalation wave with many emitters is
  the worst case to time.
- **Voice:** capture is its own WASAPI thread; VoiceSession::update (Opus decode per talker, 20 ms frames) runs on the main thread
  in online matches - small per talker, linear in talkers. Not on the campaign / offline Escalation path.
- **Plan:** one timer per subsystem per frame (input pump, audio tick, voice update), reported as mean / p99 / max per 1,000
  frames; anything above 0.2 ms p99 gets moved or batched.

### 3. Mid-level hitches (the frame spikes 300 FPS cannot hide)

- **First-time body loads mid-match:** World's own comment puts a first chassis load at ~130-165 ms (glb parse + animation files +
  PNG decodes + renderer prewarm), at most one per tick. In Escalation (enemy types arriving by wave) and campaign (new squad /
  enemy classes) that is a 40-50-frame hitch each time. Lossless fix: do the CPU part (glb, animation files, PNG decode - all now
  thread-safe, see the load-time pass) on a worker thread ahead of need, then upload / prewarm on the main thread in slices.
  Identity: the same loaders, so WFC_ANIMHASH / the image decode identity apply unchanged.
- **Ownership (agreed with Rendering 2026-10-24):** Systems = the CPU / file part on workers (PNG decode, glb parse, animation
  reads); Rendering = the GL part (uploads, program / skin prewarm sliced per frame with a time budget inside the renderer, behind
  the existing load / prewarm entry points - the worker hand-off calls them unchanged). Already merged: Rendering bd543d9e moved
  D1's first-draw skeletal loads (a 4.5 s in-play stall) to the chapter load.
- **Texture uploads mid-level:** World::resolveTexture uploads on first use; upload cost per frame should be capped (a per-frame
  budget, queued), measured with the new addLoadTime parts.
- **Campaign streaming:** the chapter's unit loads (Rendering's streaming) - their CPU part can use the same worker pattern; the
  GPU part is Rendering's budget.
- **Plan:** a WFC_HITCHLOG-style frame log (every frame > 3.3 ms with the labels of what ran in it), run on D1 and an Escalation
  wave, then move the top three causes off the frame.

### 4. Binary animation cache (after the baseline; Integration: first lever after the build)

- **Why:** Streets' 15 animation files are 95 MB (65 MB binary + 30 MB JSON) and cost 0.44 s (read 222 ms, JSON parse 103,
  key extraction ~120); a class's Shared_ROBO_ANIM dominates, and a mid-match class load pays the same.
- **Format:** `<file>.anim.gltf.wfcanim` beside each source: magic + version, the source's size and FNV-1a (a stale cache is
  ignored), then the parsed assets::AnimFile as-is - node names, clips (name, duration), samplers (interpolation, components, times[],
  values[] as raw floats), channels. One read + pointer fix-up, no JSON.
- **Built offline, never at runtime:** a tool mode of the exe (the same loadAnimationFile, then serialise), run by build_slim /
  AssetTools at packaging time; the package ships both (the cache is optional: missing or stale = the gltf path).
- **Identity gate:** WFC_ANIMHASH over every shipped animation file, loaded from the cache vs from the gltf: equal hash sets.
- **Expected:** most of the parse + extraction (~0.2 s per map, more for mid-match class loads), plus about 30 MB less to read.
- **MEASURED 2026-10-24 - REJECTED.** Built (cache root keyed by the path below "Transformers Rebuild/", a size + head/tail stamp,
  a clip's shared key-time arrays stored once) over every shipped animation source (base content, campaign / Escalation characters,
  DLC content): 818 files, 813 with clips, **813 / 813 byte-identical on read-back** (assets::hashAnimFile). But the parsed data is far
  larger than the sources: the cache came to **924 MB against 213 MB of sources** (1,180 MB before sharing the time arrays), and
  reading it took **4.8 s against 2.7 s** to parse the sources (both from the OS file cache). The sources store their keys more
  compactly than the expanded floats the game keeps, so any cache of the parsed AnimFile is bigger and slower to read. Not shipped;
  the code is kept at work/jip/Application_AnimCache.cpp.rejected + work/jip/animcache_edit.py. What remains for animation loads:
  the parallel load (merged 4d338915) and the background body load (merged 6ddb0abe), which already take them off the frame.
