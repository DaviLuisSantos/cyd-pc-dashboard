"""
Manda uma notificação pro painel do CYD PC Dashboard (POST /api/notify).

Só stdlib. O endereço e a senha da interface web do painel vêm de "panel" no
agent/config.json (default: cyd-dashboard.local, sem senha) ou de --host/--password.

Uso:
    python notify.py "Build terminou"
    python notify.py "3 testes falharam" --title CI --level error
    python notify.py "Backup ok" --level ok --duration 0      # fica até tocar
    make 2>&1 | tail -n 1 | python notify.py --title make     # texto pelo stdin

Exemplos de "me avise quando acabar":
    bash:       ./build.sh && python notify.py "Build ok" --level ok || python notify.py "Build falhou" --level error
    PowerShell: npm test; if ($?) { python notify.py "Testes ok" --level ok } else { python notify.py "Testes falharam" --level error }
"""

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = ("info", "ok", "warn", "error")


def load_panel_config(path):
    panel = {"host": "cyd-dashboard.local", "password": ""}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            panel.update(json.load(f).get("panel") or {})
    return panel


def send(host, password, payload, retries=3):
    req = urllib.request.Request(
        f"http://{host}/api/notify",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    if password:
        cred = base64.b64encode(f"admin:{password}".encode()).decode()
        req.add_header("Authorization", f"Basic {cred}")
    last = None
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code in (400, 401):  # erro "de verdade": não adianta tentar de novo
                raise
            last = e
        except OSError as e:  # painel ocupado/reiniciando, conexão resetada
            last = e
        time.sleep(0.5 * (attempt + 1))
    raise last


def main():
    p = argparse.ArgumentParser(description="Notificação pro painel CYD PC Dashboard")
    p.add_argument("text", nargs="?", help="texto (sem ele, lê do stdin)")
    p.add_argument("--title", default="", help="título (opcional)")
    p.add_argument("--level", choices=LEVELS, default="info")
    p.add_argument("--duration", type=int, help="segundos na tela (0 = até tocar; default do painel: 8)")
    p.add_argument("--host", help="endereço do painel (default: panel.host do config.json)")
    p.add_argument("--password", help="senha da interface web do painel")
    p.add_argument("--config", default=os.path.join(HERE, "config.json"))
    args = p.parse_args()

    text = args.text
    if text is None and not sys.stdin.isatty():
        text = sys.stdin.read().strip()
    if not text and not args.title:
        p.error("informe o texto (argumento ou stdin)")

    panel = load_panel_config(args.config)
    payload = {"text": text or "", "title": args.title, "level": args.level}
    if args.duration is not None:
        payload["duration"] = args.duration

    try:
        send(args.host or panel["host"], args.password if args.password is not None else panel["password"], payload)
    except urllib.error.HTTPError as e:
        sys.exit(f"notify: o painel respondeu {e.code}" + (" (senha errada?)" if e.code == 401 else ""))
    except OSError as e:
        sys.exit(f"notify: não consegui falar com o painel ({e})")


if __name__ == "__main__":
    main()
