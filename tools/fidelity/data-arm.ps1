# A scratch render-data root for a DATA arm (perf / look bisects that separate code from data): every map / file of the shared
# render data is linked (junctions for directories, hard copies for the map's small top-level files), and for -Map the files
# named in -From (a backup dir laid out like render\<map>\<file>) replace the current ones. The shared data is never written:
# the replaced files are copies, everything else is a junction or a copy. Remove the root with rmdir on the junctions first.
#   .\tools\fidelity\data-arm.ps1 -Out work\bisect_data\fx -Map MP_IAC_Berth -From "F:\Transformers Rebuild\Rebuild\work\render_backup_pre_fxregen_20261016_0758"
param([Parameter(Mandatory)][string]$Out, [Parameter(Mandatory)][string]$Map, [Parameter(Mandatory)][string]$From,
      [string]$Render = "F:\Transformers Rebuild\Rebuild\work\render")
if (Test-Path $Out) { throw "$Out exists - remove it first (junctions with rmdir)" }
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
foreach ($it in Get-ChildItem $Render -Force) {
    $dst = Join-Path $Out $it.Name
    if ($it.Name -eq $Map) {
        New-Item -ItemType Directory $dst | Out-Null
        foreach ($c in Get-ChildItem $it.FullName -Force) {
            $d2 = Join-Path $dst $c.Name
            if ($c.PSIsContainer) { cmd /c mklink /J "$d2" "$($c.FullName)" | Out-Null } else { Copy-Item -LiteralPath $c.FullName $d2 }
        }
        foreach ($f in Get-ChildItem (Join-Path $From $Map) -File) { Copy-Item -LiteralPath $f.FullName (Join-Path $dst $f.Name) -Force; "replaced $Map\$($f.Name) from $From" }
    } elseif ($it.PSIsContainer) { cmd /c mklink /J "$dst" "$($it.FullName)" | Out-Null } else { Copy-Item -LiteralPath $it.FullName $dst }
}
"data arm root: $Out ($((Get-ChildItem $Out).Count) entries)"
