# RAY TRACING BUILD CHECK (tier FAST; 2026-10-13: integration builds had shipped without the RT shaders - third_party/dxc and
# ffx_denoiser absent at configure, so the Ray Tracing row showed and did nothing). Two checks, either or both:
#   -BuildDir <cmake build tree>   build.ninja defines WFC_RT_SHADERS and WFC_RT_DENOISER, generated/rt holds the 10 DXC headers
#   -Exe <wfc_rebuild.exe>         a direct-boot match with the profile's Ray Tracing row = Realistic (the profile row wins over
#                                  WFC_RT_MODE at boot) builds its RT scene ("... TLAS <n> instances"), logs Realistic, and never logs
#                                  "built without the RT shaders" / "this build has no RT shaders". Needs DXR 1.1 hardware.
# For a package, point -Exe at the package's exe (run it the way the package runs; verify_slim can call this script).
#
#   .\tools\fidelity\rt-build-check.ps1 [-BuildDir <dir>] [-Exe <exe>] -OutDir <dir> [-Map MP_IAC_Streets_Base_m] [-Frames 900]
param([string]$BuildDir = "", [string]$Exe = "", [Parameter(Mandatory)][string]$OutDir, [string]$Map = "MP_IAC_Streets_Base_m", [int]$Frames = 900)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "rtcheck.$id" $status $null $note $owner }
if ($BuildDir) {
    $bn = Join-Path $BuildDir "build.ninja"
    if (-not (Test-Path $bn)) { Res "build" "UNKNOWN" "no build.ninja in $BuildDir" "Experimental" }
    else {
        $t = [IO.File]::ReadAllText((Resolve-Path $bn).Path)
        $sh = $t.Contains("WFC_RT_SHADERS=1"); $dn = $t.Contains("WFC_RT_DENOISER=1")
        $hdr = @(Get-ChildItem (Join-Path $BuildDir "generated\rt") -Filter *.h -ErrorAction SilentlyContinue).Count
        Res "build" $(if ($sh -and $dn -and $hdr -ge 10) { "PASS" } else { "FAIL" }) ("WFC_RT_SHADERS {0}, WFC_RT_DENOISER {1}, generated/rt headers {2} of 10 (third_party/dxc + ffx_denoiser must exist at configure: tools/render/fetch_dxc.py, fetch_ffx_denoiser.py)" -f $sh, $dn, $hdr) "Integration"
    }
}
if ($Exe) {
    $Exe = (Resolve-Path $Exe).Path; $d = Join-Path $OutDir "rt_on"; New-Item -ItemType Directory -Force $d | Out-Null; $lg = Join-Path $d "wfc.log"
    if (-not (Test-Path $lg)) {
        (Get-BotProfile 0 0 1 -Width 1920 -Height 1080 -RayTracing 2) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        $null = Invoke-WfcExe $Exe $d @{ WFC_BOOT = "match"; WFC_MATCH_URL = "${Map}?GameModeTag=TDM"; WFC_SMOKE_FRAMES = "$Frames"; WFC_NOMOUSE = "1"; WFC_SKIPINTRO = "1" } "run.log" 600
    }
    if (-not (Test-Path $lg)) { Res "runtime" "UNKNOWN" "no log" "Experimental" }
    else {
        $rs = Get-WfcRtState $lg
        # (a map that did not load is the harness' fault, not the build's: 2026-10-13 "$Map?..." expanded an undefined variable and
        #  the run fell back to the graybox arena - Systems)
        $noMap = Select-String -Path $lg -Pattern 'render data not found' -Quiet
        if ($noMap) { Res "runtime" "UNKNOWN" ("map '{0}' did not load (render data not found): no verdict" -f $Map) "Experimental"; $Exe = $null }
        if (-not $Exe) { } else {
        $st = if ($rs.noShaders) { "FAIL" } elseif ($rs.scene -and $rs.mode -eq 2) { "PASS" } elseif (Select-String -Path $lg -Pattern 'RT bridge: no D3D12 device|raytracing tier .* < 1\.1' -Quiet) { "UNKNOWN" } else { "FAIL" }
        Res "runtime" $st ("profile RayTracing=2 on {0}: mode {1} (logged {2}), RT scene {3}, no-shader message {4}{5}" -f $Map, $rs.mode, $rs.modeLogged, $rs.scene, $rs.noShaders,
            $(if ($st -eq "UNKNOWN") { " (this machine has no DXR 1.1 device: not a build verdict)" } else { "" })) "Rendering"
        }
    }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"RT BUILD CHECK: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
