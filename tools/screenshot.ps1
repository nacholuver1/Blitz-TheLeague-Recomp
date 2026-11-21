# Launches blitz.exe, waits, screenshots its window, then (optionally) kills it.
# Usage: powershell -File screenshot.ps1 -Wait 45 -Out $(Split-Path $PSScriptRoot -Parent)\work\shot.png [-Keep]
param(
  [string]$Config = "Release",
  [int]$Wait = 45,
  [string]$Out = "$(Split-Path $PSScriptRoot -Parent)\work\shot.png",
  [switch]$Keep
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$dir = "$(Split-Path $PSScriptRoot -Parent)\blitz\out\build\win-amd64\$Config"
$p = Start-Process -FilePath "$dir\blitz.exe" -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $Wait
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { "no window"; if (-not $Keep) { Stop-Process -Id $p.Id -Force }; exit 1 }
$r = New-Object Win+RECT
[Win]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# PW_RENDERFULLCONTENT = 2 captures D3D12/Vulkan swapchain content
[Win]::PrintWindow($h, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"saved $Out ($w x $ht) pid=$($p.Id) title='$($p.MainWindowTitle)'"
if (-not $Keep) { Stop-Process -Id $p.Id -Force }
