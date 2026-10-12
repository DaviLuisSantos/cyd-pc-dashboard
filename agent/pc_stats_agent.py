"""
Agente de stats do PC para o CYD PC Dashboard.

Expõe (JSON via HTTP):
  GET /stats   -> CPU, RAM, disco e velocidade de rede
  GET /quotes  -> cotações de moedas/cripto configuradas em "quotes"
  GET /procs   -> top N processos por uso de CPU (agregados por nome)
  GET /sysinfo -> latência (ping TCP), IP público e uptime do PC
  GET /youtube -> stats de canais do YouTube (requer API key)

Também anuncia a si mesmo na rede local (broadcast UDP, ver "discovery"),
pro painel achar o agente sozinho com o IP configurado como "auto".

Com "token" definido (ou a env CYDPC_AGENT_TOKEN), todo request precisa do
header "X-Agent-Token: <token>" (ou ?token=<token> na URL, pra testar no
navegador); sem ele a resposta é 401.

Roda na máquina que o ESP32 vai consultar. Toda a configuração fica em
agent/config.json (copie de config.example.json) — tudo é opcional, o que
faltar usa o default de DEFAULT_CONFIG.

Uso:
    pip install -r requirements.txt
    python pc_stats_agent.py [--config caminho/config.json] [--port 8377]
"""

import argparse
import copy
import hmac
import ipaddress
import json
import os
import socket
import sys
import time
import threading
import unicodedata
import urllib.request
from urllib.parse import parse_qs, urlsplit
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import psutil

HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_CONFIG = {
    "bind": "0.0.0.0",
    "port": 8377,
    # Token compartilhado com o painel (vazio = sem autenticação). A env
    # CYDPC_AGENT_TOKEN tem prioridade. Vai em HTTP puro na LAN: protege contra
    # acesso casual de outros dispositivos da rede, não contra quem captura tráfego.
    "token": "",
    # Partição cujo uso (%) aparece no painel. null = raiz do sistema ("C:\\" ou "/")
    "disk_path": None,
    "top_procs": 4,
    "ping": {"host": "8.8.8.8", "port": 53},
    "public_ip_url": "https://api.ipify.org",
    "intervals": {
        "io": 1.0,          # janela de medição de CPU/rede/disco (s)
        "quotes": 60.0,     # atualização das cotações (s)
        "procs": 3.0,       # medição dos top processos (s)
        "sysinfo": 5.0,     # medição do ping (s)
        "public_ip": 600.0, # atualização do IP público (s)
        "youtube": 1800.0,  # atualização do YouTube (s)
    },
    "http_timeout": 5.0,
    "quotes": {
        "enabled": True,
        # Moeda em que os preços são cotados e prefixo mostrado no painel
        # (só ASCII — as fontes do display não têm símbolos tipo €/£)
        "base": "USD",
        "prefix": "$",
        # Ordem = ordem das linhas no painel (o painel mostra até 4).
        # fiat   -> AwesomeAPI, par "<symbol>-<base>" (ex.: EUR-USD)
        # crypto -> CoinGecko, "id" é o id da moeda lá (ex.: bitcoin)
        "assets": [
            {"symbol": "EUR", "type": "fiat"},
            {"symbol": "GBP", "type": "fiat"},
            {"symbol": "BTC", "type": "crypto", "id": "bitcoin"},
            {"symbol": "ETH", "type": "crypto", "id": "ethereum"},
        ],
    },
    # Anúncio na rede local: broadcast UDP de {"cydpc": 1, "port", "name"} a cada
    # "interval" s, pra cada interface IPv4 ativa. Só pacote de saída — não
    # precisa de regra de firewall. O painel com IP do agente "auto" usa isso.
    "discovery": {
        "enabled": True,
        "port": 8377,       # porta UDP de destino (DISCOVERY_PORT no firmware)
        "interval": 5.0,
        "name": None,       # nome anunciado (null = hostname do PC)
    },
    "youtube": {
        # Chave da "YouTube Data API v3" (console.cloud.google.com). Também
        # pode vir da variável de ambiente YOUTUBE_API_KEY (tem prioridade).
        "api_key": "",
        "channel_ids": [],
    },
}


