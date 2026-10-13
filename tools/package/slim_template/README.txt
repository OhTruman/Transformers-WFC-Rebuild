WFC Rebuild playtest build (09c 37eec59, Release + PGO)

1. Copy this whole folder to the laptop (any drive, e.g. D:\WFC_Playtest).
2. Double-click Play.bat (always start the game this way, not by double-clicking wfc_rebuild.exe or through a
   shortcut). It maps the folder to a free drive letter for the session (F: when free, otherwise the first free
   letter from W: down), points the game's data at it, starts this folder's game, and removes the mapping when
   the game exits.
3. Logs: Transformers Rebuild\Rebuild\build\release\bin\wfc.log (crash reports wfc_crash_*.txt / .dmp beside it).
   Each start overwrites wfc.log: copy it before playing again if something went wrong.

Settings are saved in that bin folder (wfc_profile.ini). Requires a GPU with OpenGL 4.5+.
