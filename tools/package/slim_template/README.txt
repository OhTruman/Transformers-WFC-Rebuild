WFC Rebuild playtest build (09c 37eec59, Release + PGO)

1. Copy this whole folder to the laptop (any drive, e.g. D:\WFC_Playtest).
2. Double-click Play.bat (always start the game this way, not by double-clicking wfc_rebuild.exe or through a
   shortcut). It maps the folder to a free drive letter for the session (F: when free, otherwise the first free
   letter from W: down), points the game's data at it, starts this folder's game, and removes the mapping when
   the game exits.
3. Logs: Transformers Rebuild\Rebuild\build\release\bin\wfc.log (crash reports wfc_crash_*.txt / .dmp beside it).
   Each start overwrites wfc.log: copy it before playing again if something went wrong.

Settings are saved in that bin folder (wfc_profile.ini). Requires a GPU with OpenGL 4.5+.

Online play
- The first time you host or join, Windows may ask whether wfc_rebuild.exe may use the network: allow it (private networks
  are enough for LAN play; allow public too for internet play).
- LAN: games on the same network show up by themselves. Internet: the host's game shows a 6-letter code; the others join with
  it. Matches run directly between the players' PCs; if two PCs cannot reach each other, the game switches to a relay on its
  own after a few seconds.
- The lobby server the codes and Quick Match use is set in Transformers Rebuild\Rebuild\data\frontend\online_service.json
  (empty = LAN and direct addresses only). A LobbyService= line in wfc_profile.ini overrides it.
