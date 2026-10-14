# Presentation measures: definitions and calibration

The measures are in `tools/fidelity/lib/Presentation.cs`; the verdict rules are in `lib/Present.ps1`. They are built
to catch **catastrophic** failures, not to recognise screenshots. A PASS still needs a human look.

## Measures (8×8 pixel blocks in a region)
| measure | meaning |
|---|---|
| detail | fraction of blocks with luma std-dev ≥ 6: textured / structured content |
| black | fraction of pixels with luma < 12 |
| maxFlat | largest connected area of featureless blocks (std-dev < 2.5) with similar colour: one giant blank / grey / black area |
| untexMax / untexFrac | largest connected area, and total share, of blocks that are lit (luma > 30), desaturated (< 0.20) and without texture detail (mean \|Laplacian\| < 1.5): geometry drawn without its material, placeholders, malformed polygons |
| noise | share of blocks whose \|Laplacian\| exceeds 2.6× their std-dev: noise / garbage textures |
| similarity | gradient-energy correlation on a 64×36 grid: the same architecture in the same place |

## Regions
- **Gameplay "world"**: left third, right third, and upper centre, all below the HUD band (y 20–88 %).
  - Excluded: the HUD band, the lower-centre player character and the bottom HUD strip.
  - So **a HUD or Optimus alone cannot make a gameplay frame pass.**
  - The blank-area measures cover the whole world rectangle, because a near wall filling one third is normal.
- **Title scene**: the area left of the PC main menu (x 0–55 %).
- **Lobby scene**: the area right of the roster panel.

## Rules
- **Gameplay frame.**
  - FAIL when any one holds:
    - detail < 0.10;
    - black ≥ 0.60;
    - one blank area ≥ 0.60 of the world;
    - largest untextured area ≥ 0.12, or untextured total ≥ 0.40;
    - noise ≥ 0.15.
  - PARTIAL when detail < 0.15.
- **A set of frames** (one state or one map): FAIL when **most** frames FAIL; PARTIAL when some do.
- **Title scene.**
  - FAIL when black ≥ 0.70, blank area ≥ 0.45, detail < 0.10, or a malformed signature.
  - PARTIAL when detail < 0.12.
- **Menu screen.** FAIL when:
  - detail < 0.04;
  - blank ≥ 0.80;
  - black ≥ 0.90;
  - a malformed signature;
  - the scene area beside the panel is one blank area ≥ 0.60, or more than 15 % flat grey.
- **Route vs direct.** FAIL when the frontend-launched world has < 0.5× the detail of a direct boot at the same start.
- **Reference.** FAIL when the median similarity to the known-good Streets cameras is < 0.6, or more than 25 % of views
  are lost (detail ratio < 0.5 or similarity < 0.4).

## Calibration frames
| frame | detail | black | other | expected | rule result |
|---|---|---|---|---|---|
| M06 95edd7b frontend-route Streets spawn / moving (Debug, Integration data) | 0.055 / 0.019 / 0.019 | 0.05–0.67 | | broken (human report) | FAIL ×6 |
| M06 95edd7b same start, direct boot | 0.197 | 0.12 | | intact corridor | PASS |
| M05 1e14900 frontend-route Streets spawn / moving | 0.198 / 0.224 / 0.155 | 0.08–0.20 | untextured 0.075 / 0.29 (close-up metal) | good | PASS |
| Frontend 0160cf1 frontend-route Streets | 0.201 / 0.228 / 0.155 | | | good | PASS / PARTIAL |
| M06 Seed / Molten / Complex / Debris first in-match frame | 0.33 / 0.45 / 0.30 / 0.59 | ≤ 0.13 | | playable | PASS |
| M06 title over 60 s | 0.28–0.38 | ≤ 0.02 | untextured ≤ 0.03 / 0.12 | plausible | PASS |
| Frontend 9e67bf8 title (giant untextured sphere) | 0.19 | 0.11 | **untextured 0.23 / 0.43** | malformed (human report) | FAIL |
| Frontend 9e67bf8 lobby (grey slab) | 0.11–0.16 | | **untextured 0.30 / 0.55**, flat grey 26–34 % | blank / grey (human report) | FAIL |
| Frontend 9e67bf8 frontend after a match | 0.59 | 0.47 | **noise 0.43** | garbage plates | FAIL |
| M05 title (no scene) | 0.03 | 0.97 | | black (known M05 gap) | FAIL |
| Streets sky-only free views (v00–v02) | 0.06–0.08 | 0 | flat 0.69–0.80 | no architecture | empty (lit) |

## Known limits
- Dark maps by design (Gorge, Rust, Remnant corridors) can fail the black / dark-empty rules. They stay FAIL until a
  human confirms the darkness is authored.
- Fog or dust maps (BrokenHope) show as lit-empty and are PARTIAL, with a human check.
- Scripted input (`WFC_AUTO*`) is injected after the menu input gate, so "no movement under a menu" cannot be measured.
  It is reported UNKNOWN.
- The gate reads frames rendered by the product (`shot:`). It cannot see swap-chain or pacing artefacts on a real monitor.
