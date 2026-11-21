# Launches the packaged blitz.exe with no arguments, screenshots the launcher,
# presses Enter (START GAME), screenshots the game, then reports what it used.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class P {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
function Shot($h, $out) {
  $r = New-Object P+RECT; [P]::GetWindowRect($h, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
  [P]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
  $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
}
$dir = Join-Path (Split-Path $PSScriptRoot -Parent) "BlitzPC"
$work = Join-Path (Split-Path $PSScriptRoot -Parent) "work"
$p = Start-Process -FilePath "$dir\blitz.exe" -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 8
$p.Refresh(); $h = $p.MainWindowHandle
Shot $h "$work\pkg_launcher.png"
[P]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 500
[P]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [P]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Seconds 30
Shot $h "$work\pkg_game.png"
Stop-Process -Id $p.Id -Force
Start-Sleep -Seconds 1
"--- folder after run"; Get-ChildItem $dir -Name
"--- cache"; Get-ChildItem "$dir\cache" -Recurse -File -ErrorAction SilentlyContinue | Measure-Object | ForEach-Object { "$($_.Count) cache files" }
"--- log"; Get-ChildItem "$dir\logs" | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { Get-Content $_.FullName | Select-String -Pattern "blitz:|no game folder| E " | Select-Object -First 10 }
"--- blitz.toml"; Get-Content "$dir\blitz.toml"
