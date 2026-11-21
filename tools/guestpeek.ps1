# Launches blitz.exe, waits, then reads big-endian guest memory with lldb.
# Guest address A lives at host 0x100000000 + A.
# Usage: powershell -File guestpeek.ps1 -Wait 12
param(
  [string]$Config = "RelWithDebInfo",
  [int]$Wait = 12
)
$lldb = "C:\Program Files\LLVM\bin\lldb.exe"
$dir = "$(Split-Path $PSScriptRoot -Parent)\blitz\out\build\win-amd64\$Config"
$p = Start-Process -FilePath "$dir\blitz.exe" -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $Wait

function Read-Guest([uint64]$addr, [int]$count) {
  $host_addr = "0x{0:X}" -f (0x100000000 + $addr)
  $out = & $lldb -b -p $p.Id -o "memory read -s1 -c $count -f x $host_addr" -o "detach" -o "quit" 2>&1 | Out-String
  $bytes = [regex]::Matches(($out -split "`n" | Where-Object { $_ -match "^0x[0-9a-f]+:" }) -join " ", "0x([0-9a-f]{2})\b") |
    ForEach-Object { [Convert]::ToByte($_.Groups[1].Value, 16) }
  return ,$bytes
}
function BE32($b, [int]$o) { return ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o+1] -shl 16) -bor ([uint32]$b[$o+2] -shl 8) -bor [uint32]$b[$o+3] }

$g = Read-Guest 0x82D34FE0 16
$obj = BE32 $g 4
"[0x82D34FE4] object        = 0x{0:X8}" -f $obj
"[0x82D34FE8] callback      = 0x{0:X8}" -f (BE32 $g 8)
$t = Read-Guest 0x828525C8 4
"[0x828525C8] special tid   = 0x{0:X8}" -f (BE32 $t 0)

if ($obj -ne 0) {
  $o = Read-Guest $obj 0x48
  for ($i = 0; $i -lt 0x48; $i += 4) { "  obj+0x{0:X2} = 0x{1:X8}" -f $i, (BE32 $o $i) }
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
