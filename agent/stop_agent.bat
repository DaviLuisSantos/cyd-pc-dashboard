@echo off
echo Parando agente(s) pc_stats_agent.py...
powershell -NoProfile -Command "$procs = Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | Where-Object { $_.CommandLine -like '*pc_stats_agent.py*' }; if (-not $procs) { Write-Host 'Nenhum agente rodando.' } else { $procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue; Write-Host ('Encerrado PID ' + $_.ProcessId) } }"
pause
