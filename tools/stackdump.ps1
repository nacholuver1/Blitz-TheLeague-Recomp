# Launches blitz.exe, waits, attaches lldb to dump all thread stacks, then kills it.
# Usage: powershell -File stackdump.ps1 [-Config RelWithDebInfo] [-Wait 12] [-Out $(Split-Path $PSScriptRoot -Parent)\work\stacks.txt]
param(
  [string]$Config = "RelWithDebInfo",
  [int]$Wait = 12,
  [string]$Out = "$(Split-Path $PSScriptRoot -Parent)\work\stacks.txt",
  [string]$ExtraArgs = ""
)
$dir = "$(Split-Path $PSScriptRoot -Parent)\blitz\out\build\win-amd64\$Config"
$launchArgs = @{}; if ($ExtraArgs) { $launchArgs.ArgumentList = $ExtraArgs }
$p = Start-Process -FilePath "$dir\blitz.exe" @launchArgs -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $Wait
$cmds = @(
  "-o", "thread list",
  "-o", "thread backtrace all -c 25",
  "-o", "detach",
  "-o", "quit"
)
& "C:\Program Files\LLVM\bin\lldb.exe" -b -p $p.Id @cmds *> $Out
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
"pid=$($p.Id) lines=$((Get-Content $Out).Count)"
