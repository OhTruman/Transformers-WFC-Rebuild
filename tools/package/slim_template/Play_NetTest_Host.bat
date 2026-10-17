@echo off
rem WFC Rebuild - two-PC network test, HOST side (PC A). Package 7 (online phase 1): a background bot match over your LAN, no
rem picture - it checks that both PCs simulate the same match, tick by tick, and that voice (your microphone) flows both ways.
rem Start this on PC A first, then Play_NetTest_Join.bat on PC B within 3 minutes. Results: NetTest_results\ beside this file.
setlocal
set "PKG=%~dp0"
if "%PKG:~-1%"=="\" set "PKG=%PKG:~0,-1%"
echo.
echo  This PC's network addresses (give the IPv4 one to PC B):
ipconfig | findstr /i "IPv4"
echo.
set /p "PEER=Type PC B's IPv4 address (from ipconfig on PC B) and press Enter: "
if not defined PEER ( echo No address typed. & pause & exit /b 1 )
set "L="
for %%D in (F W V U T S R Q P O N M L K J I H G) do if not defined L if not exist %%D:\ set "L=%%D"
if not defined L ( echo No free drive letter to map the game's folder to. & pause & exit /b 1 )
subst %L%: "%PKG%"
if errorlevel 1 ( echo Could not map drive %L%:. & pause & exit /b 1 )
set "R=%L%:/Transformers Rebuild"
set "WFC_ASSETS=%R%/ExtractedAssets/VerticalSlice"
set "WFC_ASSET_ROOT=%R%/ExtractedAssets/VerticalSlice"
set "WFC_CONTENT=%R%/ExtractedAssets/content"
set "WFC_RENDER_DATA=%R%/Rebuild/work/render"
set "WFC_MANIFESTS=%R%/AssetTools/manifests"
set "WFC_FRONTEND_MANIFESTS=%R%/AssetTools/manifests"
set "WFC_ASSETTOOLS_MANIFESTS=%R%/AssetTools/manifests"
set "WFC_DDS_ROOT=%R%/AssetTools/out/dds"
set "WFC_FRONTEND_DATA=%R%/Rebuild/data/frontend"
rem the match: the same on both PCs (the launch hash must agree), 5 minutes of Streets TDM with bots
set "WFC_NULLRENDER=1"
set "WFC_MUTE=1"
set "WFC_AUTODETECT=0"
set "WFC_SEED=7"
set "WFC_FLOWSEED=7"
set "WFC_SKIPINTRO=1"
set "WFC_PLAYERBOT=1"
set "WFC_MATCH_SECONDS=300"
set "WFC_MATCH_URL=MP_IAC_Streets?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2?PointsToWin=1000"
rem the network: seat 0 (host), UDP port 14000 on both PCs, a state hash every tick, the recording, microphone voice
set "WFC_NET_SLOT=0"
set "WFC_NET_SEATS=2"
set "WFC_NET_BIND=0.0.0.0:14000"
set "WFC_NET_PEERS=1=%PEER%:14000"
set "WFC_NET_HASH_EVERY=1"
set "WFC_NET_PACE=1"
set "WFC_NET_TIMEOUT_S=60"
set "WFC_NET_VOICE=mic"
set "OUT=%PKG%\NetTest_results"
if not exist "%OUT%" mkdir "%OUT%"
set "WFC_NET_LOG=%OUT%\net.csv"
set "WFC_NET_RECORD=%OUT%\record.bin"
echo.
echo  Running the HOST side (about 6 minutes, nothing is shown). Allow wfc_rebuild.exe on Private networks if Windows asks.
echo  Your microphone is used while it runs (Windows shows the microphone icon). Do not close this window.
pushd "%OUT%"
start "" /wait "%L%:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe"
set "RC=%errorlevel%"
popd
subst %L%: /d >nul 2>&1
echo.
echo  Finished: exit code %RC% (0 = both PCs stayed identical to the end).
echo  Send back the folder: %OUT%
pause
endlocal
