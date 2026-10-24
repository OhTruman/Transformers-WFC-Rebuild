# MENU-FLOW QA (Integration 2026-10-23, USER_PLAYTEST_2026-10-23b.md section 43): drives the real menus on a FRESH profile with the
# Frontend's ScriptDriver (WFC_FRONTEND_SCRIPT), windowed (the null render refuses frontend boots), muted, NOINTERACT, one exe,
# killed only by its own PID; then tools/fidelity/menu_qa_check.py turns the FLOW trace into a PASS / FAIL table per step.
#   powershell -File tools/fidelity/menu-qa.ps1 -Exe <wfc_rebuild.exe> -Flows mp,esc,camp -Out <dir> [-TimeoutS 900]
# A flow file tools/fidelity/menu-qa/<flow>.txt: one script step per line ('#' comments; '#expect ...' lines are for the checker).
param([Parameter(Mandatory)][string]$Exe, [string]$Flows = 'mp,esc,camp', [Parameter(Mandatory)][string]$Out, [int]$TimeoutS = 900,
      [string[]]$ExtraEnv = @(), [switch]$AllowTodo, [switch]$NoSeed)   # -NoSeed: a truly empty profile (the first-run popup shows)   # -AllowTodo: run a flow whose file still has TODO(frontend) steps (dry runs only)
$here = $PSScriptRoot; $py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
New-Item -ItemType Directory -Force $Out | Out-Null
foreach ($flow in ($Flows -split ',')) {
    $file = Join-Path $here "menu-qa\$flow.txt"; if (-not (Test-Path $file)) { Write-Host "$flow : no flow file"; continue }
    $todo = @(Select-String -LiteralPath $file -Pattern 'TODO\(' -SimpleMatch:$false).Count
    if ($todo -and -not $AllowTodo) { Write-Host "$flow : $todo TODO steps pending - not run (use -AllowTodo for a dry run)"; continue }
    $steps = @(Get-Content $file | % { $_.Trim() } | ? { $_ -and -not $_.StartsWith('#') })
    $run = Join-Path $Out $flow; if (Test-Path $run) { Remove-Item -Recurse -Force $run }; New-Item -ItemType Directory -Force $run | Out-Null   # fresh profile
    $script = ($steps + @('wait:t=1', 'navcheck:flow.end', 'shot:flow_end.bmp', 'quit')) -join ';'
    # a fresh profile, minus the first-visit 'Online Play / Choose Name' popup that swallows clickclip (Frontend 2026-10-24)
    if (-not $NoSeed) { Set-Content -Encoding ascii (Join-Path $run 'wfc_profile.ini') "[Accounts]`r`nSignedIn=QA`r`n[Online]`r`nFirstRunShown=1`r`n" }
    $saved = @{}; Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
    $e = @{ WFC_BOOT = 'frontend'; WFC_SKIPINTRO = '1'; WFC_CHARSELECT = '1'; WFC_FRONTEND_SCRIPT = $script; WFC_FLOW_TIMEOUT = "$TimeoutS";
            WFC_FLOWLOG = (Join-Path $run 'flow.log'); WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_AUTODETECT = '0'; WFC_QA = '1' }
    foreach ($kv in $ExtraEnv) { $k, $v = $kv -split '=', 2; $e[$k] = $v }
    foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
    $t0 = Get-Date
    $p = Start-Process -FilePath $Exe -WorkingDirectory $run -PassThru -RedirectStandardOutput (Join-Path $run 'out.txt'); $null = $p.Handle
    Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { Remove-Item -LiteralPath "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
    $killed = $false
    if (-not $p.WaitForExit(($TimeoutS + 60) * 1000)) { Stop-Process -Id $p.Id -Force; $killed = $true }
    $wall = [int]((Get-Date) - $t0).TotalSeconds
    Set-Content -Encoding utf8 (Join-Path $run 'run.json') (@{ flow = $flow; exit = $(if ($killed) { 'killed' } else { $p.ExitCode }); wall_s = $wall; steps = $steps.Count } | ConvertTo-Json)
    Write-Host "$flow : exit $(if ($killed) { 'KILLED (timeout)' } else { $p.ExitCode }) in $wall s"
}
& $py -I (Join-Path $here 'menu_qa_check.py') $Out ($Flows -split ',')
Write-Host ("own exes left: " + @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | ? { $_.Path -eq (Resolve-Path $Exe).Path }).Count)
