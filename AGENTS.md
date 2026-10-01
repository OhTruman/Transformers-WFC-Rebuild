# Agent workspace rules

Read WORKTREES.md before working. Stay inside the folder in which this Claude session was launched. Only one active agent may edit or build in any one worktree. Each worktree owns its build/, work/, and .toolchain/ directories. Do not write into another worktree or the integration tree from an agent session.

F:\Transformers Rebuild\ExtractedAssets and F:\Transformers Rebuild\Game Dump are shared original resources: read only, never modify, delete, rename, or extract into them. Treat AssetTools, the original WFC-Rebuild Ghidra projects, and ghidra_12.1.4_PUBLIC as read-only references during parallel agent work. Reverse-engineering exports and new project copies belong in your own work/ folder. Never open the same writable Ghidra project from two sessions.

Use .\build.ps1 -Jobs 2 for the current CMake source tree; all output stays in your own build/. Limit build jobs further if four sessions saturate the machine. build-direct.ps1 is a preserved legacy fallback whose source paths do not match the current tree; do not assume it builds the current reconstruction.

Commit your own scoped changes to your assigned branch. Do not switch to main, merge, rebase, reset, clean, or modify shared Git configuration from worker sessions. The designated integration owner merges committed work serially in Rebuild after coordinating with workers. Git worktrees share history and refs; source files and build output are separate. Parallel changes can still produce merge conflicts.