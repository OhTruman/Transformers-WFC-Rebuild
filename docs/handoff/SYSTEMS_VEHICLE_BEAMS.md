# Vehicle headlight sockets: INVENTED PLACEMENT (Realistic-only addition)

The original authors no vehicle headlights (SYSTEMS_VEHICLE_LIGHTS.md): this table is an addition, approved by Integration 2026-10-16, for Realistic only. Sockets come from each MP vehicle emissive lamp areas (tools/systems/vehicle_beams.py: forward-facing triangles, n.x >= 0.5, in the front third whose emissive texture is bright, clustered at 0.15 m; mirrored pairs matched). Pick = the mirrored pair facing most directly forward, then frontmost. Positions in vehicle root space (the vehicle.glb bind pose), glTF Y-up metres, +X forward, +Z = UE +Y; the R lamp mirrors z. dir = the lamp surface normal (aim the spot along it, or along +X with the pitch from dir.y). Emissive = the lamp texture colour, linear: mostly the Autobot-blue energon glow of the default skin, so Rendering picks the light colour (team colour or warm white). Confidence: high = a forward-facing pair (dir.x >= 0.9); medium / low = angled or energon-trim lamps; none = no forward lamp (skip, or place by hand). Full clusters: SYSTEMS_VEHICLE_BEAMS.json.