def _deep_merge(base, override):
    out = copy.deepcopy(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _deep_merge(out[k], v)
        else:
            out[k] = v
    return out


def load_config(path):
    cfg = DEFAULT_CONFIG
    if path and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            cfg = _deep_merge(DEFAULT_CONFIG, json.load(f))
        print(f"[config] carregado de {path}")
    else:
        print(f"[config] {path} nao encontrado - usando defaults "
              "(copie config.example.json para config.json)")
    env_key = os.environ.get("YOUTUBE_API_KEY")
    if env_key:
        cfg["youtube"]["api_key"] = env_key
    cfg["youtube"]["api_key"] = (cfg["youtube"]["api_key"] or "").strip()
    if not cfg["disk_path"]:
        cfg["disk_path"] = os.path.abspath(os.sep)
    return cfg


# Preenchido em main() antes de subir as threads
CFG = DEFAULT_CONFIG


# ===== Medição de CPU + rede + disco em background (delta por segundo) =====
# cpu_percent(interval=None) mede o uso desde a ÚLTIMA CHAMADA NA MESMA THREAD.
# O ThreadingHTTPServer cria uma thread nova por request, então chamar isso
# direto no handler faz cada request parecer "a primeira chamada" e sempre
# retornar 0.0. Por isso medimos aqui, numa única thread persistente, e o
# handler só lê o valor cacheado.
_io_lock = threading.Lock()
_io_state = {
    "cpu_pct": 0.0,
    "net_down_mbps": 0.0,
    "net_up_mbps": 0.0,
    "disk_read_mbps": 0.0,
    "disk_write_mbps": 0.0,
    "disk_pct": 0.0,
}


def _io_monitor():
    psutil.cpu_percent(interval=None)  # prime (primeira chamada retorna 0, ignorar)
    prev_net = psutil.net_io_counters()
    prev_disk = psutil.disk_io_counters()
    interval = CFG["intervals"]["io"]
    while True:
        time.sleep(interval)
        cpu_pct = psutil.cpu_percent(interval=None)
        cur_net = psutil.net_io_counters()
        down = (cur_net.bytes_recv - prev_net.bytes_recv) * 8 / interval / 1_000_000
        up = (cur_net.bytes_sent - prev_net.bytes_sent) * 8 / interval / 1_000_000
        prev_net = cur_net

        disk_read_mbps = 0.0
        disk_write_mbps = 0.0
        cur_disk = psutil.disk_io_counters()
        if cur_disk is not None and prev_disk is not None:
            disk_read_mbps = (cur_disk.read_bytes - prev_disk.read_bytes) / interval / 1_000_000
            disk_write_mbps = (cur_disk.write_bytes - prev_disk.write_bytes) / interval / 1_000_000
            prev_disk = cur_disk

        try:
            disk_pct = psutil.disk_usage(CFG["disk_path"]).percent
        except OSError:
            disk_pct = 0.0

        with _io_lock:
            _io_state["cpu_pct"] = cpu_pct
            _io_state["net_down_mbps"] = round(down, 2)
            _io_state["net_up_mbps"] = round(up, 2)
            _io_state["disk_read_mbps"] = round(disk_read_mbps, 2)
            _io_state["disk_write_mbps"] = round(disk_write_mbps, 2)
            _io_state["disk_pct"] = round(disk_pct, 1)


# ===== Cotações em background (evita bater na API externa a cada poll do ESP32) =====
_quotes_lock = threading.Lock()
# Preenchido em _init_state() a partir do config: {"symbol": {"price", "change_pct"}}
_quotes_state = {"prices": {}, "updated_at": 0}


def _fetch_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "cyd-pc-dashboard-agent"})
    with urllib.request.urlopen(req, timeout=CFG["http_timeout"]) as resp:
        return json.loads(resp.read().decode())


def _to_ascii(s):
    # As fontes clássicas do painel não têm acentos — "Filósofo" vira "Filosofo"
    return unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()


def _fetch_text(url):
    req = urllib.request.Request(url, headers={"User-Agent": "cyd-pc-dashboard-agent"})
    with urllib.request.urlopen(req, timeout=CFG["http_timeout"]) as resp:
        return resp.read().decode().strip()


