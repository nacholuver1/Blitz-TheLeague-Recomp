# Tests the launcher's relaunch path with a throwaway config:
# Render resolution -> 2x, back up to START GAME, Enter. Expects a new blitz.exe
# process that skipped the launcher and renders at 2x.
$cfg = "$(Split-Path $PSScriptRoot -Parent)\work\test_launcher.toml"
Remove-Item $cfg -ErrorAction SilentlyContinue
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class K {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
function Press([byte]$vk) {
  $ext = if ($vk -ge 0x25 -and $vk -le 0x28) { 1 } else { 0 }
  [K]::keybd_event($vk, 0, $ext, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80
  [K]::keybd_event($vk, 0, $ext -bor 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 600
}
$dir = "$(Split-Path $PSScriptRoot -Parent)\blitz\out\build\win-amd64\Release"
$p = Start-Process -FilePath "$dir\blitz.exe" -ArgumentList "--app.config_file=`"$cfg`" --log.level=debug" -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 8
$p.Refresh(); [K]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 500
Press 0x28; Press 0x28; Press 0x27   # Down, Down, Right: render resolution -> 2x
Press 0x26; Press 0x26               # Up, Up: START GAME
Press 0x0D                           # Enter
Start-Sleep -Seconds 12
"original pid $($p.Id) exited: $($p.HasExited)"
$new = Get-Process blitz -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $p.Id }
"new blitz processes: $(@($new).Count) $(($new | ForEach-Object { $_.Id }) -join ',')"
"--- $cfg"
Get-Content $cfg -ErrorAction SilentlyContinue | Select-String -Pattern "resolution_scale|launcher|\[gpu\]|\[blitz\]"
$log = Get-ChildItem "$dir\logs" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
"--- newest log $($log.Name)"
Get-Content $log.FullName | Select-String -Pattern "relaunch|Module prepared|resolution_scale|scaled" | Select-Object -First 8
Get-Process blitz -ErrorAction SilentlyContinue | Stop-Process -Force
