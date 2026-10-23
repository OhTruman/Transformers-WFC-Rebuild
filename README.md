# WFC Clean-Room Rebuild
An original Windows graybox gameplay prototype.

Build and verify from PowerShell (in F:\Transformers Rebuild\Rebuild):
    .\build.ps1 -Configuration Debug -Test
    .\build.ps1 -Configuration Debug -Run

Or run build.cmd -Test from Command Prompt.

The scripts use the portable Clang, CMake and Ninja tools already in .toolchain. No downloads or installs are required.
Executable: build\Debug\WFCRebuild.exe. Logs and smoke-frame.bmp are written beside the executable.

Click to capture the mouse. WASD moves, mouse looks, Space jumps, T transforms, R respawns, Esc releases capture and F10 exits.

See docs/ARCHITECTURE.md, ROADMAP.md, MILESTONES.md and BUILD_REPORT.md for scope and verification.

## Verified fallback on this host
Ninja subprocess execution stalls in this execution environment. The runnable build was verified with:
    .\build-direct.ps1 -Configuration Debug -Test
    .\build-direct.ps1 -Configuration Debug -Run
The CMake-generated commands also built successfully when executed serially, and CTest passed 2/2 in build\NinjaDiagnostic. See docs/BUILD_REPORT.md for the exact limitation and full verification details.

## Third-party fetches (gitignored)

- `tools/net/fetch_opus.py` puts Opus 1.5.2 in `third_party/opus`. Without it the build has no voice chat (`WFC_OPUS` unset): `WFC_VOICETEST` fails at its first check and `refresh.sh` refuses to package. Fetch it before any package build.
- `third_party/dxc` + `third_party/ffx_denoiser`: the ray-tracing row silently does nothing without them (check `WFC_RT_SHADERS=1` in build.ninja).
