# Launches blitz.exe, waits, presses a key in its window, waits, screenshots, kills it.
# Usage: powershell -File screenshot_key.ps1 -Wait 30 -Key "{F3}" -Out $(Split-Path $PSScriptRoot -Parent)\work\shot_f3.png
param(
  [string]$Config = "Release",
  [int]$Wait = 30,
  [string]$Key = "{F3}",
  [string]$Out = "$(Split-Path $PSScriptRoot -Parent)\work\shot_key.png",
  [string]$ExtraArgs = "",
  [int]$After = 3
)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win2 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
$dir = "$(Split-Path $PSScriptRoot -Parent)\blitz\out\build\win-amd64\$Config"
$launchArgs = @{}; if ($ExtraArgs) { $launchArgs.ArgumentList = $ExtraArgs }
$p = Start-Process -FilePath "$dir\blitz.exe" @launchArgs -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $Wait
$p.Refresh()
$h = $p.MainWindowHandle
[Win2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
# Send real key down/up events so SDL sees them. -Key takes a comma-separated
# list of names: F1..F12, Down, Up, Left, Right, Enter, Esc.
$vk = @{ "{F1}"=0x70; "{F3}"=0x72; "{F4}"=0x73; "Down"=0x28; "Up"=0x26; "Left"=0x25; "Right"=0x27; "Enter"=0x0D; "Esc"=0x1B }
foreach ($k in $Key.Split(",")) {
  $code = [byte]$vk[$k.Trim()]
  # Arrow keys need KEYEVENTF_EXTENDEDKEY (1) or Windows sends them as numpad keys.
  $ext = if ($code -ge 0x25 -and $code -le 0x28) { 1 } else { 0 }
  [Win2]::keybd_event($code, 0, $ext, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80
  [Win2]::keybd_event($code, 0, $ext -bor 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 600
}
Start-Sleep -Seconds $After
$r = New-Object Win2+RECT
[Win2]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[Win2]::PrintWindow($h, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"saved $Out ($w x $ht)"
Stop-Process -Id $p.Id -Force
