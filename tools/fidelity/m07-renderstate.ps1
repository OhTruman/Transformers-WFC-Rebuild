# MILESTONE 07 RENDER-STATE CORRUPTION: after every UI / HUD / menu overlay the next world frame must be sane.
# Permanent coverage of the M06 regression (UI pass left GL_DEPTH_TEST / GL_CULL_FACE disabled -> architecture missing
# behind HUD / Optimus / effects, frontend route only).
#
#   .\tools\fidelity\m07-renderstate.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Config Debug|Release] [-ReportOnly]
#
# Transitions on the real frontend route (Streets): Choose Character -> play, pause -> resume, death / spectate ->
# respawn, results -> lobby -> second match, runtime display change (display:1920,1080 when the build has it).
# After each, the next world frame is judged three independent ways:
#   pixels     world coverage with the HUD band and the player excluded (Present-World)
#   renderer   <shot>.json (WFC_VISUALCHECK): opaque draws without depth test = 0, GL errors = 0, viewport = window
#   GL entry   the state the overlay left for the world pass: depth test / depth writes on, no scissor, colour mask
#              1111, fill mode, default framebuffer (Rendering's beginFrame capture)
# Negative controls (only when the exe has the hooks): WFC_GFX_NO_GLRESTORE=1 (Frontend's restore off),
# WFC_M11_INHERITSTATE=1 (Rendering's per-frame state reset off), and both: with BOTH defences off the world MUST fail -
# if it passes, the detector is blind (TEST FAULT); with either one off the other defence must keep the world intact.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("Release", "Debug")][string]$Config = "Debug", [string[]]$OnlyVariants = @(), [switch]$ReportOnly)   # -OnlyVariants normal: FAST tier (no negative controls)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$src = Join-Path $Root "src"; $hasDisplay = [bool](Get-ChildItem $src -Recurse -Include *.cpp -ErrorAction SilentlyContinue | Select-String -Pattern 'rfind("display:", 0)' -SimpleMatch -List | Select-Object -First 1)
$res = New-WfcResults
function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "m07state.$id" $status $null $note $owner }
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;shot:{D}\t0_charselect.bmp;ui:Accept;" } else { "" }
$disp = if ($hasDisplay) { "display:1920,1080,0;wait:t=2;shot:{D}\t9_display1080.bmp;display:1280,720,0;wait:t=2;shot:{D}\t9b_display720.bmp;" } else { "" }
$script = "wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1.5;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;call:Online.SetSelectedMapID,508;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:t=1.5;shot:{D}\t1_after_charselect.bmp;showmenu;wait:ui=Paused;wait:t=1;shot:{D}\t2_paused.bmp;ui:Accept;wait:ui=InGame;wait:t=1.5;shot:{D}\t3_after_resume.bmp;${disp}wait:ui=Spectating;wait:t=0.5;shot:{D}\t4_spectating.bmp;wait:ui=InGame;wait:t=1.5;shot:{D}\t5_after_respawn.bmp;wait:ui=GameEnded;wait:t=2;shot:{D}\t6_results.bmp;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:t=1.5;shot:{D}\t7_second_match.bmp;wait:t=3;shot:{D}\t8_second_match_later.bmp;quit"
$variants = [ordered]@{ normal = @{} }
if ($H.Contains("WFC_GFX_NO_GLRESTORE")) { $variants.no_frontend_restore = @{ WFC_GFX_NO_GLRESTORE = "1" } }
if ($H.Contains("WFC_M11_INHERITSTATE")) { $variants.no_renderer_reset = @{ WFC_M11_INHERITSTATE = "1" } }
if ($H.Contains("WFC_GFX_NO_GLRESTORE") -and $H.Contains("WFC_M11_INHERITSTATE")) { $variants.both_off = @{ WFC_GFX_NO_GLRESTORE = "1"; WFC_M11_INHERITSTATE = "1" } }
$OnlyVariants = @($OnlyVariants | ForEach-Object { $_ -split "," } | Where-Object { $_ }); if ($OnlyVariants.Count) { foreach ($k in @($variants.Keys)) { if ($OnlyVariants -notcontains $k) { $variants.Remove($k) } } }
$worldShots = "t1_after_charselect", "t3_after_resume", "t5_after_respawn", "t7_second_match", "t8_second_match_later", "t9_display1080", "t9b_display720"
$rows = New-Object System.Collections.Generic.List[object]; $verdictByVariant = @{}
foreach ($v in $variants.Keys) {
    $d = Join-Path $OutDir $v; New-Item -ItemType Directory -Force $d | Out-Null
    $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $script.Replace("{D}", $d); WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "600"; WFC_LIFECYCLE = "3"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "60" }
    if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }; if ($H.Contains("WFC_VISUALCHECK")) { $e.WFC_VISUALCHECK = "1" }
    foreach ($k in $variants[$v].Keys) { $e[$k] = $variants[$v][$k] }
    if (-not $ReportOnly) { if (Wait-WfcGpu) { $null = Invoke-WfcSampled $exe $d $e 900 1.0 } else { "GPU busy" | Set-Content (Join-Path $d "SKIPPED.txt") } }
    $fails = 0
    foreach ($s in $worldShots) {
        $f = Join-Path $d "$s.bmp"; if (-not (Test-Path $f)) { continue }
        $w = Present-World $f; $wv = Present-WorldVerdict $w; $diag = Read-ShotDiag $f
        $entryBad = if ($diag) { @(Test-GlEntryState $diag.entry) } else { @() }
        $rendBad = @(); if ($diag) { if ([int]$diag.noDepth -gt 0) { $rendBad += "$($diag.noDepth) opaque draws without depth test" }; if ([int]$diag.glErrors -gt 0) { $rendBad += "$($diag.glErrors) GL errors" } }
        if ($s -like "t9*" -and $diag) { $want = if ($s -eq "t9_display1080") { "0,0,1920,1080" } else { "0,0,1280,720" }; if ($diag.viewport -ne $want) { $rendBad += "viewport $($diag.viewport) (window $want)" } }
        $status = if ($wv -eq "FAIL" -or $rendBad.Count) { "FAIL" } elseif ($entryBad.Count) { "FAIL" } elseif ($wv -eq "PARTIAL") { "PARTIAL" } else { "PASS" }
        if ($status -eq "FAIL") { $fails++ }
        $rows.Add([pscustomobject][ordered]@{ variant = $v; transition = $s; status = $status; world = "$wv ($($w.detail))"; renderer = $(if ($diag) { if ($rendBad.Count) { $rendBad -join "; " } else { "ok (world $($diag.world), bsp $($diag.bsp))" } } else { "no <shot>.json (hook absent)" }); gl_entry = $(if ($diag) { if ($entryBad.Count) { $entryBad -join "; " } else { "sane" } } else { "n/a" }) })
        if ($v -eq "normal") { Res "transition.$s" $status ("world {0} (detail {1}); renderer {2}; GL state left by the overlay {3}" -f $wv, $w.detail, $rows[-1].renderer, $rows[-1].gl_entry) $(if ($entryBad.Count) { "Frontend (overlay leaves GL state)" } elseif ($rendBad.Count -or $wv -eq "FAIL") { "Rendering/Integration" } else { "" }) }
    }
    $verdictByVariant[$v] = $fails
}
# negative controls: prove the detector still sees the M06 bug, and that each defence alone holds
if ($variants.Contains("both_off")) { Res "control.both_defences_off" $(if ($verdictByVariant.both_off -gt 0) { "PASS" } else { "FAIL" }) ("with Frontend's GL restore AND Rendering's per-frame reset disabled the world must fail: failing transitions {0}. PASS = the detector still sees the M06 bug; FAIL = TEST FAULT, the detector is blind" -f $verdictByVariant.both_off) "Experimental" }
foreach ($v in "no_frontend_restore", "no_renderer_reset") { if ($variants.Contains($v)) { Res "control.$v" $(if ($verdictByVariant[$v] -eq 0) { "PASS" } else { "FAIL" }) ("{0}: failing transitions {1} (the other defence alone must keep every world frame sane)" -f $v, $verdictByVariant[$v]) $(if ($v -eq "no_frontend_restore") { "Rendering" } else { "Frontend" }) } }
if ($OnlyVariants.Count) { Res "control.hooks" "INFO" ("negative controls not run (-OnlyVariants {0}, FAST tier): detector blindness last proven by the M07 dry run" -f ($OnlyVariants -join ",")) "Experimental" } elseif ($variants.Count -eq 1) { Res "control.hooks" "INFO" "this build has neither WFC_GFX_NO_GLRESTORE nor WFC_M11_INHERITSTATE: negative controls not run (they arrive with Frontend a96f841+ / Rendering M11+)" "Experimental" }
Write-WfcCsv $rows (Join-Path $OutDir "renderstate.csv")
Write-M07Matrix $rows @("variant", "transition", "status", "world", "renderer", "gl_entry") (Join-Path $OutDir "RENDERSTATE.md") "M07 render-state after overlays" @("exe: ``$exe``", "", "A world of only HUD / Optimus / effects FAILS on pixels even when draw counts look normal; GL state left by an overlay FAILS even when the renderer re-asserts it (both defences must hold).")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"M07 RENDER STATE: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
