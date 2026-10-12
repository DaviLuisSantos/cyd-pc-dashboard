# Remove a tarefa de inicialização automática do agente e para o agente.
#
#   powershell -ExecutionPolicy Bypass -File agent\uninstall_autostart.ps1

$TaskName = "CYD PC Dashboard Agent"

if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "Tarefa '$TaskName' removida."
} else {
    Write-Host "Tarefa '$TaskName' nao existe."
}

Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='pythonw.exe'" |
    Where-Object { $_.CommandLine -like '*pc_stats_agent.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
