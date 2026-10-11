# Long-path audit: narrow / MAX_PATH-limited file opens (Systems, 2026-10-11, 09c e307fc0)

Trigger: the first M3 verify_slim -Strict run (package tested in place at Rebuild-Systems\work\playtest_slim) had the game
reject the HD pack: "incomplete (3 of 2244 files missing / wrong size)". The 3 paths were 260-269 chars; the HD loader opens
them with a narrow std::ifstream, limited to MAX_PATH (260). Integration asked for an audit of narrow-path opens (sites
listed below); Rendering decides on the renderer ones.

## 1. How much headroom there is

Every file of the M3 package (51,782), as the game sees it under Play.bat's subst (F:\Transformers Rebuild\...):

| Tree | Longest path |
|---|---|
| AssetTools/out/dds (original DDS) | 214 |
| AssetTools/out/hd (HD pack) | 213 |
| ExtractedAssets/content | 166 |
| ExtractedAssets/VerticalSlice | 149 |
| Rebuild/work/render | 129 |
| everything else | <= 118 |

- The shipped package is safe: 214 < 260, 46 chars of headroom. Play.bat always maps the package to F:, wherever the user
  copies it.
- It breaks when the data root is about 46+ chars longer than "F:\Transformers Rebuild": +60 chars gives 13 paths >= 260 and
  +80 gives 86. Cases that do this: testing a package in place (this incident; verify_slim now has -SubstDrive), dev roots
  pointed somewhere deep (WFC_DDS_ROOT / WFC_HD_ROOT / WFC_CONTENT ...), and a future "configurable paths" package without
  subst installed under a user folder (e.g. C:\Users\<name>\Downloads\WFC Playtest\...).
- The longest paths are the texture trees (DDS + HD), not just HD: the original DDS loader has the same exposure.

## 2. What actually lifts the limit (Windows 10 / 11)

- Switching to wide APIs alone does NOT: wide CreateFileW / _wfopen are still limited to MAX_PATH unless (a) the path
  carries the \\?\ prefix, or (b) the exe's manifest declares longPathAware AND the machine's LongPathsEnabled policy is on
  (a registry setting we don't control on the user's PC).
- So the robust fix is (a): one helper that turns a data path into a wide \\?\ path, used at the sites below:
  `std::filesystem::path platform::longPath(const std::string& utf8)`: UTF-8 -> wide (MultiByteToWideChar CP_UTF8),
  GetFullPathNameW (\\?\ needs absolute, backslash-only, no '.' / '..'), prefix \\?\ (\\?\UNC\ for \\server paths) when
  the full path is >= ~240 chars (or always for absolute paths: simpler, same behaviour).
  std::ifstream / std::ofstream (libc++) and std::filesystem accept std::filesystem::path, so most sites change from
  `std::ifstream f(p, ...)` to `std::ifstream f(platform::longPath(p), ...)` with no other edits.
  _wfopen(platform::longPath(p).c_str(), L"rb") for FILE* sites.
- Also fixes non-ASCII paths (narrow opens use the ANSI code page; a user folder with non-ASCII characters breaks them).
- (b) as well: longPathAware in an exe manifest helps where the policy is on, but can't be relied on. The exe has no manifest
  today (no .rc / .manifest in the tree; DPI awareness is set at run time in Win32Window.cpp), so this means adding a small
  manifest resource; optional.
- Toolchain check: llvm-mingw's libc++ defines _LIBCPP_HAS_OPEN_WITH_WCHAR on Windows, so the std::filesystem::path stream
  constructors open through the wide path.

## 3. Sites (src/, 09c e307fc0)

Priority A: long data paths (asset-relative names under a data root). These are the ones that can exceed 260:

