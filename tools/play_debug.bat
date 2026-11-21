@echo off
rem Developer run: the RelWithDebInfo build with debug symbols and info-level log
rem flushing (build it with tools\build_blitz.bat relwithdebinfo). Uses the game
rem folder in the project root. If it crashes, the details land in:
rem   blitz\out\build\win-amd64\RelWithDebInfo\logs\  (newest blitz_*.log)
rem   work\play_stderr.txt                             (crash-report fallback)
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if not exist "%ROOT%\work" mkdir "%ROOT%\work"
cd /d "%ROOT%\blitz\out\build\win-amd64\RelWithDebInfo"
blitz.exe "--title.game_root=%ROOT%\game" --log.flush_level=info %* > "%ROOT%\work\play_stdout.txt" 2> "%ROOT%\work\play_stderr.txt"
echo Game exited with code %ERRORLEVEL%
echo %ERRORLEVEL% > "%ROOT%\work\play_exitcode.txt"
pause
