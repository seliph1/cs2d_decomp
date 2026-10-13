# Runs a case matrix against our client and measures the same per-process GPU counters
# Task Manager shows, to attribute the graphics cost. Reads sys/config.cfg once, writes each
# variant from that text and restores the file at the end (rule 5: user files stay as they were).
#
#   powershell -ExecutionPolicy Bypass -File tools/agent/perf_gpu_matrix.ps1 -Out perf.txt
#   powershell -ExecutionPolicy Bypass -File tools/agent/perf_gpu_matrix.ps1 -Out perf_res.txt `
#       -CaseList @('850x480:light=0,mapshadows=0,vsync=1,res=850x480','1280x720:light=0,mapshadows=0,vsync=1,res=1280x720')
#
# A case is "<name>:<key>=<value>[,<key>=<value>...]"; every key is rewritten in sys/config.cfg
# from its shipped value. Defaults are the four cases used for the 2026-10-13 measurement.
param(
    [string]$Exe = '.\cs2d_client.exe',
    [string]$Map = 'fun_roleplay',
    [string]$Out = 'perf_matrix.txt',
    [int]   $Settle  = 4,
    [int]   $Samples = 5,
    [string[]]$CaseList = @(
        'shipped-light5-ms4:light=5,mapshadows=4,shadows=50,vsync=1',
        'light0-ms0:light=0,mapshadows=0,shadows=0,vsync=1',
        'light5-ms4-novsync:light=5,mapshadows=4,shadows=50,vsync=0',
        'light0-ms0-novsync:light=0,mapshadows=0,shadows=0,vsync=0'
    )
)

$ErrorActionPreference = 'Continue'
# The repo root is the script's grandparent directory: the script must not depend on the
# caller's working directory (a process spawned through WMI starts in C:\Windows\system32).
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$cfg  = Join-Path $root 'sys\config.cfg'
$bak  = Join-Path $root 'sys\config.cfg.perfbak'
if (-not (Test-Path $cfg)) { "no sys/config.cfg under $root" | Out-File -Encoding utf8 $Out; exit 1 }
if (-not (Test-Path $bak)) { Copy-Item $cfg $bak -Force }

$script:cfgBase = [System.IO.File]::ReadAllText($cfg)
$script:lines   = @()
$script:lines  += "matrix exe=$Exe map=$Map settle=${Settle}s samples=$Samples"
$script:lines | Out-File -Encoding utf8 $Out

function Set-Cfg([hashtable]$kv) {
    $t = $script:cfgBase
    foreach ($k in $kv.Keys) { $t = [regex]::Replace($t, "(?m)^\s*$k\s+\S+", "$k $($kv[$k])") }
    [System.IO.File]::WriteAllText($cfg, $t)
}

function Measure-Case([string]$name, [hashtable]$kv) {
    $pid2 = $null
    try {
        Set-Cfg $kv
        $check = (Select-String -Path $cfg -Pattern ('^' + (($kv.Keys | Sort-Object) -join ' ')) -ErrorAction SilentlyContinue)
        Start-Sleep -Milliseconds 300
        taskkill /F /IM cs2d_client.exe 2>&1 | Out-Null
        Start-Sleep -Milliseconds 400
        $logOut = "$Out.$name.stdout.log"
        $logErr = "$Out.$name.stderr.log"
        $p = Start-Process -FilePath (Join-Path $root $Exe) -ArgumentList $Map -PassThru `
                -RedirectStandardOutput $logOut -RedirectStandardError $logErr
        Start-Sleep -Seconds $Settle
        $pid2 = (Get-Process -Name cs2d_client -ErrorAction SilentlyContinue | Select-Object -First 1).Id
        if (-not $pid2) {
            $script:lines += ("case {0,-22}: CLIENT NOT RUNNING (exit={1})" -f $name, $p.ExitCode)
            return
        }
        $g3 = @(); $ga = @(); $cpu = @()
        for ($i = 0; $i -lt $Samples; $i++) {
            $c = Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage', '\Process(cs2d_client*)\% Processor Time' -ErrorAction SilentlyContinue
            $g3  += ($c.CounterSamples | Where-Object { $_.InstanceName -like ("pid_" + $pid2 + "_*") -and $_.InstanceName -like '*engtype_3D*' } | Measure-Object -Property CookedValue -Sum).Sum
            $ga  += ($c.CounterSamples | Where-Object { $_.InstanceName -like ("pid_" + $pid2 + "_*") } | Measure-Object -Property CookedValue -Sum).Sum
            $cpu += ($c.CounterSamples | Where-Object { $_.Path -like '*Process*' } | Measure-Object -Property CookedValue -Sum).Sum
            Start-Sleep -Seconds 1
        }
        $settings = (($kv.Keys | Sort-Object | ForEach-Object { "$_=$($kv[$_])" }) -join ' ')
        $script:lines += ("case {0,-22} [{1}] GPU(3D) avg {2,6:N2}% max {3,6:N2}% | GPU(all engines) avg {4,6:N2}% | CPU avg {5,6:N1}%" -f `
            $name, $settings, (($g3 | Measure-Object -Average).Average), (($g3 | Measure-Object -Maximum).Maximum), `
            (($ga | Measure-Object -Average).Average), (($cpu | Measure-Object -Average).Average))
    } catch {
        $script:lines += ("case {0,-22}: ERROR {1}" -f $name, $_.Exception.Message)
    } finally {
        if ($pid2) { Stop-Process -Id $pid2 -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Milliseconds 600
        $script:lines | Out-File -Encoding utf8 $Out
    }
}

foreach ($spec in $CaseList) {
    $i = $spec.IndexOf(':')
    $name = $spec.Substring(0, $i)
    $kv = @{}
    foreach ($pair in $spec.Substring($i + 1).Split(',')) {
        $e = $pair.IndexOf('=')
        if ($e -gt 0) { $kv[$pair.Substring(0, $e)] = $pair.Substring($e + 1) }
    }
    Measure-Case $name $kv
}

Copy-Item $bak $cfg -Force
$script:lines += ("config.cfg restored (hash " + (Get-FileHash $cfg).Hash + ")")
$script:lines | Out-File -Encoding utf8 $Out
taskkill /F /IM cs2d_client.exe 2>&1 | Out-Null
