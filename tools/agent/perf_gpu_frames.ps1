# Measures GPU time per frame by dividing the Task-Manager GPU counters by a known frame count
# (tools/agent/perf_frames.sh runs --capture:"map <map>;!wait <frames>", which exits after
# <frames> + 36 frames). Comparisons between runs of the same length are what matter: the
# startup cost (window, context, map load) is a constant bias that cancels out.
# Numbers and their reading: docs/agents/KNOWLEDGE.md "Performance".
#   powershell -ExecutionPolicy Bypass -File tools/agent/perf_gpu_frames.ps1 -Label msaa4
#   powershell -ExecutionPolicy Bypass -File tools/agent/perf_gpu_frames.ps1 -Label msaa0 -Msaa 0
param(
    [string]$Map    = 'fun_roleplay',
    [int]   $Frames = 600,
    [string]$Out    = 'perf_frames.txt',
    [string]$Label  = 'default',
    [int]   $Msaa   = -1,    # -1: leave CS2D_MSAA unset (project default); 0/4: set the context MSAA
    [switch]$Menu,           # measure the main menu instead of a map
    [switch]$DrawCalls,      # CS2D_DRAWCALLS=1: the client prints the batcher stats of the run
    [string]$Config = ''     # "key=val,…" applied to sys/config.cfg for the run (backed up, restored)
)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
# A process spawned outside a console keeps C:\Windows\system32 as both the POSIX-ish and the .NET
# current directory, so every path here (and Start-Process -WorkingDirectory) must be explicit.
Set-Location $root
[Environment]::CurrentDirectory = $root

$bash = 'C:\Program Files\Git\bin\bash.exe'
if (-not (Test-Path $bash)) { $bash = (Get-Command bash -ErrorAction SilentlyContinue).Source }
if (-not $bash) { "no bash found" | Out-File -Encoding utf8 $Out; exit 1 }

$lines = @()
$mapArg = if ($Menu) { '-' } else { $Map }
$lines += "variant=$Label scene=$(if ($Menu) { '(main menu)' } else { $Map }) frames=$Frames (+36 capture overhead) msaa=$Msaa config='$Config' bash=$bash"

# Optional sys/config.cfg override for this run only (rule 5: the file goes back at the end)
$cfg = Join-Path $root 'sys\config.cfg'
$bak = Join-Path $root 'sys\config.cfg.perfbak'
$cfgChanged = $false
if ($Config -ne '') {
    if (-not (Test-Path $bak)) { Copy-Item $cfg $bak -Force }
    $text = [System.IO.File]::ReadAllText($cfg)
    foreach ($pair in $Config.Split(',')) {
        $e = $pair.IndexOf('=')
        if ($e -gt 0) {
            $k = $pair.Substring(0, $e).Trim()
            $v = $pair.Substring($e + 1).Trim()
            $text = [regex]::Replace($text, "(?m)^\s*$k\s+\S+", "$k $v")
        }
    }
    [System.IO.File]::WriteAllText($cfg, $text)
    $cfgChanged = $true
}

if ($Msaa -ge 0) { $env:CS2D_MSAA = "$Msaa" } else { Remove-Item Env:\CS2D_MSAA -ErrorAction SilentlyContinue }
if ($DrawCalls) { $env:CS2D_DRAWCALLS = '1' } else { Remove-Item Env:\CS2D_DRAWCALLS -ErrorAction SilentlyContinue }
taskkill /F /IM cs2d_client.exe 2>&1 | Out-Null
Start-Sleep -Milliseconds 400

$logOut = Join-Path $root "$Out.$Label.stdout.log"
$logErr = Join-Path $root "$Out.$Label.stderr.log"
$p = Start-Process -FilePath $bash -ArgumentList @('tools/agent/perf_frames.sh', "$Frames", $mapArg) `
        -WorkingDirectory $root -PassThru `
        -RedirectStandardOutput $logOut -RedirectStandardError $logErr

# Wait for the client window/process, then sample every second until it exits
$pid2 = $null
for ($i = 0; $i -lt 40 -and -not $pid2; $i++) {
    Start-Sleep -Milliseconds 250
    $pid2 = (Get-Process -Name cs2d_client -ErrorAction SilentlyContinue | Select-Object -First 1).Id
}
if (-not $pid2) { $lines += "CLIENT DID NOT START (see $logErr)"; $lines | Out-File -Encoding utf8 $Out; exit 1 }

$busyMs = 0.0; $samples = 0; $peak = 0.0
while ($samples -lt 120) {
    $c = Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction SilentlyContinue
    $v = ($c.CounterSamples | Where-Object { $_.InstanceName -like ("pid_" + $pid2 + "_*") -and $_.InstanceName -like '*engtype_3D*' } |
          Measure-Object -Property CookedValue -Sum).Sum
    if ($null -eq $v) { $v = 0.0 }
    $busyMs += $v * 10.0      # a 1 s sample at v % is v * 10 ms of GPU busy time
    if ($v -gt $peak) { $peak = $v }
    $samples++
    if ((Get-Process -Id $pid2 -ErrorAction SilentlyContinue) -eq $null) { break }
    Start-Sleep -Seconds 1
}

$totalFrames = $Frames + 36
$perFrame    = if ($totalFrames -gt 0) { $busyMs / $totalFrames } else { 0 }
$implied60   = $perFrame / (1000.0 / 60.0) * 100.0
$lines += ("GPU busy {0:N0} ms over {1} samples ({2:N1} s), peak {3:N1}%" -f $busyMs, $samples, $samples, $peak)
$lines += ("frames {0} -> GPU {1:N3} ms/frame  (that is {2:N2}% of a 60 FPS frame budget, i.e. what Task Manager shows with VSync ON)" -f `
            $totalFrames, $perFrame, $implied60)
if ($cfgChanged) {
    Copy-Item $bak $cfg -Force
    $lines += ("config.cfg restored (hash " + (Get-FileHash $cfg).Hash + ")")
}
$lines | Out-File -Encoding utf8 $Out
taskkill /F /IM cs2d_client.exe 2>&1 | Out-Null
