# matc vector-parameter channel scan (Systems, 2026-10-17)

**Cause (found):** tools/render/matc.py `x_VectorParameter` returns the expression's selected output pin (1..4 = R / G / B / A)
only for materials listed in VECTOR_CHANNEL_PROVEN (vector_channel_proven.txt) or with WFC_MATC_VECTOR_CHANNELS=1; for every
other material it returns the whole vec4, so a graph that takes Color.A (an intensity) gets Color.rgba (a tint). DeathDecal_MAT:
weight G x Color.A (1) became G x Color.rgb (20) -> m max 25.7 instead of 2.24 (Rendering's check), the white / black scorch.

**Method:** the 10 MP maps' material sets built twice with af4d0731's tools/render (build_materials_only.py; WFC_ASSETTOOLS set):
as shipped (reproduces the shipped materials_glsl.json exactly on all 10 maps: 0 differing of 574-669 entries per map) and with
WFC_MATC_VECTOR_CHANNELS=1; per material the GLSL diffed and the selected literal channel compared with the components the
shipped code used (tools/systems/matscan_diff.py, matscan_class.py, matscan_show.py; data: docs/handoff/matscan/).

**Result: 116 materials' GLSL changes; 108 change value** (the selected channel differs from the rgb the shipped code used),
7 are equivalent (uniform vectors, e.g. DecalDefault (1, 1, 1, 1)), 1 not classified (a non-literal source). No compile error
with the channels honoured. The 108 are mostly one UE3 idiom - a colour VectorParameter whose alpha is an intensity:
light shafts / volumes (LightRaysV2* / LightVolume*: (tint, 1-2).w), force fields and glass (ENV_ForceField*, IAC_Glass*,
barricades: (blue, 3).w), energon plasma / lava ((rgb, 35-150).w), rockets ((1.25, 0.05, 0.05, 2).w), the decals
(DeathDecal (20, 20, 20, 1).w, MetalDecal (5, 5, 5, 1).w), vending glass ((rgb, 5).w), shields, the Omega battery, the
corruptor crystals ((0, 0.03, 0.46, 10).w). Full list with the selected values: docs/handoff/matscan/scan_classes.json.

**What it means:** today these draw with intensity x tint where the graph has intensity x scalar (or the reverse), in every
mode incl. Off / Faithful. Honouring the pin is the faithful translation (the pin is authored in the graph; DeathDecal is
proven against the shipped PS by RE). The visual change is broad (glows, glass, force fields, shafts on every map), so the
regen needs a per-map eye check before it ships, and ideally a microcode spot check on 2-3 representative materials (one
LightRaysV2, one ForceField, one plasma) the way DeathDecal was proven.