| vehicle | form | pick | lamp pos m (L; R = z mirrored) | dir | emissive (linear) | area m2 | confidence |
|---|---|---|---|---|---|---|---|
| Car10 | car | pair | [1.779, 0.461, 0.296] | [1.0, 0.003, 0.0] | [0.23, 0.556, 0.927] | 0.0359 | high |
| Car2 | car | single | [1.728, 0.377, 0.008] | [0.813, 0.582, -0.0] | [0.127, 0.304, 0.509] | 0.1011 | low |
| Car3 | car | pair | [1.321, 0.329, 0.454] | [0.978, -0.209, 0.007] | [0.08, 0.19, 0.32] | 0.0823 | high |
| Car4 | car | pair | [1.858, 0.52, 0.711] | [0.969, -0.243, 0.045] | [0.2, 0.483, 0.803] | 0.0042 | high |
| Car5 | car | pair | [1.932, 0.345, 0.514] | [0.972, 0.026, 0.232] | [0.105, 0.251, 0.421] | 0.0308 | high |
| Car6 | car | pair | [1.654, 0.541, 0.603] | [1.0, 0.0, 0.0] | [0.103, 0.247, 0.413] | 0.0145 | high |
| Car7 | car | pair | [1.157, 0.294, 0.659] | [1.0, 0.017, 0.0] | [0.147, 0.353, 0.59] | 0.0277 | high |
| Car8 | car | pair | [1.13, 0.352, 0.991] | [0.966, -0.259, 0.004] | [0.159, 0.384, 0.641] | 0.0756 | high |
| Car9 | car | pair | [1.779, 0.461, 0.296] | [1.0, 0.003, 0.0] | [0.23, 0.556, 0.928] | 0.0359 | high |
| Car | car | pair | [0.948, 0.331, 0.911] | [0.808, 0.007, 0.589] | [0.16, 0.387, 0.646] | 0.0156 | medium |
| Jet2 | jet | pair | [2.444, 1.285, 0.124] | [0.882, 0.471, 0.0] | [0.147, 0.355, 0.593] | 0.0021 | medium |
| Jet3 | jet | pair | [2.444, 1.285, 0.124] | [0.882, 0.471, 0.0] | [0.147, 0.354, 0.589] | 0.0021 | medium |
| Jet4 | jet | single | [2.639, 0.493, -0.009] | [0.558, -0.83, -0.0] | [0.137, 0.327, 0.55] | 0.1027 | low |
| Jet5 | jet | single | [2.312, 0.88, 0.0] | [0.832, 0.555, 0.0] | [0.114, 0.275, 0.458] | 0.0091 | low |
| Jet6 | jet | single | [2.806, 0.708, 0.0] | [0.916, 0.401, 0.001] | [0.057, 0.135, 0.227] | 0.0319 | medium |
| Jet7 | jet | pair | [4.431, 0.166, 0.459] | [0.697, -0.377, 0.61] | [0.119, 0.289, 0.482] | 0.0019 | low |
| Jet8 | jet | single | [2.097, 0.598, -0.512] | [0.92, -0.281, -0.275] | [0.072, 0.173, 0.289] | 0.0168 | medium |
| Jet | jet | pair | [2.444, 1.285, 0.124] | [0.882, 0.471, 0.0] | [0.147, 0.355, 0.593] | 0.0021 | medium |
| Minion1 | car | pair | [1.13, 0.352, 0.991] | [0.966, -0.259, 0.004] | [0.159, 0.384, 0.641] | 0.0756 | high |
| Minion2 | car | pair | [1.13, 0.352, 0.991] | [0.966, -0.259, 0.004] | [0.159, 0.384, 0.641] | 0.0756 | high |
| Minion3 | jet | none | | | | | none |
| Optimus | None | pair | [1.67, 1.775, 0.221] | [1.0, 0.001, 0.0] | [0.124, 0.3, 0.501] | 0.0527 | high |
| Tank2 | tank | single | [1.951, 1.688, -0.87] | [0.567, 0.824, 0.001] | [0.18, 0.433, 0.721] | 0.0205 | low |
| Tank3 | tank | pair | [2.602, 1.049, 0.248] | [0.856, 0.513, 0.064] | [0.217, 0.525, 0.872] | 0.0330 | medium |
| Tank4 | tank | pair | [2.474, 0.859, 0.17] | [1.0, 0.0, 0.0] | [0.17, 0.41, 0.68] | 0.0225 | high |
| Tank5 | tank | pair | [2.19, 2.009, 0.225] | [0.694, 0.72, 0.0] | [0.138, 0.332, 0.552] | 0.0303 | low |
| Tank | tank | none | | | | | none |
| Truck2 | truck | pair | [1.551, 1.116, 0.7] | [0.961, 0.256, -0.109] | [0.247, 0.599, 1.0] | 0.0033 | high |
| Truck3 | truck | pair | [1.538, 1.14, 0.681] | [0.963, 0.257, -0.082] | [0.197, 0.473, 0.789] | 0.0069 | high |
| Truck4 | truck | pair | [1.263, 0.652, 1.056] | [0.933, 0.359, 0.0] | [0.093, 0.224, 0.373] | 0.0905 | high |
| Truck5 | truck | pair | [1.752, 0.542, 0.386] | [0.91, 0.397, 0.116] | [0.055, 0.133, 0.223] | 0.0181 | high |
| Truck6 | truck | pair | [1.541, 2.094, 0.164] | [1.0, 0.0, 0.0] | [0.073, 0.175, 0.293] | 0.0060 | high |
| Truck7 | truck | pair | [1.67, 1.775, 0.221] | [1.0, 0.001, 0.0] | [0.124, 0.3, 0.501] | 0.0527 | high |
| Truck | truck | pair | [1.67, 1.775, 0.221] | [1.0, 0.001, 0.0] | [0.124, 0.3, 0.501] | 0.0527 | high |

Caveats: geometric heuristic, not checked visually. Some picks are high lamp pairs (Truck / Truck6 / Truck7 / Optimus at 1.8-2.1 m: roof or cab lamps) where a lower, more angled pair exists (Truck: x 3.04, y 0.22, z 0.64, dir.x 0.82; see the JSON clusters). Tank and Minion3 have no forward lamp area; Jet low-confidence picks are energon trim. Shared meshes give identical rows (Car9 / Car10, Jet / Jet2 / Jet3, Truck / Truck7 / Optimus, Car8 / Minion1 / Minion2).
