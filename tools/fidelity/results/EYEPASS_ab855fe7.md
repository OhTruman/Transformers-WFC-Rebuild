# Eye pass on ab855fe7 (the package 7 internal checkpoint) - Experimental, 2026-10-17 16:0x

eyepass.sh on work/ab/fast_ab855fe (Off / Realistic / Realistic + HD 2, 5 user maps, match frames + 25 authored views, 2560x1440,
live render data with (A), fogs, (B), the FX and door data and the lens flares). Compared with package 6's eye pass (x3_cand13).

| floor | package 6 (x3_cand13) | ab855fe7 | floor | read |
|---|---|---|---|---|
| match all-map, Realistic | 1.2654 | 1.27 | 1.26 | OK |
| match all-map, + Enhanced | 1.2748 | 1.26 | 1.28 | BELOW |
| views all-view, Realistic | 1.47 | **1.38** | 1.42 | BELOW |
| views all-view, + Enhanced | 1.50 | **1.40** | 1.45 | BELOW |

The contrast ratio is Realistic / Off, and BOTH sides moved today: Off gained fog (x 1 / ln 2 in-scatter, Rendering's Set E, all
modes) and (B)'s brighter materials. Per view (luminance std):

| view | Off std / mean (p6 -> p7) | Realistic std / mean (p6 -> p7) | reading |
|---|---|---|---|
| Rust 504 'dark' | 17.1 / 18.2 -> 17.9 / 40.7 | 29.6 / 22.1 -> **21.0** / 34.3 | **finding**: the floor in front of the camera is now a flat grey-teal sheet in BOTH Off and Realistic (results/p7_ab855fe/rust_dark_off.png, flare_views.png top row) - the near ground below the fog height is fogged almost opaque; the walls above are unchanged. Realistic lost ~30 % contrast here |
| Rust 504 'reflective' | 10.7 / 13.2 -> 10.9 / 21.6 | 30.0 -> 25.0 | same ground haze, milder |
| Rust 504 'lane' | 15.8 -> 15.8 | 27.0 -> 23.9 | slight |
| Gorge 510 'firefight' | 16.9 -> **22.2** / 20.5 -> 28.8 | 29.9 -> 30.0 | Off gained contrast (fog / (B)); Realistic unchanged - the ratio drop is not a Realistic regression |
| Debris 507 'lane' | 22.4 -> 22.5 | 33.7 -> 33.8 | identical (no fog, no doors on Debris) |

Lens flares: Rust 'dark' shows no flare (as expected); the Rust 'lane' and Debris 'lane' views show NO visible flare either (no
change vs package 6 in those frames) - the authored large faint flare is not in these cameras' view; a flare check needs a view
aimed at a flare source (lens_flares.json positions) - to add.

For Rendering: is Rust's near-ground haze (the 2 height-fog layers x 1.443 in-scatter, all modes) the original's look? On the
eye it hides the floor texture under a flat sheet in 'dark' / 'reflective'. If authored, the views floor should be re-based on
ab855fe7; if not, it is the fog density / height of Rust's low layer.
