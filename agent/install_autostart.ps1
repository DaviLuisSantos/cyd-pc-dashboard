# Registra o agente pra iniciar sozinho no logon do Windows (Agendador de
# Tarefas), rodando com pythonw.exe — sem janela de console. Logs em agent.log.
# Não precisa de admin. Rodar de novo só atualiza a tarefa.
#
#   powershell -ExecutionPolicy Bypass -File agent\install_autostart.ps1
#   (ou duplo-clique em install_autostart.bat)

$ErrorActionPreference = "Stop"
$TaskName = "CYD PC Dashboard Agent"
$Script = Join-Path $PSScriptRoot "pc_stats_agent.py"
$Log = Join-Path $PSScriptRoot "agent.log"

$Pythonw = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source
if (-not $Pythonw) {
    throw "pythonw.exe nao encontrado no PATH. Instale o Python (python.org) marcando 'Add to PATH'."
}
& ($Pythonw -replace 'pythonw\.exe$', 'python.exe') -c "import psutil" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "psutil nao instalado. Rode: pip install -r `"$PSScriptRoot\requirements.txt`""
}

$Action = New-ScheduledTaskAction -Execute $Pythonw `
    -Argument "`"$Script`" --log `"$Log`"" -WorkingDirectory $PSScriptRoot
$Trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$Settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings `
    -Description "Agente HTTP do CYD PC Dashboard (porta 8377)" -Force | Out-Null

# Para instâncias manuais (start_agent.bat etc.) pra não ficarem duas na mesma porta
Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='pythonw.exe'" |
    Where-Object { $_.CommandLine -like '*pc_stats_agent.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 1

Start-ScheduledTask -TaskName $TaskName
Write-Host "Tarefa '$TaskName' registrada e iniciada. Ela sobe sozinha a cada logon."
Write-Host "Logs: $Log"
