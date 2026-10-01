# Four-agent workspace

Integration: F:\Transformers Rebuild\Rebuild on main. Keep this tree idle while the four worker agents run; only the designated integrator merges and builds here.

| Folder under F:\Transformers Rebuild | Branch | Purpose |
| --- | --- | --- |
| Rebuild-Gameplay | agents/gameplay | Controls, facing, movement, animation |
| Rebuild-Rendering | agents/rendering | Lighting, shaders, rendering, materials |
| Rebuild-Systems | agents/systems | Weapons, audio, general gameplay systems |
| Rebuild-Experimental | agents/experimental | Experiments and reverse-engineering bridge |

Each worker has a private .toolchain copy, independently configured build directory, and private work directory. Experimental also has its own copy of the preexisting work data, including the Ghidra snapshot. No build caches were copied from integration: CMake caches contain absolute paths.

Original ExtractedAssets and Game Dump remain outside Git and all worktrees. Their read-only policy is documented in CLAUDE.md/AGENTS.md; Windows permissions were not changed. Existing folders and data were left in place.

A full pre-setup backup is at F:\Transformers Rebuild\Backups\Rebuild-before-worktrees-20261001. The initial baseline commit tracks source, documentation and scripts, excluding toolchains, builds and work scratch. The backup preserves those excluded files too. The origin remote is https://github.com/OhTruman/Transformers-WFC-Rebuild.git, as supplied by the user. No commits have been uploaded. Setup commits use the per-command identity Codex Local Setup <codex-local@localhost>; your global Git identity was not changed. Configure your own Git identity before authoring future commits if needed.

Open four separate PowerShell terminals and run one pair in each:

```powershell
Set-Location 'F:\Transformers Rebuild\Rebuild-Gameplay'
claude

Set-Location 'F:\Transformers Rebuild\Rebuild-Rendering'
claude

Set-Location 'F:\Transformers Rebuild\Rebuild-Systems'
claude

Set-Location 'F:\Transformers Rebuild\Rebuild-Experimental'
claude
```

Build in the worker folder: .\build.ps1 -Jobs 2
Inspect worktrees: git worktree list

Workers commit scoped changes on their assigned branches. Once ready and the integration tree is clean, the integration owner can merge one at a time, resolve conflicts, then build/test before the next merge:

```powershell
Set-Location 'F:\Transformers Rebuild\Rebuild'
git status
git merge --no-ff agents/gameplay
.\build.ps1 -Jobs 2
```

Repeat for the other branches only when their work is ready. Coordinate worker updates to main after integration; do not perform concurrent repository-wide maintenance. Four isolated trees prevent file/build collisions but do not guarantee low CPU consumption.
Setup validation: all five Git working trees are clean. All four worker CMake caches reference their own source, compiler and Ninja paths. Gameplay completed a full compile/link using build.ps1 -Jobs 2. The other three workers configured successfully; no interactive gameplay test was performed.
