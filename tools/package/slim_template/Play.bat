@echo off
rem WFC Rebuild playtest (09c b898c75 slim). Maps this folder to a free drive letter for the session (subst; F: when it is
rem free, as the game's built-in data paths are F:\Transformers Rebuild\...), points the game's data roots at that letter
rem (the WFC_* variables, as verify_slim runs it), then starts THIS package's game from its own folder, so the log
rem (wfc.log) and the saved settings (wfc_profile.ini) land beside it in Transformers Rebuild\Rebuild\build\release\bin.
setlocal
set "PKG=%~dp0"
if "%PKG:~-1%"=="\" set "PKG=%PKG:~0,-1%"
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
set "WFC_CAMPAIGN_ROOT=%R%/AssetTools/out/campaign_render"
pushd "%L%:\Transformers Rebuild\Rebuild\build\release\bin"
start "" /wait wfc_rebuild.exe
popd
subst %L%: /d >nul 2>&1
endlocal
