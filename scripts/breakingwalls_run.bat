@echo off
REM Run BreakingWalls with physics.json
setlocal
set BIN_DIR=%~dp0
cd /d "%BIN_DIR%"

REM If installed, executable may be in bin folder
if exist "%BIN_DIR%breakingwalls.exe" (
    "%BIN_DIR%breakingwalls.exe" physics.json
    goto :eof
)
if exist "%BIN_DIR%bin\breakingwalls.exe" (
    "%BIN_DIR%bin\breakingwalls.exe" physics.json
    goto :eof
)
echo breakingwalls.exe not found!
exit /b 1

endlocal
