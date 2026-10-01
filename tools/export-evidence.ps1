param([string]$Snapshot='work\ghidra-snapshot-20260930',[string]$Evidence='work\re-evidence')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$ghidra='F:\Transformers Rebuild\ghidra_12.1.4_PUBLIC'
$java='C:\Program Files\Eclipse Adoptium\jdk-25.0.4.101-hotspot\bin\java.exe'
$vmArgs=@(Get-Content -LiteralPath (Join-Path $ghidra 'support\launch.properties') | Where-Object {$_ -match '^VMARGS(?:_WINDOWS)?='} | ForEach-Object {($_ -split '=',2)[1]})
& $java @vmArgs '-Xmx3G' "-Duser.home=$root\work\ghidra-home" "-Dapplication.settingsdir=$root\work\ghidra-home\settings" "-Dapplication.cachedir=$root\work\ghidra-home\cache" "-Dapplication.tempdir=$root\work\ghidra-home\temp" "-Djava.io.tmpdir=$root\work\ghidra-home" '-Djava.awt.headless=true' -cp (Join-Path $ghidra 'Ghidra\Framework\Utility\lib\Utility.jar') ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless (Join-Path $root $Snapshot) WFC-Rebuild -process -noanalysis -readOnly -scriptPath (Join-Path $root 'tools\ghidra_scripts') -postScript ExportRebuildEvidence.java (Join-Path $root $Evidence) -log (Join-Path $root 'work\headless.log') -scriptlog (Join-Path $root 'work\headless-script.log')
if($LASTEXITCODE) { throw 'Headless export failed; inspect work\headless.log' }