# ===== Top processos em background =====
# Mesma pegadinha do cpu_percent global: por-processo ele também mede desde a
# última chamada NAQUELE MESMO objeto Process. O process_iter() cacheia as
# instâncias internamente, então chamar em loop numa thread única funciona.
_procs_lock = threading.Lock()
_procs_state = {"procs": []}


def _procs_monitor():
    n_cpu = psutil.cpu_count() or 1
    skip = {"system idle process", "idle"}
    for p in psutil.process_iter():  # prime (primeira leitura é sempre 0)
        try:
            p.cpu_percent(None)
        except psutil.Error:
            pass
    while True:
        time.sleep(CFG["intervals"]["procs"])
        agg = {}
        for p in psutil.process_iter(attrs=["name", "memory_info"]):
            try:
                cpu = p.cpu_percent(None) / n_cpu
            except psutil.Error:
                continue
            name = p.info["name"] or "?"
            if name.lower() in skip:
                continue
            if name.lower().endswith(".exe"):
                name = name[:-4]
            entry = agg.setdefault(name, [0.0, 0])
            entry[0] += cpu
            mem = p.info["memory_info"]
            if mem:
                entry[1] += mem.rss
        top = sorted(agg.items(), key=lambda kv: kv[1][0], reverse=True)[:CFG["top_procs"]]
        procs = [
            {"name": name[:20], "cpu": round(cpu, 1), "mem_mb": int(rss / 1048576)}
            for name, (cpu, rss) in top
        ]
        with _procs_lock:
            _procs_state["procs"] = procs


# ===== Ping + IP público em background =====
# Latência medida por TCP connect em ping.host:ping.port (default 8.8.8.8:53) (ICMP puro exigiria
# admin no Windows; o connect TCP é um proxy bom o bastante pro painel).
_sys_lock = threading.Lock()
_sys_state = {"ping_ms": -1.0, "public_ip": "?"}


def _sys_monitor():
    last_ip_fetch = 0.0
    ping = CFG["ping"]
    while True:
        t0 = time.perf_counter()
        try:
            conn = socket.create_connection((ping["host"], ping["port"]), timeout=2.0)
            conn.close()
            ping_ms = round((time.perf_counter() - t0) * 1000, 1)
        except OSError:
            ping_ms = -1.0

        public_ip = None
        if CFG["public_ip_url"] and time.time() - last_ip_fetch >= CFG["intervals"]["public_ip"]:
            try:
                public_ip = _fetch_text(CFG["public_ip_url"])
                last_ip_fetch = time.time()
            except OSError as e:
                print(f"[sysinfo] falha ao buscar IP publico: {e}")

        with _sys_lock:
            _sys_state["ping_ms"] = ping_ms
            if public_ip:
                _sys_state["public_ip"] = public_ip

        time.sleep(CFG["intervals"]["sysinfo"])


# ===== Anúncio na rede (descoberta pelo painel) =====
def _broadcast_addrs():
    """Endereço de broadcast de cada interface IPv4 ativa (sem loopback/link-local).

    Broadcast direcionado por interface em vez de 255.255.255.255: no Windows o
    global sai por uma interface só (às vezes a de VPN/Hyper-V).
    """
    stats = psutil.net_if_stats()
    out = set()
    for iface, addrs in psutil.net_if_addrs().items():
        if iface in stats and not stats[iface].isup:
            continue
        for a in addrs:
            if a.family != socket.AF_INET or not a.netmask:
                continue
            if a.address.startswith("127.") or a.address.startswith("169.254."):
                continue
            net = ipaddress.IPv4Network(f"{a.address}/{a.netmask}", strict=False)
            if net.prefixlen < 31:
                out.add(str(net.broadcast_address))
    return out or {"255.255.255.255"}


def _discovery_beacon():
    disc = CFG["discovery"]
    payload = json.dumps({
        "cydpc": 1,
        "port": CFG["port"],
        "name": _to_ascii(disc["name"] or socket.gethostname())[:31],
        "auth": bool(CFG["token"]),  # só avisa que exige token, nunca o token
    }).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    failing = set()
    while True:
        for addr in _broadcast_addrs():
            try:
                sock.sendto(payload, (addr, disc["port"]))
                failing.discard(addr)
            except OSError as e:
                if addr not in failing:  # loga uma vez por endereço, não a cada 5s
                    print(f"[discovery] falha ao anunciar em {addr}: {e}")
                    failing.add(addr)
        time.sleep(disc["interval"])


