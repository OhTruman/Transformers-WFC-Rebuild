# Eye pass - x3_cand13

- 508: 26 steps x 3 modes; picks ['00900', '00780', '01320']; sim split vs off: real none, realhd none
- 510: 26 steps x 3 modes; picks ['01080', '01200', '00780', '01320']; sim split vs off: real none, realhd none
- 507: 26 steps x 3 modes; picks ['00960', '01080', '00780', '01320']; sim split vs off: real none, realhd none
- 504: 26 steps x 3 modes; picks ['00840', '00900', '00780', '01320']; sim split vs off: real none, realhd none
- 509: 26 steps x 3 modes; picks ['01140', '01800', '00780', '01320']; sim split vs off: real none, realhd none

## Difference from Off (scene pixels, HUD masked; mean over same-moment pairs)

Bigger = more visibly different from the original. Contrast 1.00 = the same contrast as Off.

| map | mode | pairs | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % | misaligned |
|---|---|---|---|---|---|---|---|---|
| Streets (508) | Realistic | 26 | 22.7 | +9.7 | 1.12 | +1.4 | 37.1 |  |
| Streets (508) | Realistic + Enhanced materials | 26 | 23.4 | +9.8 | 1.14 | +1.6 | 38.4 |  |
| Gorge (510) | Realistic | 26 | 21.7 | +16.3 | 1.35 | +4.5 | 30.5 |  |
| Gorge (510) | Realistic + Enhanced materials | 23 | 20.7 | +15.1 | 1.37 | +4.0 | 30.7 | 3 |
| Debris (507) | Realistic | 26 | 26.4 | +5.7 | 1.16 | +2.6 | 39.0 |  |
| Debris (507) | Realistic + Enhanced materials | 24 | 26.3 | +3.6 | 1.15 | +2.7 | 39.8 | 2 |
| Rust (504) | Realistic | 26 | 22.9 | +14.6 | 1.28 | +4.3 | 32.0 |  |
| Rust (504) | Realistic + Enhanced materials | 25 | 22.9 | +14.3 | 1.28 | +4.6 | 33.0 | 1 |
| Molten (509) | Realistic | 25 | 24.9 | +19.8 | 1.43 | +4.6 | 35.9 | 1 |
| Molten (509) | Realistic + Enhanced materials | 23 | 23.9 | +19.0 | 1.44 | +4.5 | 35.8 | 3 |
| **all maps** | Realistic | 129 | 23.7 | +13.2 | 1.27 | +3.5 | 34.9 | |
| **all maps** | Realistic + Enhanced materials | 121 | 23.5 | +12.4 | 1.27 | +3.5 | 35.5 | |

- contrast floor Realistic: all-map 1.27 vs floor 1.26 (checkpoint 4078e47b 1.28) -> OK
- contrast floor Realistic + Enhanced materials: all-map 1.27 vs floor 1.28 (checkpoint 4078e47b 1.30) -> BELOW FLOOR
## Authored views (fixed cameras from tools/fidelity/views; difference from Off (original))

| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |
|---|---|---|---|---|---|---|---|
| Streets (508) | firefight | Realistic | 13.9 | +2.1 | 1.05 | +5.5 | 30.5 |
| Streets (508) | firefight | Realistic + Enhanced materials | 17.3 | +0.2 | 1.03 | +4.4 | 35.9 |
| Streets (508) | street | Realistic | 24.4 | -19.3 | 1.03 | -1.2 | 58.2 |
| Streets (508) | street | Realistic + Enhanced materials | 25.1 | -19.6 | 0.98 | -0.8 | 57.6 |
| Streets (508) | dark | Realistic | 12.2 | +9.6 | 1.19 | +1.3 | 16.5 |
| Streets (508) | dark | Realistic + Enhanced materials | 7.0 | +0.4 | 1.16 | +3.4 | 6.1 |
| Streets (508) | reflective | Realistic | 30.0 | +28.9 | 1.39 | +6.7 | 48.3 |
| Streets (508) | reflective | Realistic + Enhanced materials | 31.4 | +30.0 | 1.42 | +8.1 | 53.1 |
| Streets (508) | lane | Realistic | 18.8 | +10.7 | 1.06 | +2.8 | 28.4 |
| Streets (508) | lane | Realistic + Enhanced materials | 20.1 | +12.7 | 1.12 | +3.1 | 31.1 |
| Gorge (510) | firefight | Realistic | 12.2 | +10.5 | 1.77 | +6.9 | 19.3 |
| Gorge (510) | firefight | Realistic + Enhanced materials | 15.0 | +13.6 | 1.94 | +7.0 | 25.0 |
| Gorge (510) | street | Realistic | 13.0 | +2.8 | 1.26 | +1.5 | 20.4 |
| Gorge (510) | street | Realistic + Enhanced materials | 14.4 | +5.4 | 1.30 | +2.0 | 23.5 |
| Gorge (510) | dark | Realistic | 5.1 | +0.2 | 1.73 | +5.0 | 6.4 |
| Gorge (510) | dark | Realistic + Enhanced materials | 5.8 | +1.1 | 1.95 | +5.4 | 8.4 |
| Gorge (510) | reflective | Realistic | 10.5 | +6.2 | 1.63 | +5.1 | 15.0 |
| Gorge (510) | reflective | Realistic + Enhanced materials | 10.9 | +6.8 | 1.65 | +4.9 | 15.9 |
| Gorge (510) | lane | Realistic | 19.0 | +11.8 | 1.20 | +5.9 | 30.3 |
| Gorge (510) | lane | Realistic + Enhanced materials | 23.7 | +15.6 | 1.22 | +6.0 | 38.1 |
| Debris (507) | firefight | Realistic | 18.4 | -9.3 | 0.98 | +1.7 | 31.3 |
| Debris (507) | firefight | Realistic + Enhanced materials | 21.1 | -5.3 | 1.05 | +1.7 | 34.1 |
| Debris (507) | street | Realistic | 38.7 | +36.1 | 1.58 | +13.7 | 63.2 |
| Debris (507) | street | Realistic + Enhanced materials | 39.9 | +36.9 | 1.68 | +10.4 | 60.1 |
| Debris (507) | dark | Realistic | 20.0 | +15.3 | 1.22 | +6.2 | 33.9 |
| Debris (507) | dark | Realistic + Enhanced materials | 18.1 | +11.8 | 1.21 | +3.0 | 28.0 |
| Debris (507) | reflective | Realistic | 40.5 | +37.5 | 1.64 | +9.4 | 65.4 |
| Debris (507) | reflective | Realistic + Enhanced materials | 35.2 | +31.9 | 1.61 | +9.8 | 61.6 |
| Debris (507) | lane | Realistic | 10.3 | +7.2 | 1.51 | +5.0 | 12.9 |
| Debris (507) | lane | Realistic + Enhanced materials | 10.3 | +6.6 | 1.51 | +5.4 | 14.1 |
| Rust (504) | firefight | Realistic | 12.3 | +6.4 | 1.38 | +12.6 | 22.2 |
| Rust (504) | firefight | Realistic + Enhanced materials | 13.0 | +7.1 | 1.40 | +13.8 | 22.9 |
| Rust (504) | street | Realistic | 18.3 | +1.3 | 1.42 | +7.0 | 30.8 |
| Rust (504) | street | Realistic + Enhanced materials | 19.8 | +2.0 | 1.45 | +6.4 | 34.3 |
| Rust (504) | dark | Realistic | 7.0 | +3.9 | 1.74 | +4.3 | 12.0 |
| Rust (504) | dark | Realistic + Enhanced materials | 7.5 | +3.7 | 1.73 | +6.0 | 12.8 |
| Rust (504) | reflective | Realistic | 12.8 | +12.7 | 2.85 | +19.8 | 26.4 |
| Rust (504) | reflective | Realistic + Enhanced materials | 12.9 | +12.7 | 2.83 | +16.8 | 26.4 |
| Rust (504) | lane | Realistic | 11.4 | +9.8 | 1.72 | +10.3 | 18.0 |
| Rust (504) | lane | Realistic + Enhanced materials | 15.5 | +12.7 | 2.01 | +13.0 | 28.2 |
| Molten (509) | firefight | Realistic | 15.0 | -0.8 | 1.07 | +3.8 | 21.8 |
| Molten (509) | firefight | Realistic + Enhanced materials | 16.0 | -0.4 | 1.05 | +3.7 | 23.4 |
| Molten (509) | street | Realistic | 18.7 | +15.0 | 1.40 | +11.7 | 45.5 |
| Molten (509) | street | Realistic + Enhanced materials | 20.6 | +16.7 | 1.43 | +9.3 | 52.3 |
| Molten (509) | dark | Realistic | 9.4 | +6.8 | 1.35 | +4.8 | 14.2 |
| Molten (509) | dark | Realistic + Enhanced materials | 9.9 | +7.5 | 1.36 | +4.7 | 15.4 |
| Molten (509) | reflective | Realistic | 37.5 | +34.4 | 2.12 | +17.2 | 44.1 |
| Molten (509) | reflective | Realistic + Enhanced materials | 35.3 | +31.3 | 1.96 | +16.1 | 46.4 |
| Molten (509) | lane | Realistic | 17.4 | +9.3 | 1.39 | +7.9 | 26.1 |
| Molten (509) | lane | Realistic + Enhanced materials | 20.9 | +11.4 | 1.46 | +8.1 | 30.3 |

- views contrast floor Realistic: all-view mean 1.47 over 25 views vs floor 1.42 (checkpoint 4078e47b 1.47) -> OK
- views contrast floor Realistic + Enhanced materials: all-view mean 1.50 over 25 views vs floor 1.45 (checkpoint 4078e47b 1.50) -> OK
## Light-facing views (Rendering x3views; per view, not in the contrast floor) (fixed cameras from work\x3gate\cand13\xviews_list; difference from Off (original))

| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |
|---|---|---|---|---|---|---|---|
