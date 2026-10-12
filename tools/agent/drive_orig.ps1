param([string]$Steps)
$script:t0 = Get-Date
# Sends key input to the foreground CS2D window. Steps: "down:W", "up:W", "tap:2", "wait:1500", "focus"
Add-Type @'
using System; using System.Runtime.InteropServices;
public class K {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
}
'@
function Key([string]$name, [bool]$down) {
  $vk = [byte][char]$name.ToUpper()
  $scan = [byte][K]::MapVirtualKey($vk, 0)
  $flags = 0; if (-not $down) { $flags = 2 }
  [K]::keybd_event($vk, $scan, $flags, [UIntPtr]::Zero)
}
foreach ($s in $Steps.Split(';')) {
  $p = $s.Split(':')
  switch ($p[0]) {
    'focus' { $p2 = Get-Process CS2D -ErrorAction SilentlyContinue | Select-Object -First 1; if ($p2) { [K]::SetForegroundWindow($p2.MainWindowHandle) | Out-Null } }
    'down'  { Key $p[1] $true }
    'up'    { Key $p[1] $false }
    'tap'   { Key $p[1] $true; Start-Sleep -Milliseconds 80; Key $p[1] $false }
    'wait'  { Start-Sleep -Milliseconds ([int]$p[1]) }
    'mouse' { $w = Get-Process CS2D | Select-Object -First 1; $r = New-Object K+RECT; [K]::GetWindowRect($w.MainWindowHandle, [ref]$r) | Out-Null; [K]::SetCursorPos($r.L + 3 + [int]$p[1], $r.T + 26 + [int]$p[2]) | Out-Null }
    'click' { for ($i = 0; $i -lt [int]$p[1]; $i++) { [K]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [K]::mouse_event(4,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds ([int]$p[2]) } }
    'shot' { Add-Type -AssemblyName System.Drawing; $w = Get-Process CS2D | Select-Object -First 1; $r = New-Object K+RECT; [K]::GetWindowRect($w.MainWindowHandle, [ref]$r) | Out-Null; $bmp = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T); $g=[System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size); $bmp.Save($p[1] + ':' + $p[2]); $g.Dispose(); $bmp.Dispose() }
    'ldown' { [K]::mouse_event(2,0,0,0,[UIntPtr]::Zero) }
    'lup' { [K]::mouse_event(4,0,0,0,[UIntPtr]::Zero) }
    'wheel' { $d = [int64]([int]$p[1] * 120); if ($d -lt 0) { $d += 4294967296 }; [K]::mouse_event(0x800,0,0,[uint32]$d,[UIntPtr]::Zero) }
    'vk'    { $vk = [byte]([Convert]::ToInt32($p[1], 16)); $scan = [byte][K]::MapVirtualKey($vk, 0); $fl = 0; if ($p[2] -eq 'up') { $fl = 2 }; [K]::keybd_event($vk, $scan, $fl, [UIntPtr]::Zero) }
  }
  Write-Output ("{0:N3} {1}" -f ((Get-Date) - $script:t0).TotalSeconds, $s)
}
