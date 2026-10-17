# Nonpaged / paged pool usage per tag (poolmon-style, no admin, no WDK): NtQuerySystemInformation(SystemPoolTagInformation = 22).
#   powershell -File tools/fidelity/pooltags.ps1 [-Top 15] [-Csv <append file>] [-Label <text>]
# Prints the top nonpaged tags by bytes; with -Csv appends one row per tag (time, label, tag, nonpaged bytes, allocs - frees, paged bytes)
# so two snapshots can be differenced per operation (tools/fidelity/pooltags_diff.py).
param([int]$Top = 15, [string]$Csv = "", [string]$Label = "")
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class PoolTags {
  [DllImport("ntdll.dll")] public static extern int NtQuerySystemInformation(int c, IntPtr b, int l, out int r);
}
"@
$len = 1 << 20; $buf = [IntPtr]::Zero
for ($i = 0; $i -lt 6; $i++) {
  $buf = [Runtime.InteropServices.Marshal]::AllocHGlobal($len); $ret = 0
  $st = [PoolTags]::NtQuerySystemInformation(22, $buf, $len, [ref]$ret)
  if ($st -eq 0) { break }
  [Runtime.InteropServices.Marshal]::FreeHGlobal($buf); $buf = [IntPtr]::Zero; $len = [Math]::Max($ret, $len * 2)
}
if ($buf -eq [IntPtr]::Zero) { Write-Error "NtQuerySystemInformation(22) failed: 0x$('{0:X8}' -f $st)"; exit 1 }
# SYSTEM_POOLTAG_INFORMATION: ULONG Count; (pad to 8 on x64) SYSTEM_POOLTAG[] - Tag(4) PagedAllocs(4) PagedFrees(4) PagedUsed(SIZE_T)
#   NonPagedAllocs(4) NonPagedFrees(4) NonPagedUsed(SIZE_T): 40 bytes per entry on x64
$n = [Runtime.InteropServices.Marshal]::ReadInt32($buf, 0); $rows = New-Object System.Collections.Generic.List[object]
for ($k = 0; $k -lt $n; $k++) {
  $o = 8 + 40 * $k; $tb = New-Object byte[] 4; [Runtime.InteropServices.Marshal]::Copy([IntPtr]($buf.ToInt64() + $o), $tb, 0, 4)
  $tag = -join ($tb | % { if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { '.' } })
  $pa = [Runtime.InteropServices.Marshal]::ReadInt32($buf, $o + 4); $pf = [Runtime.InteropServices.Marshal]::ReadInt32($buf, $o + 8)
  $pu = [Runtime.InteropServices.Marshal]::ReadInt64($buf, $o + 16)
  $na = [Runtime.InteropServices.Marshal]::ReadInt32($buf, $o + 24); $nf = [Runtime.InteropServices.Marshal]::ReadInt32($buf, $o + 28)
  $nu = [Runtime.InteropServices.Marshal]::ReadInt64($buf, $o + 32)
  $rows.Add([pscustomobject]@{ Tag = $tag; NonPaged = $nu; NpOutstanding = [int64]$na - [int64]$nf; Paged = $pu })
}
[Runtime.InteropServices.Marshal]::FreeHGlobal($buf)
$t = Get-Date -Format "yyyy-MM-ddTHH:mm:ss"
if ($Csv) {
  if (-not (Test-Path $Csv)) { "time,label,tag,nonpaged,np_outstanding,paged" | Out-File -Encoding utf8 $Csv }
  $rows | ? { $_.NonPaged -gt 0 -or $_.Paged -gt 0 } | % { "$t,$Label,$($_.Tag -replace ',', '_'),$($_.NonPaged),$($_.NpOutstanding),$($_.Paged)" } | Out-File -Append -Encoding utf8 $Csv
}
$tot = ($rows | Measure-Object NonPaged -Sum).Sum
"$t nonpaged total by tags {0:N2} GB over {1} tags" -f ($tot / 1GB), $n
$rows | Sort-Object NonPaged -Descending | Select-Object -First $Top | % { "{0,-6} {1,10:N1} MB  outstanding {2,10}" -f $_.Tag, ($_.NonPaged / 1MB), $_.NpOutstanding }
