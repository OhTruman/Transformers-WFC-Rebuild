param([string]$Dump='F:\Transformers Rebuild\Game Dump')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$out=Join-Path $root 'work\inventory'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$dumpPath=(Resolve-Path -LiteralPath $Dump).Path.TrimEnd('\')
$files=@(Get-ChildItem -LiteralPath (Join-Path $dumpPath 'TransGame') -Recurse -File -Force)
$manifest=@(foreach($file in $files) {
    $header=$null; $version=$null; $licensee=$null
    if($file.Extension -eq '.xxx') {
        $stream=[System.IO.File]::OpenRead($file.FullName)
        try { $bytes=New-Object byte[] 8; if($stream.Read($bytes,0,8) -eq 8) {
            $header=[BitConverter]::ToString($bytes)
            if($header.StartsWith('9E-2A-83-C1')) {
                $licensee=[int]$bytes[4]*256+[int]$bytes[5]
                $version=[int]$bytes[6]*256+[int]$bytes[7]
            }
        } } finally { $stream.Dispose() }
    }
    [PSCustomObject]@{Path=$file.FullName.Substring($dumpPath.Length+1);Bytes=$file.Length;Extension=$file.Extension;Header=$header;PackageVersion=$version;LicenseeVersion=$licensee}
})
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'assets.json')
$groups=@($files|Group-Object Extension|Sort-Object Count -Descending|ForEach-Object {[PSCustomObject]@{Extension=$_.Name;Count=$_.Count;Bytes=($_.Group|Measure-Object Length -Sum).Sum}})
$summary=[PSCustomObject]@{
    Dump=$dumpPath;TransGameFiles=$files.Count;TransGameBytes=($files|Measure-Object Length -Sum).Sum;Types=$groups
    LooseSourceFiles=@($files|Where-Object {$_.Extension -in '.cpp','.c','.h','.hpp','.uc','.cs'}).Count
    LooseBuildProjects=@($files|Where-Object {$_.Extension -in '.sln','.vcxproj','.vcproj','.uproject' -or $_.Name -eq 'CMakeLists.txt'}).Count
    XexSha256=(Get-FileHash -LiteralPath (Join-Path $dumpPath 'default.xex') -Algorithm SHA256).Hash
    PackageHeaders=@($manifest|Where-Object Extension -eq '.xxx'|Group-Object Header|ForEach-Object {[PSCustomObject]@{Header=$_.Name;Count=$_.Count}})
}
$summary|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $out 'summary.json')
$summary|ConvertTo-Json -Depth 6