# ===== YouTube em background =====
_yt_lock = threading.Lock()
_yt_state = {"configured": False, "channels": [], "updated_at": 0}


def _youtube_monitor():
    yt = CFG["youtube"]
    if not (yt["api_key"] and yt["channel_ids"]):
        return
    url = (
        "https://www.googleapis.com/youtube/v3/channels"
        "?part=snippet,statistics&id=" + ",".join(yt["channel_ids"])
        + "&key=" + yt["api_key"]
    )
    while True:
        try:
            data = _fetch_json(url)
            channels = []
            for item in data.get("items", []):
                stats = item.get("statistics", {})
                channels.append({
                    "title": _to_ascii(item.get("snippet", {}).get("title", "?"))[:24],
                    "subs": int(stats.get("subscriberCount", 0)),
                    "views": int(stats.get("viewCount", 0)),
                    "videos": int(stats.get("videoCount", 0)),
                })
            with _yt_lock:
                _yt_state["channels"] = channels
                _yt_state["updated_at"] = int(time.time())
        except (OSError, ValueError, KeyError) as e:
            # Mantém os últimos valores conhecidos; só loga o problema
            print(f"[youtube] falha ao atualizar: {e}")

        time.sleep(CFG["intervals"]["youtube"])


def _fetch_fiat(symbols, base):
    """AwesomeAPI: {symbol: (preço, variação % do dia)}."""
    if not symbols:
        return {}
    pairs = ",".join(f"{sym}-{base}" for sym in symbols)
    data = _fetch_json(f"https://economia.awesomeapi.com.br/json/last/{pairs}")
    out = {}
    for sym in symbols:
        q = data.get(f"{sym}{base}")
        if q:
            out[sym] = (float(q["bid"]), float(q["pctChange"]))
        else:
            print(f"[quotes] par {sym}-{base} nao retornado pela AwesomeAPI")
    return out


def _fetch_crypto(assets, base):
    """CoinGecko: {symbol: (preço, variação % 24h)}. assets = [(symbol, id)]."""
    if not assets:
        return {}
    vs = base.lower()
    ids = ",".join(cid for _, cid in assets)
    data = _fetch_json(
        "https://api.coingecko.com/api/v3/simple/price"
        f"?ids={ids}&vs_currencies={vs}&include_24hr_change=true"
    )
    out = {}
    for sym, cid in assets:
        q = data.get(cid, {})
        if vs in q:
            out[sym] = (float(q[vs]), float(q.get(f"{vs}_24h_change") or 0.0))
        else:
            print(f"[quotes] {cid} em {base} nao retornado pela CoinGecko")
    return out


def _quotes_monitor():
    qcfg = CFG["quotes"]
    if not qcfg["enabled"]:
        return
    base = qcfg["base"].upper()
    fiat = [a["symbol"].upper() for a in qcfg["assets"] if a.get("type") == "fiat"]
    crypto = [(a["symbol"].upper(), a["id"]) for a in qcfg["assets"] if a.get("type") == "crypto"]
    while True:
        # Cada fonte falha de forma independente; mantém o último valor conhecido
        fetched = {}
        for name, fn, arg in (("fiat", _fetch_fiat, fiat), ("crypto", _fetch_crypto, crypto)):
            try:
                fetched.update(fn(arg, base))
            except (OSError, ValueError, KeyError, TypeError) as e:
                print(f"[quotes] falha ao atualizar {name}: {e}")

        if fetched:
            with _quotes_lock:
                for sym, (price, pct) in fetched.items():
                    _quotes_state["prices"][sym] = {
                        "price": round(price, 6 if price < 1 else 4 if price < 1000 else 2),
                        "change_pct": round(pct, 2),
                    }
                _quotes_state["updated_at"] = int(time.time())

        time.sleep(CFG["intervals"]["quotes"])


