@echo off
setlocal
set AGENT_DIR=%~dp0
set PORT=8377

powershell -NoProfile -Command "if (Get-NetTCPConnection -LocalPort %PORT% -State Listen -ErrorAction SilentlyContinue) { Write-Host 'Agente ja esta rodando na porta %PORT%.'; exit 1 }"
if %ERRORLEVEL% EQU 1 (
    pause
    exit /b
)

echo Iniciando agente...
start "CYD Agent" /MIN python "%AGENT_DIR%pc_stats_agent.py"
ping -n 3 127.0.0.1 >nul
echo Agente iniciado. Veja http://localhost:%PORT%/stats
pause
