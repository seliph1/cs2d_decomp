# run_live_test.ps1
Stop-Process -Name cs2d_server, cs2d_client, CS2D, cs2d_dedicated -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Write-Host "Starting dedicated server with full_live_suite.lua..."
$sv = Start-Process -FilePath ".\cs2d_server.exe" -ArgumentList "-port 36963 -offline -map de_dust -lua tools/agent/lua/full_live_suite.lua" -PassThru -NoNewWindow -RedirectStandardOutput "server_live.log" -RedirectStandardError "server_err.log"

Start-Sleep -Seconds 2

Write-Host "Starting CS2D.exe client connecting to 127.0.0.1:36963..."
$cl = Start-Process -FilePath ".\CS2D.exe" -ArgumentList "-join 127.0.0.1:36963 -nointro -offline" -PassThru

Write-Host "Waiting 12 seconds for client to finish map load and handshake..."
Start-Sleep -Seconds 12

Write-Host "Sending interactive input sequence to real client..."
powershell -NoProfile -File "tools/agent/drive_orig.ps1" "focus;wait:1000;tap:1;wait:500;click:3:150;wait:500;tap:R;wait:500;tap:G;wait:500;tap:T;wait:2000"

Write-Host "Waiting for suite final report..."
Start-Sleep -Seconds 8

if ($cl) { Stop-Process -Id $cl.Id -Force -ErrorAction SilentlyContinue }
if ($sv) { Stop-Process -Id $sv.Id -Force -ErrorAction SilentlyContinue }
Stop-Process -Name cs2d_server, cs2d_client, CS2D, cs2d_dedicated -Force -ErrorAction SilentlyContinue

if (Test-Path "server_live.log") {
    Get-Content "server_live.log" | Select-Object -Last 60
} else {
    Write-Host "server_live.log not found!"
}

