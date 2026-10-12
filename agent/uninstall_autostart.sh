#!/usr/bin/env bash
# Desfaz o install_autostart.sh: para o agente e remove o registro de autostart
# (Linux: serviço de usuário do systemd; macOS: LaunchAgent).
#
#   ./agent/uninstall_autostart.sh
#   ./agent/uninstall_autostart.sh --dry-run
set -euo pipefail

UNIT=cyd-pc-dashboard-agent.service
LABEL=com.cyd-pc-dashboard.agent

DRY_RUN=0
OS="$(uname -s)"
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --os) OS="$2"; shift ;;
        -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 2 ;;
    esac
    shift
done

run() {
    if [ "$DRY_RUN" = 1 ]; then echo "+ $*"; else "$@"; fi
}

case "$OS" in
    Linux|linux)
        UNIT_FILE="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/$UNIT"
        run systemctl --user disable --now "$UNIT" 2> /dev/null || true
        run rm -f "$UNIT_FILE"
        run systemctl --user daemon-reload
        echo "Serviço $UNIT removido."
        echo "Se usou --linger e não tem outros serviços de usuário: loginctl disable-linger ${USER:-$(id -un)}"
        ;;
    Darwin|macos|darwin)
        PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
        run launchctl bootout "gui/$(id -u)/$LABEL" 2> /dev/null || run launchctl unload -w "$PLIST" 2> /dev/null || true
        run rm -f "$PLIST"
        echo "LaunchAgent $LABEL removido."
        ;;
    *)
        echo "Sistema não suportado: $OS. No Windows, use uninstall_autostart.bat." >&2
        exit 1
        ;;
esac
