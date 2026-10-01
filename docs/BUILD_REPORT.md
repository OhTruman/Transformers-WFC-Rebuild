# WFC Rebuild — Build Report

## Toolchain (portable, in `.toolchain/`, not committed)
- Compiler: llvm-mingw 20260922, clang++ 23.1.2, target `x86_64-w64-windows-gnu`
- Build system: CMake 4.4.3 + Ninja 1.13.2
- No system-wide installs; no Visual Studio required.

## How to build
```
powershell -ExecutionPolicy Bypass -File build.ps1          # configure + build (Debug)
powershell -ExecutionPolicy Bypass -File build.ps1 -Run     # build then launch
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean   # wipe build/ first
```
Output: `build/bin/wfc_rebuild.exe` (statically linked — self-contained, no mingw DLLs needed).

## Status (M1)
- Configure: OK
- Compile: OK (10 TUs, warnings-clean with -Wall -Wextra)
- Link: OK (static libc++/libunwind/pthread; libs: opengl32 gdi32 user32 xinput)
- Launch: OK — OpenGL 4.6 compatibility context created
- Runtime: 150-frame headless smoke test exits 0; interactive run stays alive & responding

## Notes / gotchas resolved
- `-D...=$Config` must be quoted in PowerShell or the value is passed literally.
- `UNICODE`/`_UNICODE` defined project-wide so `IDC_ARROW` etc. match the `...W` APIs.
- Static link (`-static`) required, else `STATUS_DLL_NOT_FOUND (0xC0000135)` at launch.
- `wfc.log` is written beside the exe so logs are capturable without a console.

## Controls
WASD move · mouse look · Space jump · F transform (robot/vehicle) · C free/capture cursor · Esc quit · gamepad supported.
