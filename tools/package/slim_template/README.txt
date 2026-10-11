WFC Rebuild playtest build (09c 37eec59, Release + PGO)

1. Copy this whole folder to the laptop (any drive, e.g. D:\WFC_Playtest).
2. Double-click Play.bat. It maps the folder to drive letter F: for the session (the game's data paths
   are F:\Transformers Rebuild\...), starts the game, and removes the mapping when the game exits.
   If the laptop already uses drive letter F:, Play.bat stops and says so.
3. Logs: Transformers Rebuild\Rebuild\build\release\bin\wfc.log (crash reports wfc_crash_*.txt / .dmp beside it).

Settings are saved in that bin folder (wfc_profile.ini). Requires a GPU with OpenGL 4.5+.
