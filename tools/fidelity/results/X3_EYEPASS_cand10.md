# Eye pass - x3_cand10

- 508: 26 steps x 3 modes; picks ['00900', '00780', '01320']; sim split vs off: real none, realhd none
- 510: 26 steps x 3 modes; picks ['01080', '01200', '00780', '01320']; sim split vs off: real none, realhd none
- 507: 26 steps x 3 modes; picks ['00960', '01080', '00780', '01320']; sim split vs off: real none, realhd none
- 504: 26 steps x 3 modes; picks ['00840', '00900', '00780', '01320']; sim split vs off: real none, realhd none
- 509: 26 steps x 3 modes; picks ['01140', '01800', '00780', '01320']; sim split vs off: real none, realhd none

## Difference from Off (scene pixels, HUD masked; mean over same-moment pairs)

Bigger = more visibly different from the original. Contrast 1.00 = the same contrast as Off.

| map | mode | pairs | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % | misaligned |
|---|---|---|---|---|---|---|---|---|
| Streets (508) | Realistic | 26 | 45.0 | +35.6 | 0.95 | -1.7 | 68.9 |  |
| Streets (508) | Realistic + Enhanced materials | 26 | 45.1 | +35.0 | 0.95 | -1.7 | 69.0 |  |
| Gorge (510) | Realistic | 26 | 31.7 | +28.5 | 1.19 | +2.0 | 52.8 |  |
| Gorge (510) | Realistic + Enhanced materials | 24 | 30.4 | +27.0 | 1.21 | +1.7 | 52.0 | 2 |
| Debris (507) | Realistic | 26 | 55.6 | +47.9 | 0.91 | -2.0 | 73.9 |  |
| Debris (507) | Realistic + Enhanced materials | 24 | 55.5 | +46.9 | 0.91 | -1.9 | 74.0 | 2 |
| Rust (504) | Realistic | 26 | 43.9 | +39.8 | 1.10 | -0.3 | 65.1 |  |
| Rust (504) | Realistic + Enhanced materials | 25 | 44.2 | +39.9 | 1.10 | -0.1 | 65.8 | 1 |
| Molten (509) | Realistic | 25 | 46.7 | +44.5 | 1.24 | +1.3 | 64.3 | 1 |
| Molten (509) | Realistic + Enhanced materials | 24 | 45.8 | +43.9 | 1.25 | +0.9 | 64.6 | 2 |
| **all maps** | Realistic | 129 | 44.6 | +39.3 | 1.08 | -0.1 | 65.0 | |
| **all maps** | Realistic + Enhanced materials | 123 | 44.2 | +38.5 | 1.09 | -0.2 | 65.1 | |

- contrast floor Realistic: all-map 1.08 vs floor 1.26 (checkpoint 4078e47b 1.28) -> BELOW FLOOR
- contrast floor Realistic + Enhanced materials: all-map 1.09 vs floor 1.28 (checkpoint 4078e47b 1.30) -> BELOW FLOOR
## Authored views (fixed cameras from tools/fidelity/views; difference from Off (original))

| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |
|---|---|---|---|---|---|---|---|
| Streets (508) | firefight | Realistic | 13.7 | +0.5 | 1.05 | +5.4 | 30.3 |
| Streets (508) | firefight | Realistic + Enhanced materials | 17.1 | -1.4 | 1.02 | +4.4 | 35.4 |
| Streets (508) | street | Realistic | 23.9 | -18.8 | 0.99 | -1.5 | 56.1 |
| Streets (508) | street | Realistic + Enhanced materials | 24.5 | -19.0 | 0.94 | -1.1 | 55.8 |
| Streets (508) | dark | Realistic | 11.8 | +9.1 | 1.17 | +1.2 | 15.7 |
| Streets (508) | dark | Realistic + Enhanced materials | 6.8 | -0.3 | 1.13 | +3.2 | 5.6 |
| Streets (508) | reflective | Realistic | 29.9 | +28.8 | 1.36 | +5.9 | 47.6 |
| Streets (508) | reflective | Realistic + Enhanced materials | 31.3 | +29.8 | 1.39 | +7.1 | 51.6 |
| Streets (508) | lane | Realistic | 18.6 | +9.0 | 1.05 | +2.5 | 28.2 |
| Streets (508) | lane | Realistic + Enhanced materials | 20.0 | +11.4 | 1.12 | +2.9 | 31.1 |
| Gorge (510) | firefight | Realistic | 11.8 | +10.1 | 1.75 | +6.7 | 18.6 |
| Gorge (510) | firefight | Realistic + Enhanced materials | 14.8 | +13.3 | 1.92 | +6.9 | 24.6 |
| Gorge (510) | street | Realistic | 12.5 | +2.9 | 1.23 | +1.2 | 20.1 |
| Gorge (510) | street | Realistic + Enhanced materials | 13.9 | +5.5 | 1.27 | +1.8 | 23.0 |
| Gorge (510) | dark | Realistic | 5.1 | +0.1 | 1.70 | +4.9 | 6.2 |
| Gorge (510) | dark | Realistic + Enhanced materials | 5.7 | +1.0 | 1.91 | +5.3 | 8.2 |
| Gorge (510) | reflective | Realistic | 10.8 | +6.8 | 1.61 | +5.0 | 15.1 |
| Gorge (510) | reflective | Realistic + Enhanced materials | 11.2 | +7.4 | 1.63 | +4.8 | 16.1 |
| Gorge (510) | lane | Realistic | 19.9 | +12.3 | 1.13 | +4.5 | 34.2 |
| Gorge (510) | lane | Realistic + Enhanced materials | 24.4 | +16.0 | 1.15 | +4.8 | 43.1 |
| Debris (507) | firefight | Realistic | 22.8 | -7.5 | 0.99 | +2.9 | 40.2 |
| Debris (507) | firefight | Realistic + Enhanced materials | 24.9 | -3.7 | 1.05 | +2.8 | 43.2 |
| Debris (507) | street | Realistic | 39.0 | +34.7 | 1.52 | +13.2 | 65.8 |
| Debris (507) | street | Realistic + Enhanced materials | 40.0 | +35.4 | 1.62 | +9.9 | 62.5 |
| Debris (507) | dark | Realistic | 19.5 | +14.5 | 1.18 | +5.8 | 32.9 |
| Debris (507) | dark | Realistic + Enhanced materials | 18.0 | +11.5 | 1.17 | +2.9 | 28.0 |
| Debris (507) | reflective | Realistic | 38.1 | +35.2 | 1.51 | +8.7 | 65.7 |
| Debris (507) | reflective | Realistic + Enhanced materials | 32.9 | +30.0 | 1.48 | +9.0 | 61.0 |
| Debris (507) | lane | Realistic | 9.9 | +6.8 | 1.42 | +4.8 | 12.4 |
| Debris (507) | lane | Realistic + Enhanced materials | 9.9 | +6.1 | 1.42 | +5.2 | 13.6 |
| Rust (504) | firefight | Realistic | 11.8 | +5.7 | 1.35 | +12.4 | 21.3 |
| Rust (504) | firefight | Realistic + Enhanced materials | 12.6 | +6.5 | 1.38 | +13.7 | 22.0 |
| Rust (504) | street | Realistic | 17.0 | +0.2 | 1.31 | +5.7 | 28.6 |
| Rust (504) | street | Realistic + Enhanced materials | 18.4 | +0.8 | 1.34 | +5.1 | 31.9 |
| Rust (504) | dark | Realistic | 6.4 | +2.8 | 1.64 | +3.8 | 10.8 |
| Rust (504) | dark | Realistic + Enhanced materials | 6.9 | +2.7 | 1.64 | +5.2 | 11.7 |
| Rust (504) | reflective | Realistic | 11.9 | +11.7 | 2.71 | +18.5 | 25.4 |
| Rust (504) | reflective | Realistic + Enhanced materials | 12.0 | +11.7 | 2.69 | +15.8 | 25.2 |
| Rust (504) | lane | Realistic | 10.6 | +8.9 | 1.62 | +9.5 | 16.3 |
| Rust (504) | lane | Realistic + Enhanced materials | 14.6 | +11.7 | 1.93 | +12.2 | 26.6 |
| Molten (509) | firefight | Realistic | 15.0 | -0.9 | 1.07 | +3.7 | 21.9 |
| Molten (509) | firefight | Realistic + Enhanced materials | 16.0 | -0.4 | 1.05 | +3.7 | 23.4 |
| Molten (509) | street | Realistic | 18.6 | +14.9 | 1.39 | +11.7 | 45.3 |
| Molten (509) | street | Realistic + Enhanced materials | 20.5 | +16.5 | 1.43 | +9.4 | 52.0 |
| Molten (509) | dark | Realistic | 9.4 | +6.8 | 1.35 | +4.8 | 14.2 |
| Molten (509) | dark | Realistic + Enhanced materials | 9.9 | +7.5 | 1.36 | +4.7 | 15.4 |
| Molten (509) | reflective | Realistic | 37.5 | +34.5 | 2.11 | +17.4 | 44.2 |
| Molten (509) | reflective | Realistic + Enhanced materials | 35.2 | +31.4 | 1.95 | +16.3 | 46.5 |
| Molten (509) | lane | Realistic | 17.3 | +9.2 | 1.39 | +7.9 | 26.1 |
| Molten (509) | lane | Realistic + Enhanced materials | 20.9 | +11.4 | 1.46 | +8.1 | 30.3 |

- views contrast floor Realistic: all-view mean 1.42 over 25 views vs floor 1.42 (checkpoint 4078e47b 1.47) -> OK
- views contrast floor Realistic + Enhanced materials: all-view mean 1.46 over 25 views vs floor 1.45 (checkpoint 4078e47b 1.50) -> OK
## Light-facing views (Rendering x3views; per view, not in the contrast floor) (fixed cameras from work\x3gate\cand10\xviews_list; difference from Off (original))

| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |
|---|---|---|---|---|---|---|---|
