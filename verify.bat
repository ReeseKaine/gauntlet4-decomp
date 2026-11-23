@echo off
rem verify.bat -- double-click to check the Gauntlet 4 C translation (runs verify.sh in Ubuntu/WSL).
rem Asks for a quick or full check, then shows the results. The full log is verify_log.txt.
echo Gauntlet 4 C translation check
echo.
echo   1 = quick check (a few minutes)
echo   2 = full check  (takes a long time; you can leave it running)
echo.
set /p choice=Type 1 or 2 and press Enter:
set mode=--quick
if "%choice%"=="2" set mode=
cd /d "%~dp0"
wsl bash verify.sh %mode%
echo.
echo Done. The results are above; the full log is verify_log.txt in this folder.
pause
