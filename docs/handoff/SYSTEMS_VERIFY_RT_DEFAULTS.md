# SYSTEMS_VERIFY_RT_DEFAULTS (against integration 6155925b): verify_slim checks the package 6 Realistic defaults

Switches at 6155925b (exact spelling, default; every one is an env-only static read at first use - none is a menu / profile
setting, so release()'s keep list and the SETTINGSTEST count (7: RT mode, display gamma, FSR scale / sharpness, anisotropy,
HD, FSR 3.1, Modern) do NOT change):

| item | switch | default | file |
|---|---|---|---|
| effect lights | WFC_FXLIGHTS (=0 off) | on in Realistic | game/World.cpp |
| headlights | WFC_HEADLIGHTS (=0 off) | on in Realistic | game/World.cpp |
| row 4 transient gain | WFC_RT_TLGAIN | 2.0 | WfcPipeline.cpp |
| X2 specular gain | WFC_RT_LMSPECGAIN (WFC_RT_LMSPEC=0 off) | 2.5 | WfcPipeline.cpp |
| X1 tone curve | WFC_RT_TONE (=0 off); WFC_TONE_ADAPT / _BLOOM / _CONTRAST / _GAIN (1.3) | on | WfcPipeline.cpp |
| X4 coloured bounce | WFC_RT_GISAT; WFC_RT_GIEMISSIVE | 2.5; 1.0 | WfcRtGi.cpp |
| X5-1 character AO | WFC_RT_CHARAORADIUS | 3.0 m | WfcRt.cpp |
| soft particles | WFC_SOFTPARTICLES (=0 off) | on | WfcPipeline.cpp |
| tiled transient lights | WFC_TLTILED (=0 off) | on | WfcPipeline.cpp |
| rows 1 / 8 / 6 / 2 / 5 | WFC_RT_CHARSUN / _CHARLAMPCAL / _REFLTL / _SUNSHARE / _PROBEFILL | on | WfcRtGi / WfcDirectLightEnv / D3D12RtBridge / WfcRt / WfcRtGi |
| Enhanced original specular | WFC_PBR_ORIGSPEC (=0 restores the lobe) | on | WfcPipeline.cpp |
| X3 volumetrics | WFC_X3 | not in the source yet | - |

Only the effect lights / headlights log their state (the "RT-era lights" line); probe fill and X2 have diagnostics only.
So the patch adds rtcheck.defaults to the RT gate: the Realistic run must log ONE line (Rendering to add, at the first
Realistic frame):

    RT Realistic defaults (first Realistic frame): tone=on lmspecgain=2.50 tlgain=2.00 gisat=2.50 giemissive=1.00 charao=3.0 softparticles=on tiled=on charsun=on charlampcal=on refltl=on sunshare=on probefill=on origspec=on [x3=on|off ...]

compared with $RealisticDefaults at the top of verify_slim.ps1 (strings exact, numbers within 1e-3; extra keys INFO). The
final head's list = one edit of that table. Tested standalone (test lines): all-as-expected PASS 14 / 14; a wrong gain +
a missing key FAIL naming both; no line FAIL ("an exe before Rendering's defaults line").
