@echo off
REM Run Breaking Walls with user configuration file
setlocal
set BIN_DIR=%..%\build\libs
cd /d "%BIN_DIR%"

REM If installed, executable may be in bin folder
if exist "%BIN_DIR%jbreaking-walls-1.1.2.jar" (
    java -jar "%BIN_DIR%jbreaking-walls-1.1.2.jar" config.json
    goto :eof
)
if exist "%BIN_DIR%build\jbreaking-walls-1.1.2.jar" (
    java -jar "%BIN_DIR%build\jbreaking-walls-1.1.2.jar" config.json
    goto :eof
)
echo jbreaking-walls-1.1.2.jar not found!
exit /b 1

endlocal
