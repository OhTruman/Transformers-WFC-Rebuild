# AMD nonpaged-pool leak: Rendering's suspects (package 7 item 4)

Written 2026-10-17 03:29, before any measurement. Experimental characterizes; Rendering owns the D3D12 / GL mitigation.

Symptom: nonpaged pool grows ~0.43 GB/h under GPU load; only a reboot frees it. The growth is per hour, not per map change, so
per-frame kernel objects come first. The per-map shared pages were already pooled in package 5 (rt2 77d9e02c), so they are
not suspects.

## Suspects, in test order

1. **The presenter's per-frame event** (D3D12Presenter.cpp, throttle() and waitIdle()). Each frame that has to wait does
   CreateEventW + SetEventOnCompletion + WaitForSingleObject(1000 ms) + CloseHandle. If the wait times out, the fence still
   holds the event when it is closed, so the driver may keep its registration in kernel pool.
   - Test: the D3D12 presenter on vs off (GL present), RT off, 1 h each, pool sampled every minute.
   - Fix: one persistent event per presenter, like the bridge and the upscaler already have.
2. **Per-frame GL <-> D3D12 shared-semaphore signal / wait** (AMD GL interop). Every frame has:
   - in the bridge: signalSem_ and waitSem_ (D3D12RtBridge.cpp 810 / 826);
   - in the presenter: signalSem and waitSem (D3D12Presenter.cpp 268 / 333 / 386 / 588);
   - in the upscaler: its own pair.

   The AMD GL driver may allocate a kernel sync object for each wait.
   - Test arms:
     - RT Off with the GL present (no interop at all);
     - RT Off with the D3D12 present (presenter semaphores only);
     - Realistic (bridge + presenter);
     - Realistic with FSR or frame generation (all three).
   - Compare pool growth per hour across the arms.
   - Fix if confirmed: fewer sync points per frame (one signal / wait pair per frame shared by bridge, upscaler and
     presenter), or a D3D12-side wait in place of the GL wait.
3. **The RT bake / BLAS rebuilds on map change** (D3D12-only allocations). Lower odds, because the growth is per hour.
   - Test: idle in one map for 1 h vs a mapswitch loop for 1 h, same mode.
4. **DML (upscaler / denoiser jobs)**: Experimental listed it; not a Rendering path.

The pool counter to sample is `\Memory\Pool Nonpaged Bytes`. Use the poolmon tags if available, so the owner (the driver
vs dxgkrnl) is visible.
