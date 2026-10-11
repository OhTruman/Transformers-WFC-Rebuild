@echo off
rem WFC Rebuild playtest (09c b898c75 slim). The game reads its data from F:\Transformers Rebuild\...,
rem so this maps this folder to drive F: for the session (subst), then starts the game.
setlocal
set "PKG=%~dp0"
if "%PKG:~-1%"=="\" set "PKG=%PKG:~0,-1%"
if exist "F:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe" goto run
if exist F:\ (
  echo Drive F: is already in use on this PC, so the game's data paths cannot be mapped.
  echo Free up the letter F: ^(Disk Management^) or tell Claude to build a version with configurable paths.
  pause
  exit /b 1
)
subst F: "%PKG%"
if errorlevel 1 ( echo Could not map drive F:. & pause & exit /b 1 )
:run
pushd "F:\Transformers Rebuild\Rebuild\build\release\bin"
start "" /wait wfc_rebuild.exe
popd
subst F: /d >nul 2>&1
endlocal
