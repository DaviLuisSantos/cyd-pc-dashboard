# Teste de fumaça do executável do agente (cyd-pc-agent.exe), usado pelo CI.
# Sobe o .exe numa porta de teste, confere o /stats, chama o subcomando
# notify --help e encerra. Sai com erro se algo falhar.
#
#   powershell -File tools/exe_smoke_test.ps1 -Exe dist/cyd-pc-agent.exe
param([Parameter(Mandatory = $true)][string]$Exe)

$ErrorActionPreference = "Stop"
$Exe = (Resolve-Path $Exe).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ("cyd-smoke-" + [guid]::NewGuid())
New-Item -ItemType Directory $work | Out-Null
# Sem cotações nem anúncio na rede: o teste não depende de internet
'{"port": 18377, "discovery": {"enabled": false}, "quotes": {"enabled": false}}' |
    Set-Content (Join-Path $work "config.json") -Encoding ascii

$proc = Start-Process $Exe -ArgumentList "--config", (Join-Path $work "config.json") -PassThru `
    -RedirectStandardOutput (Join-Path $work "out.log") -RedirectStandardError (Join-Path $work "err.log")
try {
    $stats = $null
    for ($i = 0; $i -lt 40 -and -not $stats; $i++) {
        Start-Sleep -Milliseconds 500
        try { $stats = Invoke-RestMethod "http://127.0.0.1:18377/stats" -TimeoutSec 2 } catch { }
    }
    if (-not $stats) {
        Get-Content (Join-Path $work "out.log"), (Join-Path $work "err.log") -ErrorAction SilentlyContinue
        throw "o executavel nao respondeu em /stats"
    }
    foreach ($k in "cpu", "ram", "disk", "disks", "net_down_mbps") {
        if (-not ($stats.PSObject.Properties.Name -contains $k)) { throw "/stats sem o campo $k" }
    }
    Write-Host "/stats ok: cpu=$($stats.cpu) ram=$($stats.ram) disk=$($stats.disk)"

    & $Exe notify --help | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "notify --help saiu com $LASTEXITCODE" }
    Write-Host "notify ok"
} finally {
    # Onefile = carregador + processo Python filho: derruba a árvore toda
    Get-CimInstance Win32_Process -Filter "Name='$(Split-Path $Exe -Leaf)'" |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "teste de fumaca do executavel: ok"
