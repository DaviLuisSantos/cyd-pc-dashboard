"""
Agente de demonstração: serve dados fictícios (mas plausíveis) em todos os
endpoints do agente real, sem psutil e sem APIs externas.

Serve pra:
  - experimentar o painel sem instalar o agente de verdade;
  - gerar os screenshots do README sem expor dados pessoais (processos, IP
    público, canais) — o IP é da faixa reservada pra documentação (RFC 5737).

Uso:
    python tools/demo_agent.py               # porta 8377, anuncia na rede
    python tools/demo_agent.py --port 18377 --no-discovery

Com o agente real rodando na mesma máquina, pare ele antes (mesma porta) ou
use --port e aponte o painel pra essa porta.
"""

import argparse
import json
import math
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

START = time.time()


def wave(period, lo, hi, phase=0.0):
    """Valor que oscila suave entre lo e hi (pra telas parecerem vivas)."""
    t = (time.time() - START) / period + phase
    return lo + (hi - lo) * (0.5 + 0.5 * math.sin(2 * math.pi * t))


def series(base, drift, wobble, n=32, seed=0.0):
    """Histórico de 24h sintético: tendência + ondulação, terminando em `base`."""
    pts = []
    for i in range(n):
        x = i / (n - 1)
        pts.append(base * (1 - drift * (1 - x) + wobble * math.sin(6 * x + seed) * (1 - 0.3 * x)))
    pts[-1] = base
    return [round(p, 6) for p in pts]


def stats():
    cpu = wave(40, 18, 46)
    return {
        "cpu": round(cpu, 1), "ram": round(wave(90, 58, 64), 1), "disk": 71.4,
        "disks": [
            {"name": "C:", "pct": 71.4, "used_gb": 682.1, "total_gb": 953.9},
            {"name": "GAMES", "pct": 88.2, "used_gb": 1764.0, "total_gb": 2000.4},
        ],
        "disk_read_mbps": round(wave(7, 0.4, 38.0), 2), "disk_write_mbps": round(wave(11, 0.2, 12.5), 2),
        "net_down_mbps": round(wave(9, 2.0, 86.0), 2), "net_up_mbps": round(wave(13, 0.6, 9.4), 2),
        "cpu_temp": round(46 + cpu * 0.4, 1),
    }


def procs():
    return {"procs": [
        {"name": "chrome", "cpu": round(wave(30, 7.5, 14.0), 1), "mem_mb": 3184},
        {"name": "Code", "cpu": round(wave(25, 3.0, 6.5), 1), "mem_mb": 1422},
        {"name": "obs64", "cpu": 4.1, "mem_mb": 612},
        {"name": "Spotify", "cpu": 1.2, "mem_mb": 388},
    ]}


def sysinfo():
    return {
        "ping_ms": round(wave(20, 11.0, 19.0), 1),
        "public_ip": "203.0.113.42",  # RFC 5737: reservado pra documentação
        "uptime_s": 3 * 86400 + 7 * 3600 + 25 * 60,
        "net_down_mbps": stats()["net_down_mbps"], "net_up_mbps": stats()["net_up_mbps"],
    }


def quotes():
    return {"base": "USD", "prefix": "$", "updated_at": int(time.time()), "items": [
        {"symbol": "EUR", "price": 1.0832, "change_pct": 0.21, "history": series(1.0832, 0.003, 0.0015, seed=0.5)},
        {"symbol": "GBP", "price": 1.2714, "change_pct": -0.34, "history": series(1.2714, -0.004, 0.0012, seed=2.1)},
        {"symbol": "BTC", "price": 67420.0, "change_pct": 2.86, "history": series(67420.0, 0.028, 0.006, seed=1.3)},
        {"symbol": "ETH", "price": 3512.4, "change_pct": -1.12, "history": series(3512.4, -0.011, 0.007, seed=3.7)},
    ]}


def youtube():
    return {"configured": True, "updated_at": int(time.time()), "channels": [
        {"title": "Desk Setup Lab", "subs": 128400, "views": 9_847_210, "videos": 312},
        {"title": "Embedded Weekly", "subs": 23950, "views": 1_204_377, "videos": 87},
    ]}


def services():
    now = int(time.time())
    def svc(name, ok, ms, detail="", ago=0):
        return {"name": name, "ok": ok, "ms": ms, "detail": detail, "since": now - ago}
    return {"configured": True, "updated_at": now, "services": [
        svc("Home server", True, round(wave(37, 2, 6))),
        svc("Router", True, round(wave(23, 1, 3))),
        svc("Website", True, round(wave(41, 38, 61))),
        svc("Staging API", False, None, "HTTP 503", 754),
        svc("NAS", True, round(wave(29, 3, 9))),
        svc("Game server", True, round(wave(31, 21, 34))),
    ]}


ROUTES = {"/stats": stats, "/procs": procs, "/sysinfo": sysinfo, "/quotes": quotes, "/youtube": youtube,
          "/services": services}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        fn = ROUTES.get(self.path.split("?")[0])
        if not fn:
            self.send_response(404)
            self.end_headers()
            return
        body = json.dumps(fn()).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def beacon(port):
    """Mesmo anúncio do agente real, pro painel em "auto" achar o demo."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    payload = json.dumps({"cydpc": 1, "port": port, "name": "DEMO", "auth": False}).encode()
    while True:
        try:
            sock.sendto(payload, ("255.255.255.255", 8377))
        except OSError:
            pass
        time.sleep(5)


def main():
    ap = argparse.ArgumentParser(description="Agente de demonstração do CYD PC Dashboard")
    ap.add_argument("--port", type=int, default=8377)
    ap.add_argument("--no-discovery", action="store_true", help="não anuncia na rede")
    args = ap.parse_args()
    if not args.no_discovery:
        threading.Thread(target=beacon, args=(args.port,), daemon=True).start()
    print(f"Agente DEMO em http://0.0.0.0:{args.port} (/stats /procs /sysinfo /quotes /youtube /services)")
    ThreadingHTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
