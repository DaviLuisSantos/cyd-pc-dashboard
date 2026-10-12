#!/usr/bin/env bash
# Registra o agente pra iniciar sozinho no Linux e no macOS. Não precisa de root.
#
#   Linux: serviço de usuário do systemd (~/.config/systemd/user/), reiniciado
#          se cair; logs em `journalctl --user -u cyd-pc-dashboard-agent`
#   macOS: LaunchAgent do launchd (~/Library/LaunchAgents/), carregado no
#          login; logs em agent/agent.log
#
# Usa o Python do agent/.venv se existir, senão o python3 do PATH. Rodar de novo
# só atualiza o registro. Pra desfazer: agent/uninstall_autostart.sh
#
#   ./agent/install_autostart.sh            # registra e inicia
#   ./agent/install_autostart.sh --linger   # Linux: sobe já no boot, sem precisar de login
#   ./agent/install_autostart.sh --dry-run  # só mostra o que faria
set -euo pipefail

UNIT=cyd-pc-dashboard-agent.service
LABEL=com.cyd-pc-dashboard.agent

AGENT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="$AGENT_DIR/pc_stats_agent.py"
LOG="$AGENT_DIR/agent.log"

DRY_RUN=0
LINGER=0
OS="$(uname -s)"
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --linger) LINGER=1 ;;
        --os) OS="$2"; shift ;;  # pra testar a geração em outro sistema (com --dry-run)
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 2 ;;
    esac
    shift
done

# Executa, ou só mostra no --dry-run
run() {
    if [ "$DRY_RUN" = 1 ]; then echo "+ $*"; else "$@"; fi
}

# Escreve um arquivo (conteúdo na entrada padrão), ou mostra no --dry-run
write_file() {
    if [ "$DRY_RUN" = 1 ]; then
        echo "--- $1"
        cat
        echo "---"
    else
        mkdir -p "$(dirname "$1")"
        cat > "$1"
    fi
}

if [ -x "$AGENT_DIR/.venv/bin/python" ]; then
    PYTHON="$AGENT_DIR/.venv/bin/python"
elif command -v python3 > /dev/null 2>&1; then
    PYTHON="$(command -v python3)"
else
    echo "python3 não encontrado. Instale o Python 3.8+." >&2
    exit 1
fi

if ! "$PYTHON" -c "import psutil" > /dev/null 2>&1; then
    msg="psutil não instalado pra $PYTHON. Rode: $PYTHON -m pip install -r \"$AGENT_DIR/requirements.txt\""
    if [ "$DRY_RUN" = 1 ]; then echo "aviso: $msg"; else echo "$msg" >&2; exit 1; fi
fi

# Para instâncias manuais (python pc_stats_agent.py rodando num terminal), senão o
# serviço não consegue abrir a porta e fica reiniciando
stop_manual_agents() {
    if pgrep -f "pc_stats_agent.py" > /dev/null 2>&1; then
        echo "Parando o agente que estava rodando fora do autostart..."
        run pkill -f "pc_stats_agent.py" || true
        [ "$DRY_RUN" = 1 ] || sleep 1
    fi
}

case "$OS" in
    Linux|linux)
        if [ "$DRY_RUN" = 0 ] && ! command -v systemctl > /dev/null 2>&1; then
            echo "systemd não encontrado. Rode o agente de outro jeito (ex.: cron @reboot):" >&2
            echo "  @reboot $PYTHON \"$SCRIPT\" --log \"$LOG\"" >&2
            exit 1
        fi
        UNIT_FILE="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/$UNIT"
        write_file "$UNIT_FILE" <<EOF
[Unit]
Description=CYD PC Dashboard agent (HTTP na porta 8377)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart="$PYTHON" "$SCRIPT"
WorkingDirectory=$AGENT_DIR
Restart=on-failure
RestartSec=5
# stdout/stderr vão pro journal (journalctl --user -u ${UNIT%.service})
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=default.target
EOF
        stop_manual_agents
        run systemctl --user daemon-reload
        run systemctl --user enable "$UNIT"
        run systemctl --user restart "$UNIT"
        if [ "$LINGER" = 1 ]; then
            # Sem isso, serviços de usuário só sobem depois do login
            run loginctl enable-linger "${USER:-$(id -un)}"
        fi
        echo
        echo "Serviço $UNIT registrado e iniciado. Ele sobe sozinho a cada login$([ "$LINGER" = 1 ] && echo " (e no boot, com linger)")."
        echo "Status: systemctl --user status ${UNIT%.service}"
        echo "Logs:   journalctl --user -u ${UNIT%.service} -f"
        ;;

    Darwin|macos|darwin)
        PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
        # Caminhos entram num XML: escapa &, < e >
        xml() { printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }
        write_file "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$(xml "$PYTHON")</string>
        <string>$(xml "$SCRIPT")</string>
        <string>--log</string>
        <string>$(xml "$LOG")</string>
    </array>
    <key>WorkingDirectory</key>
    <string>$(xml "$AGENT_DIR")</string>
    <key>RunAtLoad</key>
    <true/>
    <!-- Reinicia se cair (saída com erro), mas não se for parado de propósito -->
    <key>KeepAlive</key>
    <dict>
        <key>SuccessfulExit</key>
        <false/>
    </dict>
</dict>
</plist>
EOF
        stop_manual_agents
        DOMAIN="gui/$(id -u)"
        # Recarrega do zero (rodar de novo atualiza o registro)
        run launchctl bootout "$DOMAIN/$LABEL" 2> /dev/null || true
        if ! run launchctl bootstrap "$DOMAIN" "$PLIST"; then
            run launchctl load -w "$PLIST"  # macOS antigo
        fi
        echo
        echo "LaunchAgent $LABEL registrado e iniciado. Ele sobe sozinho a cada login."
        echo "Logs: $LOG"
        echo "Na primeira vez o macOS pode perguntar se o Python pode aceitar conexões: permita, senão o painel não alcança o agente."
        ;;

    *)
        echo "Sistema não suportado: $OS. No Windows, use install_autostart.bat." >&2
        exit 1
        ;;
esac
