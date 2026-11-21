@echo off
rem Runs Blitz with debug symbols and crash capture (developer build).
rem If it crashes, the details land in:
rem   blitz\out\build\win-amd64\RelWithDebInfo\logs\  (newest blitz_*.log)
rem   work\play_stderr.txt                             (crash-report fallback)
set "ROOT=%~dp0"
if not exist "%ROOT%work" mkdir "%ROOT%work"
cd /d "%ROOT%blitz\out\build\win-amd64\RelWithDebInfo"
blitz.exe "--title.game_root=%ROOT%game" --log.flush_level=info > "%ROOT%work\play_stdout.txt" 2> "%ROOT%work\play_stderr.txt"
echo Game exited with code %ERRORLEVEL%
echo %ERRORLEVEL% > "%ROOT%work\play_exitcode.txt"
pause
