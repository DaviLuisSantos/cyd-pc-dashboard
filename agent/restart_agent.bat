@echo off
set AGENT_DIR=%~dp0
set PORT=8377

echo Parando agente(s) pc_stats_agent.py...
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | Where-Object { $_.CommandLine -like '*pc_stats_agent.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"

ping -n 2 127.0.0.1 >nul

echo Iniciando agente...
start "CYD Agent" /MIN python "%AGENT_DIR%pc_stats_agent.py"
ping -n 3 127.0.0.1 >nul
echo Agente reiniciado. Veja http://localhost:%PORT%/stats
pause
