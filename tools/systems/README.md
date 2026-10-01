# Systems-agent extraction helpers (read-only against Game Dump / AssetTools)

Run with `AssetTools/bin/py/python.exe`; they import `AssetTools/scripts/wfc` (objtree,
typed_props, ue3pkg) and only read the cooked packages. Write output into your own `work/`.

- `allmods.py <ParticleSystem path>` — dumps every export under a ParticleSystem (emitters,
  LOD levels, RequiredModule, surviving tagged modules) to `<path>.mods.json`.
- `lodscan.py <ParticleSystem path>` — for each LOD-0 ParticleLODLevel, prints the
  RequiredModule summary and scans WFC's compiled native module stream for UE3 raw
  distributions (`type op n chunk | int32 count | count BE floats`). Types seen:
  1 float const, 2 float curve, 3 float uniform, 7 vector const, 8 vector curve,
  9 vector uniform, 10 vector uniform curve. Curve tables are `[min, max, t0, timeScale, ...]`.

SoundCues: `objtree.package('MP_IAC_Streets_BASE_m')` + `typed_props` on the exports under
`BL_WPN_GUN_ION_BLASTER.*` give SoundCue / SoundNodeRoot / SoundNodeWaveEvent props; the C++
table in `src/game/SoundCues.cpp` was generated from that dump (class defaults from
`HM_Engine.Default__SoundNodeRoot`).
