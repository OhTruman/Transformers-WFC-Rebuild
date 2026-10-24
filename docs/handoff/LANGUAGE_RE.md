# Language selection and shipped languages (RE, 2026-10-23; for Frontend's Language option)

**Labels:** C = exe (Ghidra snapshot) / shipped data; H = inferred.

## 1. How the original picks the language (C, exe)
- **Init:** `Function_829CD960`, the appGetLanguageExt equivalent, caches its result at 0x83757390.
- **Override:** a command-line / ini override (string 0x823DB930) is checked first. **H:** a stock `LANGUAGE=` style override.
- **Otherwise the dashboard language:** `Function_83304BF8()` (H: XGetLanguage) indexes the table at **0x83708E68**:

| XGetLanguage | Dashboard language | Code |
|---|---|---|
| 0 | none | INT |
| 1 | English | INT |
| 2 | Japanese | JPN |
| 3 | German | DEU |
| 4 | French | FRA |
| 5 | Spanish | ESN |
| 6 | Italian | ITA |
| 7 | Korean | KOR |
| 8 | Traditional Chinese | CHI |
| 9 | Portuguese | runtime (0x829CD7D0) |
| 10 | Simplified Chinese | INT |
| 11 | Polish | INT |
| 12 | Russian | RUS |

- Values ≥ 13 give INT.
- **Existence check (`0x829CD898`):** the chosen code is kept only if that language's localisation file exists (a formatted path test).
  Otherwise it **falls back to INT**, then further codes (DEU…).
- **Result with the shipped dump:** a French dashboard gives **FRA**; everything else gives **INT**.
- **Other uses:**
  - `0x82CBEEE0` maps the code to an index (FRA 1, ITA 2, DEU 3, ESN 4, RUS 5, POL 6, else 0). H: the audio / VO language index.
  - Other strings in the same code group: POL, ESM, PS3, 360, PC.

## 2. Shipped languages (C, the dump)
- `Localization/Cooked/Xenon/Coalesced.int` and `Coalesced.FRA` only.
- Packages: 267 `*_FRA.xxx` (VO / LOC), plus INT.
- Movies: FMV `*_FRA` 18, `*_INT` 15. `FMV_a5otext_ESN` is a stray.
- Engine ini: `Language=int`.
- **The DLC languages follow AssetTools' extraction** (added to DLC_RE.md).

## 3. GFx (C, all 38 extracted movies scanned)
- **There's no language row in any movie,** SettingsMenu_GFX included. The original has no in-game language option; it's the
  dashboard setting only.
- **`Game.GetLanguageCode` is declared by every movie** (the shared HmExternalInterface `Game` stub list), but **only
  FrontEnd_GFX uses it.**
  - The press-start logo: `LanguageCode = GetLanguageCode()`, `RegionCode = GetRegionCode()`.
  - Bitmaps: `TransformersWFCLogoTM_` + upper(code) when the region is "NA", else `TransformersWFCLogo_` + upper(code). **H:** the
    exact branch order.
  - Fallback: `..._INT`.
  - Only INT and FRA bitmaps exist.
- No font, layout or offset branches on language were found.
