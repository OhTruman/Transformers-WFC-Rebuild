@echo off
rem WFC Rebuild playtest (09c b898c75 slim). Maps this folder to a free drive letter for the session (subst; F: when it is
rem free, as the game's built-in data paths are F:\Transformers Rebuild\...), points the game's data roots at that letter
rem (the WFC_* variables, as verify_slim runs it), then starts THIS package's game from its own folder, so the log
rem (wfc.log) and the saved settings (wfc_profile.ini) land beside it in Transformers Rebuild\Rebuild\build\release\bin.
setlocal
set "PKG=%~dp0"
if "%PKG:~-1%"=="\" set "PKG=%PKG:~0,-1%"
set "L="
set "REUSE="
rem A mapping left by an earlier session (a crash, or a package deleted while mapped): reuse the one that is this folder,
rem drop the ones whose folder is gone ("if not exist X:\" counts such a letter as free, and subst then fails on it).
for /f "tokens=1" %%A in ('subst 2^>nul ^| findstr /i /e /c:"=> %PKG%"') do if not defined L ( set "L=%%~dA" & set "REUSE=1" )
if defined L set "L=%L:~0,1%"
for /f "tokens=1,2,*" %%A in ('subst 2^>nul') do if not exist "%%C\" subst %%~dA /d >nul 2>&1
for %%D in (F W V U T S R Q P O N M L K J I H G) do if not defined L if not exist %%D:\ set "L=%%D"
if not defined L ( echo No free drive letter to map the game's folder to. & pause & exit /b 1 )
if not defined REUSE subst %L%: "%PKG%"
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
set "WFC_CAMPAIGN_ROOT=%R%/AssetTools/out/campaign_render"
set "WFC_DLC_TEXT=%R%/AssetTools/work/dlc"
set "WFC_DLC_CONTENT=%R%/ExtractedAssets/content"
set "WFC_BASE_CONTENT=%R%/ExtractedAssets/content"
pushd "%L%:\Transformers Rebuild\Rebuild\build\release\bin"
start "" /wait wfc_rebuild.exe
popd
subst %L%: /d >nul 2>&1
endlocal
