# Resize a process's main window 3 times without activating it (SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE): 1920x1080, 1600x900,
# back to the original size; 4 s apart. For the presenter-fix verification (presenter-verify.sh step 3).
param([Parameter(Mandatory)][int]$ProcessId)
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int hh, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
}
"@
$p = Get-Process -Id $ProcessId -ErrorAction Stop; $h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { "resize: pid $ProcessId has no main window"; exit 1 }
$r = New-Object Win+RECT; [void][Win]::GetWindowRect($h, [ref]$r); $w0 = $r.R - $r.L; $h0 = $r.B - $r.T
$f = 0x0010 -bor 0x0004 -bor 0x0002   # NOACTIVATE | NOZORDER | NOMOVE
foreach ($s in @(@(1920, 1080), @(1600, 900), @($w0, $h0))) {
  $ok = [Win]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, $s[0], $s[1], $f); "$(Get-Date -Format HH:mm:ss) resize pid $ProcessId -> $($s[0])x$($s[1]) $ok"; Start-Sleep 4
}
