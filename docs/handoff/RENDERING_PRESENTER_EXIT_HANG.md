# D3D12 presenter: the 1-in-32 exit hang (package 7, user-facing with Frame Generation)

Rendering, 2026-10-17. The dump is Experimental's arm B run 32 (WFC_D3D12PRESENT=1, RT off, FSR off, af4d0731 release):
`Rebuild-Experimental/work/leak/armB_d3d12present/run-32/arm-r1/wfc_hang_19880_0.dmp`.
Read with a small minidump reader (thread contexts plus a stack scan named from wfc_rebuild.map and the system DLLs' exports).
No debugger is installed.

## What the dump shows

- **The run finished normally.** The log reads "smoke test complete: ran 10200 frames". The watchdog fired 6 s later, during
  shutdown.
- **Main thread (27852):**
  `main → Application::shutdown → ~GLRenderer → shutdownD3D12Present → teardownPresenter` (D3D12Presenter::shutdown,
  inlined) `→ dxgi → D3D12Core → amdxc64 → win32u!NtGdiDdDDIDestroyAllocation2`.
  - That is the swap chain release destroying its buffers. The kernel waits until the GPU no longer uses them.
  - The wait is in the kernel and not alertable, so Stop-Process cannot end the process.
- **Every other thread (67)** sits in an idle wait: workers, GL driver (atio6axx), D3D12 and amdxx64 pool threads.
- **No GPU reset:** the System log has no Display 4101 or LiveKernelEvent between 04:50 and 05:20. The GPU did not hang. A
  GPU queue was waiting on a fence value that never came, which Windows does not treat as a hang.

## Cause: one fence, two signallers

The presenter used one shared fence for both directions:
- GL signals N after its blit.
- The D3D12 queue does Wait(N), copy, Present, then Signal(N + 1). GL and the CPU wait on N + 1 (readDone, throttle).

With flip model, 2 buffers and tearing, Present can hold the queue until a back buffer frees. Meanwhile GL finishes the next
frame and signals N + 2. The queue's late Signal(N + 1) then sets the fence **back** to N + 1. The next queue Wait(N + 2)
blocks until GL's following signal (N + 4).

- **In play:** a one-frame stall. The log's latency-wait maxima of 10-15 ms against a 1.3-2.9 ms average fit this.
- **On the last frame:** nothing signals again. Shutdown's waitIdle times out silently after 2 s. `rel(swap_)` then waits
  forever in DestroyAllocation2 for the blocked queue's copy into the back buffer. This happens once in ~32 runs: it needs
  the race on the final frame.

The four older `Rebuild/wfc_hang_*.dmp` (2026-10-06) are a different class. Their main threads are running in game code,
not parked in a kernel wait, and they come from an older exe whose map is not at hand.

## Fix (rt2, D3D12Presenter.cpp)

1. **Two fences, one signaller each.**
   - `glFence_` (new, shared): GL → D3D12. Only GL signals it (main path `glSem_`, worker path `workerSem_`) with its own
     counter `glValue_`. The queue does `Wait(glFence_, glValue_)`.
   - `fence_` keeps D3D12 → GL / CPU. Only the queue signals it (value_); readDone, throttle and waitIdle are unchanged.
   - Each fence now only rises.
2. **Shutdown:**
   - `glFinish()`, then, if GL's last signal has not reached D3D12, raise `glFence_` from the CPU to `glValue_` and log a
     warning. The drain can then always complete.
   - Also log a warning if the queue still did not drain in 2 s.

Identity: Off / Faithful run the GL present by default, so their output is unchanged. With WFC_D3D12PRESENT the frames are
the same; only the sync objects differ.

## Verification (after the reboot, card permitting)

- Experimental's arm B loop on the fixed exe: 64 runs with no exit hang, against the 1 in 32 of today.
- The per-300-frame latency-wait max should drop toward the average (no regression stalls).
- One FG-on run (the user-facing path), and a map change plus a resize with the presenter on.
