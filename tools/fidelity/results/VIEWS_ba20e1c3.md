# Views-only eye pass - vo_ba20e1c (/f/Transformers Rebuild/Rebuild-Experimental/work/ab/fast_ba20e1c/build-release/bin/wfc_rebuild.exe)
## Authored views (fixed cameras from tools/fidelity/views; difference from Off (original))

| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |
|---|---|---|---|---|---|---|---|
| Rust (504) | firefight | Realistic | 11.0 | -1.2 | 1.29 | +9.7 | 14.3 |
| Rust (504) | firefight | Realistic + Enhanced materials | 11.6 | -0.5 | 1.31 | +10.9 | 16.2 |
| Rust (504) | street | Realistic | 17.1 | -6.1 | 1.33 | +3.7 | 29.7 |
| Rust (504) | street | Realistic + Enhanced materials | 18.2 | -5.2 | 1.38 | +3.1 | 31.6 |
| Rust (504) | dark | Realistic | 7.2 | +0.2 | 1.63 | +3.7 | 9.4 |
| Rust (504) | dark | Realistic + Enhanced materials | 7.7 | +0.4 | 1.64 | +5.1 | 10.7 |
| Rust (504) | reflective | Realistic | 11.2 | +10.1 | 2.61 | +19.3 | 25.1 |
| Rust (504) | reflective | Realistic + Enhanced materials | 11.2 | +10.0 | 2.59 | +16.4 | 24.7 |
| Rust (504) | lane | Realistic | 9.0 | +5.7 | 1.66 | +9.0 | 13.3 |
| Rust (504) | lane | Realistic + Enhanced materials | 13.4 | +8.7 | 1.93 | +11.9 | 23.0 |
| Debris (507) | firefight | Realistic | 17.4 | -9.2 | 1.03 | +1.3 | 28.0 |
| Debris (507) | firefight | Realistic + Enhanced materials | 20.0 | -6.0 | 1.07 | +1.4 | 31.3 |
| Debris (507) | street | Realistic | 34.8 | +32.0 | 1.55 | +12.3 | 59.8 |
| Debris (507) | street | Realistic + Enhanced materials | 35.9 | +32.6 | 1.64 | +9.1 | 56.7 |
| Debris (507) | dark | Realistic | 19.9 | +14.9 | 1.22 | +5.9 | 33.2 |
| Debris (507) | dark | Realistic + Enhanced materials | 18.1 | +11.6 | 1.21 | +2.8 | 27.7 |
| Debris (507) | reflective | Realistic | 33.6 | +29.8 | 1.54 | +8.6 | 58.0 |
| Debris (507) | reflective | Realistic + Enhanced materials | 28.8 | +24.7 | 1.53 | +8.5 | 53.7 |
| Debris (507) | lane | Realistic | 10.2 | +7.2 | 1.51 | +5.0 | 12.9 |
| Debris (507) | lane | Realistic + Enhanced materials | 10.4 | +6.6 | 1.52 | +5.4 | 14.2 |

- views contrast floor Realistic: all-view mean 1.54 over 10 views vs floor 1.42 (checkpoint 4078e47b 1.47) -> OK
- views contrast floor Realistic + Enhanced materials: all-view mean 1.58 over 10 views vs floor 1.45 (checkpoint 4078e47b 1.50) -> OK
## Lens flare views (sun-aimed; report-only) (fixed cameras from work\eyepass\vo_ba20e1c\flare_list; difference from Off (original))

| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |
|---|---|---|---|---|---|---|---|

## Read (Experimental 2026-10-23 00:2x)
- Fog regression row (a78386dc per-pixel height fog): Rust 'dark' lower third, Off 47.8 (ab855fe7) -> **15.9** (expected ~16);
  Realistic 38.9 -> 12.7, whole-frame contrast std 22.2 -> 29.1. PASS: the near-floor sheet is gone.
- Views contrast on Rust + Debris (10 views): Realistic 1.54, + Enhanced 1.58 - above the package 6 floors (1.42 / 1.45).
- Lens-flare views: NOT captured (0 / 10 per map and mode) - the flare list was passed as a relative path the exe's working directory
  could not resolve; fixed in views-only.sh / eyepass.sh (absolute path); a 5-min rerun of the flare views is needed.