| Site | What | Owner |
|---|---|---|
| render/gl/WfcPipeline.cpp:1508 | HD index row check (ifstream, size) | Rendering |
| render/gl/WfcPipeline.cpp:1541 (decodeOriginalTop), :1578 | DDS / HD texture reads: ddsRoot() + E.dds, the longest paths (214) | Rendering |
| render/gl/WfcPipeline.cpp:1469 | HD index probe (root / pack) | Rendering |
| render/gl/WfcPipeline.cpp:37 (readText) | JSON under render data / HD root (+ :627, :629, :1247 probes) | Rendering |
| assets/Gltf.cpp:19 | glTF / GLB by path (content meshes) | Rendering |
| platform/win32/Win32FileCompression.cpp:49 (readAll), :94 | readDataFile / dataFileSize choke point: plain file then its .xpr twin (+4 chars). SkinnedModel and others go through it | Systems |
| platform/win32/Win32Audio.cpp:110 | WAV reads | Systems |
| game/SoundCues.cpp:314-315 (fopen), :493 / :496 / :556 / :559 (filesystem::file_size) | localized-wave twin probes, FSB bank sizes | Systems |
| platform/win32/FsbDecode.cpp:38 | libvgmstream libstreamfile_open_from_stdio(path) (a narrow fopen inside the library) | Systems |
| platform/win32/Win32Image.cpp:42 | WIC decoder from a wide path (UTF-8 widened correctly, no \\?\) | Systems |
| platform/win32/Win32Movie.cpp:22 | Media Foundation source from a wide path: widened byte-by-byte (not UTF-8), no \\?\ | Systems |
| ui/gfx/MovieDef.cpp:715 | SWF movies | Frontend / UI |
| frontend/FrontendRuntime.cpp:525 (filesystem::exists robot glTF), :1055 / :1068 / :1089 (movies .mkv) | content probes / movie paths | Frontend |

Priority B: data, fixed short names (manifests / config / map JSON; <= ~150 chars under F:). Only exposed when the root
itself is deep. Use the same helper for consistency, low urgency:
core/Application.cpp:921, 1368, 1407, 1734, 1984, 2055, 5747, 5881; frontend/Catalog.cpp:18, 24, 97, 112, 386, 421;
frontend/Characters.cpp:14, 59, 173; frontend/FrontendRuntime.cpp:257, 276; frontend/FrontendScene.cpp:87;
frontend/GraphicsAutoDetect.cpp:17; frontend/InputPrompts.cpp:26; frontend/Profile.cpp:22; frontend/DataPaths.cpp:35-36;
game/AmbientAudio.cpp:96, 125; game/BotNav.cpp:21; game/ChassisDef.cpp:17, 23; game/MapState.cpp:163, 210, 394;
game/Match.cpp:45; game/World.cpp:68, 420, 6615; platform/UiBindings.cpp:48; render/HudMarkers.cpp:32;
render/LightVisibilityVolume.cpp:31; render/gl/GLRenderer.cpp:432, 698, 810, 1171, 1473; render/gl/WfcMapFx.cpp:43;
render/gl/WfcMovers.cpp:89; ui/GfxHost.cpp:26, 46.

Priority C: no change. Writes to the current directory (the exe's bin) or to dev-tool paths from env variables:
core/Log.cpp:30 (wfc.log), core/AllocProf.cpp:247, core/CpuProf.cpp:170 / 297 / 298 / 334, core/PresentTrace.cpp:66 / 175,
core/FileLog.cpp:117, frontend/Profile.cpp:69 / 137 (wfc_profile.ini), frontend/Characters.cpp:158, frontend/FlowTrace.cpp:17,
frontend/FrontendRuntime.cpp:249, render/d3d12/D3D12Presenter.cpp:233, render/gl/GLRenderer.cpp:1101 / 1688,
render/gl/RenderWatchdog.cpp:60 / 124 / 134, render/gl/WfcMapFx.cpp:424, render/gl/WfcPipeline.cpp:543 / 1084 / 2768 / 6214 /
6367 / 6406, ui/gfx/Avm1.cpp:491 / 495.

Third-party: libvgmstream (above, via FsbDecode) is the only one that opens data files itself. Its stdio streamfile can be
replaced by one over a FILE* opened with _wfopen(longPath) (or a custom libstreamfile_t), keeping the library unmodified.

## 4. Proposal

1. Systems: add platform::longPath (+ a tiny test: a > 260-char path under work/ opens via ifstream, FILE* and
   file_size), use it in the Systems-owned A sites (readDataFile choke point, Win32Audio, SoundCues, FsbDecode via a
   wide-opened FILE*, Win32Image, Win32Movie with a proper UTF-8 widen). Optionally a manifest with longPathAware.
2. Rendering: A sites in WfcPipeline (DDS / HD / readText) and Gltf.cpp, through the same helper.
3. Frontend / Gameplay: B sites when convenient (one-line changes).
Proof: verify_slim on the M3 package in place (no -SubstDrive, the 269-char case) must PASS with "HD texture pack
available: 2244"; package FILELOG unchanged; byte-identical frames (presentation unaffected).