def _build_quotes():
    qcfg = CFG["quotes"]
    with _quotes_lock:
        prices = dict(_quotes_state["prices"])
        updated_at = _quotes_state["updated_at"]
    items = []
    if qcfg["enabled"]:
        for a in qcfg["assets"]:
            sym = a["symbol"].upper()
            q = prices.get(sym, {})
            # price null = ainda não buscado (o painel não desenha a linha)
            items.append({"symbol": sym, "price": q.get("price"),
                          "change_pct": q.get("change_pct", 0.0)})
    return {"base": qcfg["base"].upper(), "prefix": qcfg["prefix"],
            "items": items, "updated_at": updated_at}


class StatsHandler(BaseHTTPRequestHandler):
    def _authorized(self, query):
        token = CFG["token"]
        if not token:
            return True
        given = self.headers.get("X-Agent-Token") or (query.get("token") or [""])[0]
        return hmac.compare_digest(given.encode(), token.encode())

    def do_GET(self):
        url = urlsplit(self.path)
        path = url.path
        if not self._authorized(parse_qs(url.query)):
            self._reply({"error": "invalid or missing token"}, status=401)
            return
        if path == "/stats":
            self._reply(self._build_stats())
        elif path == "/quotes":
            self._reply(_build_quotes())
        elif path == "/procs":
            with _procs_lock:
                self._reply({"procs": list(_procs_state["procs"])})
        elif path == "/sysinfo":
            with _sys_lock:
                info = dict(_sys_state)
            info["uptime_s"] = int(time.time() - psutil.boot_time())
            with _io_lock:
                info["net_down_mbps"] = _io_state["net_down_mbps"]
                info["net_up_mbps"] = _io_state["net_up_mbps"]
            self._reply(info)
        elif path == "/youtube":
            with _yt_lock:
                self._reply({
                    "configured": _yt_state["configured"],
                    "channels": list(_yt_state["channels"]),
                    "updated_at": _yt_state["updated_at"],
                })
        else:
            self.send_response(404)
            self.end_headers()

    def _build_stats(self):
        with _io_lock:
            io = dict(_io_state)

        return {
            "cpu": io["cpu_pct"],
            "ram": psutil.virtual_memory().percent,
            "disk": io["disk_pct"],
            "disk_read_mbps": io["disk_read_mbps"],
            "disk_write_mbps": io["disk_write_mbps"],
            "net_down_mbps": io["net_down_mbps"],
            "net_up_mbps": io["net_up_mbps"],
        }

    def _reply(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass  # silencia log de cada request


def main():
    global CFG
    parser = argparse.ArgumentParser(description="Agente HTTP do CYD PC Dashboard")
    parser.add_argument("--config", default=os.path.join(HERE, "config.json"),
                        help="caminho do config.json (default: ao lado do script)")
    parser.add_argument("--port", type=int, help="sobrescreve a porta do config")
    parser.add_argument("--log", help="grava a saída nesse arquivo (útil com pythonw, que não tem console)")
    args = parser.parse_args()

    if args.log:
        log = open(args.log, "a", encoding="utf-8", buffering=1)
        sys.stdout = sys.stderr = log
    elif sys.stdout is not None:
        # Logs aparecem na hora mesmo com stdout redirecionado (janela do .bat, arquivo)
        sys.stdout.reconfigure(line_buffering=True)

    CFG = load_config(args.config)
    if args.port:
        CFG["port"] = args.port
    CFG["token"] = os.environ.get("CYDPC_AGENT_TOKEN") or CFG["token"] or ""
    _yt_state["configured"] = bool(CFG["youtube"]["api_key"] and CFG["youtube"]["channel_ids"])

    targets = [_io_monitor, _quotes_monitor, _procs_monitor, _sys_monitor, _youtube_monitor]
    if CFG["discovery"]["enabled"]:
        targets.append(_discovery_beacon)
    for target in targets:
        threading.Thread(target=target, daemon=True).start()

    try:
        server = ThreadingHTTPServer((CFG["bind"], CFG["port"]), StatsHandler)
    except OSError as e:
        sys.exit(f"Nao foi possivel abrir {CFG['bind']}:{CFG['port']} - {e}")
    print(f"Agente rodando em http://{CFG['bind']}:{CFG['port']} "
          "(/stats /quotes /procs /sysinfo /youtube)"
          + (" com token" if CFG["token"] else " sem token"))
    server.serve_forever()


if __name__ == "__main__":
    main()
