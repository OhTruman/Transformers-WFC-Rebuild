# Language option (Frontend plan, next build)

User 2026-10-23 ~20:35 (via Integration): a Language option in Settings covering every language the dump and the DLC
packs ship; the GFx text, subtitles and dialog banks switch where the data exists; INT is the default; the choice
survives map changes and is kept in the profile. Planned during the final-gate freeze (no compute); code after RELEASE.

## 1. What the data ships (read-only survey, 2026-10-23)

| Kind | INT | FRA | Other |
|---|---|---|---|
| Text: `ExtractedAssets/config/Coalesced_int/.../INT/*.int` | 18 files | `Coalesced_FRA/.../FRA/*.fra`: 17 files (no `UIText_PC.fra`) | none |
| Map / chapter names: AssetTools manifests `FriendlyName` | `["INT"]` | `["FRA"]` (already read as `friendlyNameFra`) | none |
| Dialogue banks: `ExtractedAssets/content/WL_DX_*` | 41 `_INT` | 42 `_FRA` | 53 language-neutral |
| Per-level localized packages: `Game Dump/.../CookedXenon/*_LOC_*.xxx` | (in the base packages) | 213 `_LOC_FRA` | none |
| Movies: `ExtractedAssets/movies` | 15 `_INT` (incl. `TF_InitialStartup_NA/EU_INT`) | 18 `_FRA` (the `FMV_*text` title cards) | 1 `_ESN` (`FMV_a5otext_ESN`, a stray) |
| Logo textures: `UI_GFxFrontEnd_p`, `UI_TransformersWFCLogo_p` | `TransformersWFCLogo(TM).png` | `..._FRA.png` | none |

So the base game is **INT + FRA**. The one ESN movie has no text pack, subtitles or dialogue behind it, so it isn't offered.
The DLC packs (STFS, not extracted yet) may add languages; AssetTools' inventory decides (DLC_PLAN.md step 2), and the
row is data-driven so a new language appears without code.

## 2. Where INT is hard-coded today (Frontend)

- `Catalog::load`: the text path `Coalesced_int/TransGame/Localization/INT/<file>.int` (and `_360.int`), and the manifests'
  `FriendlyName["INT"]` / playlist `DisplayName["INT"]`.
- `pc_strings.int` (PC-only strings): INT only.
- `FrontendRuntime::localizedMovie`: tries `<name>_NA_INT`, then `<name>_INT`, then `<name>`.
- `Game.GetLanguageCode` returns `"INT"`. The movies query it (e.g. the FRA logo variants).

## 3. Design

1. **Profile:** `[PCSettings] Language=INT|FRA`, written only when not INT (default profiles unchanged).
2. **Languages offered:** a language counts as available only if its text pack exists (`Coalesced_<lang>/.../<LANG>/`).
   The catalog scans the config folder at load, so a DLC language added there shows up without code.
3. **Catalog:**
   - `Catalog::setLanguage(code)` reloads the localization table from `Coalesced_<code>/.../<CODE>/*.<code>`, falling back
     key by key to INT (e.g. everything in `UIText_PC`, which FRA lacks).
   - Map, chapter and playlist names come from the manifest's `[code]` entry, else INT.
   - `pc_strings.<code>` when present, else `pc_strings.int`. I'll write a French `pc_strings.fra` for our PC-only rows,
     marked as a PC translation.
4. **Movies:** `localizedMovie` tries `<name>_NA_<CODE>`, `<name>_<CODE>`, then the INT forms, then `<name>`.
5. **GFx:**
   - `Game.GetLanguageCode` returns the profile language.
   - Text resolves through the catalog when a movie opens, so the open movies are reopened after a change: Settings
     re-opens itself on Apply, and the other screens pick it up when they next open.
   - Logo textures: `TransformersWFCLogo*_FRA.png` swapped in for FRA if the movies' image list carries both variants
     (to check: the logo may be chosen inside the movie by GetLanguageCode).
6. **Subtitles:** `Subtitles.<code>` through the same catalog (the subtitle lookup already goes through `localize`).
7. **Dialogue (Systems):**
   - The frontend tells the audio side the language (`IFrontendAudio::setLanguage(code)`, or an env / profile read on
     Systems' side; Systems chooses).
   - Systems loads `WL_DX_<bank>_<CODE>` banks (and the `_LOC_<CODE>` level data if AssetTools extracts it), falling back
     to INT.
   - Any renderer / audio setter joins `release()`'s keep list so the language survives map changes (memory rule:
     settings-survive-map-change).
8. **Settings row:**
   - A PC EXTENSION row "Language" (Display page, as the other PC rows), with values named in their own language
     ("English" / "Français").
   - Applied on Apply. A change mid-match takes effect on the next map load for dialogue banks; text applies at once.
9. **Tests:**
   - `settings.language`: catalog FRA loads; a known UIText key in French; an INT fallback for a `UIText_PC` key;
     profile round trip; GetLanguageCode.
   - One headless check per the broad-stroke rule. No card run unless Integration asks.

## 4. Owners

- **Frontend:** items 1-6, 8-9 above.
- **Systems:** item 7 (dialogue banks + the audio language setter + its keep-list entry).
- **AssetTools:** confirm whether the `_LOC_FRA` level packages hold anything beyond the dialogue already in
  `WL_DX_*_FRA` (subtitled cinematic text, signage textures); extract the DLC packs' languages.
- **RE:** how the original chose the language (360 dashboard locale → `GetLanguageCode`), and whether any GFx movie
  branches on it (logo, fonts).

## 5. Risks to check after RELEASE

- **Glyphs:** the GFx fonts must hold the French accented glyphs (é, è, à, ç, ê, ô, É). The shipped FRA build used the
  same font library, so they should be there; one Settings screenshot in FRA confirms it.
- **Longer French strings** may overflow fixed-width labels; the original FRA build had the same layouts, so it's
  expected to fit except for our PC-only rows (pc_strings.fra wording kept short).
