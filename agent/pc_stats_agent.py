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
import base64
import concurrent.futures
import copy
import fnmatch
import hmac
import ipaddress
import json
import os
import socket
import re
import shlex
import ssl
import subprocess
import sys
import time
import threading
import unicodedata
import urllib.request
import urllib.error
import urllib.parse
from urllib.parse import parse_qs, urlsplit
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import psutil

# Pasta do agente: a do script, ou a do .exe quando empacotado com PyInstaller
# (no "onefile" o __file__ fica numa pasta temporária, apagada a cada execução
# — config.json, agent.log e o histórico das cotações têm que ficar ao lado do .exe)
HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))

DEFAULT_CONFIG = {
    "bind": "0.0.0.0",
    "port": 8377,
    # Token compartilhado com o painel (vazio = sem autenticação). A env
    # CYDPC_AGENT_TOKEN tem prioridade. Vai em HTTP puro na LAN: protege contra
    # acesso casual de outros dispositivos da rede, não contra quem captura tráfego.
    "token": "",
    # Partição cujo uso (%) aparece no painel. null = raiz do sistema ("C:\\" ou "/")
    "disk_path": None,
    # Vários discos no painel (o card alterna entre eles). null = só disk_path;
    # "all" = todas as partições fixas (sem removível/CD, reavaliado a cada 30s);
    # ou lista: ["C:\\", {"path": "D:\\", "name": "GAMES"}]. Nomes: ASCII, até 8 chars.
    "disks": None,
    "top_procs": 4,
    "procs": {
        # Nomes escondidos da aba Processos (sem diferenciar maiúsculas, com ou
        # sem ".exe", curingas * e ?). Ex.: ["svchost", "Memory Compression", "msedge*"]
        "ignore": [],
        # Esconde o próprio agente (pelo PID, não pelo nome — outros processos
        # Python continuam aparecendo)
        "ignore_self": True,
    },
    "ping": {"host": "8.8.8.8", "port": 53},
    # Temperatura da CPU (/stats "cpu_temp", null se não houver fonte).
    # source: "auto" (Linux: psutil; Windows: LibreHardwareMonitor), "psutil",
    # "lhm" ou "off". No Windows o psutil não lê temperatura: rode o
    # LibreHardwareMonitor com Options > Remote Web Server ligado (porta 8085).
    # sensor: trecho do nome do sensor pra forçar um (ex.: "Package", "Tctl").
    "temperature": {
        "source": "auto",
        "lhm_url": "http://localhost:8085/data.json",
        "sensor": None,
    },
    "public_ip_url": "https://api.ipify.org",
    # Aba Serviços: o agente verifica cada um (em paralelo) e o painel mostra
    # verde/vermelho. check: "tcp://host:porta" (conecta) ou "http(s)://..."
    # (resposta < 400). Ex.: {"name": "NAS", "check": "tcp://192.168.1.10:445"}
    "services": [],
    "services_options": {
        "timeout": 5.0,         # s por verificação
        "fail_threshold": 2,    # falhas seguidas pra dar como fora (evita alarme falso)
        "notify": True,         # avisa o painel (POST /api/notify em panel.host) quando cai/volta
    },
    # Aba Atalhos: botões no painel que rodam uma ação NESTE PC (até 8). Cada
    # um tem "name" e UMA destas:
    #   "do":   ação embutida — lock, screen_off, mute, vol_up, vol_down,
    #           play_pause, next, prev
    #   "open": arquivo, pasta, app ou URL (abre com o programa padrão)
    #   "run":  comando (string ou lista), sem shell, sem esperar terminar
    # Opcionais: "icon" (lock, screen_off, mute, vol_up, vol_down, play_pause,
    # next, prev, app, link, terminal, power, bolt) e "color" (cyan, green,
    # orange, pink, purple, red, yellow). O painel só manda o número do botão:
    # nada além do que está aqui pode ser executado. Com ações, use um "token".
    "actions": [],
    # Painel, pra quem precisa falar com ele: agent/notify.py e os avisos de serviço
    "panel": {"host": "cyd-dashboard.local", "password": ""},
    "intervals": {
        "io": 1.0,          # janela de medição de CPU/rede/disco (s)
        "quotes": 60.0,     # atualização das cotações (s)
        "procs": 3.0,       # medição dos top processos (s)
        "sysinfo": 5.0,     # medição do ping (s)
        "temp": 2.0,        # leitura da temperatura da CPU (s)
        "public_ip": 600.0, # atualização do IP público (s)
        "youtube": 1800.0,  # atualização do YouTube (s)
        "services": 30.0,   # verificação dos serviços (s)
    },
    "http_timeout": 5.0,
    "quotes": {
        "enabled": True,
        # Moeda em que os preços são cotados e prefixo mostrado no painel
        # (só ASCII — as fontes do display não têm símbolos tipo €/£)
        "base": "USD",
        "prefix": "$",
        # Histórico de 24h (gráfico de fundo de cada card no painel): cripto já
        # vem pronto do CoinGecko; fiat começa com os pontos esparsos da
        # AwesomeAPI e completa com as próprias consultas. Salvo em disco.
        "history_hours": 24,
        "history_points": 32,
        # Ordem = ordem das linhas no painel (o painel mostra até 4).
        # fiat   -> AwesomeAPI, par "<symbol>-<base>" (ex.: EUR-USD)
        # crypto -> CoinGecko, "id" é o id da moeda lá (ex.: bitcoin)
        # stock  -> Yahoo Finance, "id" é o ticker de lá: "PETR4.SA" (B3),
        #           "AAPL" (EUA), "^BVSP" (índice). Preço na moeda da bolsa;
        #           "prefix" no asset sobrescreve o prefixo daquela linha.
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
    "disks": [],  # [{"name", "pct", "used_gb", "total_gb"}], na ordem do config
}


def _disk_name(path):
    """Nome curto pro painel: "C:\\" -> "C:", "/" -> "/", "/mnt/data" -> "data".
    Aceita / e \\ em qualquer sistema (no Linux o os.path.basename não separa
    "D:\\Games"), pra dar o mesmo resultado onde quer que rode."""
    p = path.rstrip("\\/")
    if len(p) == 2 and p[1] == ":":
        return p.upper()
    return re.split(r"[\\/]", p)[-1] or "/"


def _resolve_disks():
    """Lista [(path, name)] dos discos a mostrar, a partir de "disks"/"disk_path"."""
    spec = CFG["disks"]
    if spec is None:
        return [(CFG["disk_path"], _disk_name(CFG["disk_path"]))]
    if spec == "all":
        out = []
        for part in psutil.disk_partitions(all=False):
            opts = part.opts.split(",")
            if "removable" in opts or "cdrom" in opts or not part.fstype:
                continue
            if part.fstype in ("squashfs", "tmpfs", "devtmpfs", "overlay"):
                continue  # Linux: snaps, RAM, containers
            out.append((part.mountpoint, _disk_name(part.mountpoint)))
        return out or [(CFG["disk_path"], _disk_name(CFG["disk_path"]))]
    out = []
    for item in spec:
        if isinstance(item, str):
            out.append((item, _disk_name(item)))
        elif isinstance(item, dict) and item.get("path"):
            out.append((item["path"], item.get("name") or _disk_name(item["path"])))
    return out


def _disk_usage(disks):
    out = []
    for path, name in disks:
        try:
            u = psutil.disk_usage(path)
        except OSError:
            continue  # disco que sumiu (USB desplugado, caminho errado)
        out.append({
            "name": _to_ascii(name)[:8].rstrip(),
            "pct": round(u.percent, 1),
            "used_gb": round(u.used / 1e9, 1),
            "total_gb": round(u.total / 1e9, 1),
        })
    return out


def _io_monitor():
    psutil.cpu_percent(interval=None)  # prime (primeira chamada retorna 0, ignorar)
    prev_net = psutil.net_io_counters()
    prev_disk = psutil.disk_io_counters()
    interval = CFG["intervals"]["io"]
    disks, disks_resolved_at = [], 0.0
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

        if time.time() - disks_resolved_at >= 30:
            disks, disks_resolved_at = _resolve_disks(), time.time()
        disk_list = _disk_usage(disks)
        # "disk" (compatibilidade com firmware antigo) = o primeiro da lista
        disk_pct = disk_list[0]["pct"] if disk_list else 0.0

        with _io_lock:
            _io_state["cpu_pct"] = cpu_pct
            _io_state["net_down_mbps"] = round(down, 2)
            _io_state["net_up_mbps"] = round(up, 2)
            _io_state["disk_read_mbps"] = round(disk_read_mbps, 2)
            _io_state["disk_write_mbps"] = round(disk_write_mbps, 2)
            _io_state["disk_pct"] = round(disk_pct, 1)
            _io_state["disks"] = disk_list


# ===== Cotações em background (evita bater na API externa a cada poll do ESP32) =====
_quotes_lock = threading.Lock()
# Preenchido em _init_state() a partir do config: {"symbol": {"price", "change_pct"}}
_quotes_state = {"prices": {}, "updated_at": 0}
# {symbol: [(timestamp, preço), ...]} em ordem cronológica, últimas history_hours
_quotes_hist = {}
_HIST_FILE = os.path.join(HERE, "quotes_history.json")


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


def _strip_exe(name):
    return name[:-4] if name.endswith(".exe") else name


def _self_pids():
    """PID do agente + o pai, se for o launcher do Python (py.exe / Python
    Install Manager / pythonw que reabre a si mesmo) — é a mesma instância."""
    pids = {os.getpid()}
    try:
        parent = psutil.Process().parent()
        name = _strip_exe(parent.name().lower()) if parent else ""
        if name in ("py", "pyw") or name.startswith("python"):
            pids.add(parent.pid)
    except psutil.Error:
        pass
    return pids


def _proc_ignored(name, patterns):
    """name já sem ".exe" e em minúsculas; patterns idem."""
    return any(fnmatch.fnmatchcase(name, pat) for pat in patterns)


def _procs_monitor():
    n_cpu = psutil.cpu_count() or 1
    # O "processo" ocioso do sistema mede CPU livre, não uso — sempre fora
    skip = {"system idle process", "idle"}
    patterns = [_strip_exe(p.lower()) for p in CFG["procs"]["ignore"] if p]
    self_pids = _self_pids() if CFG["procs"]["ignore_self"] else set()
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
            if p.pid in self_pids:
                continue
            name = p.info["name"] or "?"
            if name.lower() in skip:
                continue
            if name.lower().endswith(".exe"):
                name = name[:-4]
            if patterns and _proc_ignored(name.lower(), patterns):
                continue
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


# ===== Temperatura da CPU em background =====
_temp_lock = threading.Lock()
_temp_state = {"cpu_temp": None, "source": None}

# Sensores preferidos, nesta ordem (nome contém): pacote/die inteiro antes de núcleo
_TEMP_PREFERRED = ("package", "tctl", "tdie", "core average", "core max")


def _pick_sensor(candidates, forced):
    """candidates = [(nome, °C)]. Escolhe pelo nome forçado, pelos preferidos
    ou, sem nenhum deles, o mais quente."""
    if not candidates:
        return None
    if forced:
        for name, value in candidates:
            if forced.lower() in name.lower():
                return value
    for pref in _TEMP_PREFERRED:
        for name, value in candidates:
            if pref in name.lower():
                return value
    return max(value for _, value in candidates)


def _temp_psutil(forced):
    read = getattr(psutil, "sensors_temperatures", None)
    if read is None:  # Windows/macOS: não existe
        return None
    try:
        groups = read() or {}
    except (OSError, RuntimeError):
        return None
    # Drivers de CPU conhecidos primeiro; sem eles, qualquer sensor
    for key in ("coretemp", "k10temp", "zenpower", "cpu_thermal", "cpu-thermal", "acpitz"):
        if key in groups:
            cand = [(e.label or key, e.current) for e in groups[key] if e.current]
            value = _pick_sensor(cand, forced)
            if value is not None:
                return value
    cand = [(e.label or k, e.current) for k, entries in groups.items() for e in entries if e.current]
    return _pick_sensor(cand, forced)


_LHM_NUMBER = re.compile(r"-?\d+(?:[.,]\d+)?")


def _lhm_cpu_temps(node, in_cpu=False, out=None):
    """Percorre a árvore do data.json do LibreHardwareMonitor e junta
    (nome, °C) dos sensores de temperatura que estão sob um hardware de CPU."""
    if out is None:
        out = []
    sensor_id = node.get("SensorId") or ""
    image = node.get("ImageURL") or ""
    # Hardware de CPU: ícone cpu.png (todas as versões) ou SensorId /intelcpu/, /amdcpu/
    in_cpu = in_cpu or image.endswith("cpu.png") or "cpu/" in sensor_id
    is_temp = (node.get("Type") == "Temperature" or "/temperature/" in sensor_id
               or (not node.get("Children") and "°C" in (node.get("Value") or "")))
    if in_cpu and is_temp:
        m = _LHM_NUMBER.search(node.get("Value") or "")
        if m:  # "45.3 °C" ou "45,3 °C" (Windows em português)
            out.append((node.get("Text") or "", float(m.group().replace(",", "."))))
    for child in node.get("Children") or []:
        _lhm_cpu_temps(child, in_cpu, out)
    return out


def _temp_lhm(url, forced):
    with urllib.request.urlopen(url, timeout=1.5) as r:
        tree = json.load(r)
    return _pick_sensor(_lhm_cpu_temps(tree), forced)


def _temp_monitor():
    cfg = CFG["temperature"]
    source = cfg["source"]
    if source == "off":
        return
    if source == "auto":
        source = "lhm" if sys.platform == "win32" else "psutil"
    warned = False
    while True:
        value = None
        try:
            if source == "lhm":
                value = _temp_lhm(cfg["lhm_url"], cfg["sensor"])
            else:
                value = _temp_psutil(cfg["sensor"])
        except (OSError, ValueError) as e:
            if not warned:  # LHM fechado: avisa uma vez e segue tentando
                print(f"[temp] sem temperatura via {source} ({e}). No Windows, rode o "
                      "LibreHardwareMonitor com Options > Remote Web Server ligado.")
                warned = True
        else:
            if value is not None and warned:
                print(f"[temp] temperatura disponivel via {source}")
                warned = False
        with _temp_lock:
            _temp_state["cpu_temp"] = round(value, 1) if value is not None else None
            _temp_state["source"] = source if value is not None else None
        # Sem fonte: tenta de novo a cada 30s (o LHM pode ser aberto depois)
        time.sleep(CFG["intervals"]["temp"] if value is not None else 30)


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


# ===== Serviços (aba Serviços) =====
_services_lock = threading.Lock()
# Um item por serviço do config, na ordem: name, ok (True/False/None = ainda
# não verificado), ms, detail (motivo da falha), since (quando mudou de estado)
_services_state = {"services": [], "updated_at": 0}


def _short_reason(e):
    """Motivo curto da falha pro card do painel (cabe em ~11 caracteres) —
    a mensagem crua do SO ("[WinError 10061] No connection could be...") não cabe."""
    e = getattr(e, "reason", None) if isinstance(e, urllib.error.URLError) else e
    if isinstance(e, (socket.timeout, TimeoutError)):
        return "timeout"
    if isinstance(e, ConnectionRefusedError):
        return "refused"
    if isinstance(e, socket.gaierror):
        return "no DNS"
    if isinstance(e, ssl.SSLError):
        return "SSL error"
    if isinstance(e, (ConnectionResetError, ConnectionAbortedError)):
        return "reset"
    if isinstance(e, OSError):
        return "unreachable"
    return "error"


def _check_service(check, timeout):
    """(ok, ms, detail). tcp://host:porta conecta; http(s):// quer status < 400."""
    t0 = time.perf_counter()
    try:
        url = urlsplit(check)
        if url.scheme == "tcp":
            if not url.hostname or not url.port:
                print(f"[services] {check}: use tcp://host:porta")
                return False, None, "bad check"
            socket.create_connection((url.hostname, url.port), timeout=timeout).close()
        elif url.scheme in ("http", "https"):
            req = urllib.request.Request(check, headers={"User-Agent": "cyd-pc-dashboard-agent"})
            try:
                with urllib.request.urlopen(req, timeout=timeout) as resp:
                    resp.read(1)
            except urllib.error.HTTPError as e:
                if e.code >= 400:
                    return False, None, f"HTTP {e.code}"
        else:
            print(f"[services] {check}: o check precisa de tcp:// ou http(s)://")
            return False, None, "bad check"
    except ValueError:
        return False, None, "bad check"
    except OSError as e:
        return False, None, _short_reason(e)
    return True, round((time.perf_counter() - t0) * 1000), ""


def _notify_panel(title, text, level):
    """Aviso no painel (mesmo POST /api/notify do notify.py). Painel fora: só loga."""
    panel = CFG["panel"]
    body = json.dumps({"title": title, "text": text, "level": level, "duration": 0}).encode()
    req = urllib.request.Request(f"http://{panel['host']}/api/notify", data=body, method="POST",
                                 headers={"Content-Type": "application/json"})
    if panel.get("password"):
        cred = base64.b64encode(f"admin:{panel['password']}".encode()).decode()
        req.add_header("Authorization", f"Basic {cred}")
    try:
        urllib.request.urlopen(req, timeout=5).read()
    except OSError as e:
        print(f"[services] nao consegui avisar o painel em {panel['host']}: {e}")


def _services_round(prev, fails):
    """Uma rodada: verifica todos em paralelo e aplica o limite de falhas.
    prev/fails: estado e falhas seguidas da rodada anterior (por nome).
    Devolve (lista nova, eventos [(nome, "down"|"up", detalhe)])."""
    services = [s for s in CFG["services"] if s.get("name") and s.get("check")]
    opts = CFG["services_options"]
    if not services:
        return [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, len(services))) as pool:
        results = list(pool.map(lambda s: _check_service(s["check"], opts["timeout"]), services))
    now, out, events = int(time.time()), [], []
    for svc, (ok, ms, detail) in zip(services, results):
        name = _to_ascii(svc["name"])[:20]
        old = prev.get(name, {"ok": None, "since": now})
        fails[name] = 0 if ok else fails.get(name, 0) + 1
        # Só cai depois de fail_threshold falhas seguidas; volta na primeira
        # resposta. Até cair, mantém o estado anterior (None = ainda não sabe).
        state = True if ok else (False if fails[name] >= opts["fail_threshold"] else old["ok"])
        since = now if state != old["ok"] else old["since"]
        if old["ok"] is not None and state is not None and state != old["ok"]:
            events.append((name, "up" if state else "down", detail))
        out.append({"name": name, "ok": state, "ms": ms, "detail": detail, "since": since})
    return out, events


def _services_monitor():
    if not CFG["services"]:
        return
    fails = {}
    while True:
        with _services_lock:
            prev = {s["name"]: s for s in _services_state["services"]}
        services, events = _services_round(prev, fails)
        with _services_lock:
            _services_state["services"] = services
            _services_state["updated_at"] = int(time.time())
        for name, kind, detail in events:
            print(f"[services] {name}: {'fora' if kind == 'down' else 'de volta'} {detail}")
            if CFG["services_options"]["notify"]:
                if kind == "down":
                    _notify_panel(f"{name} is down", f"not responding ({detail})" if detail else "not responding", "error")
                else:
                    _notify_panel(f"{name} is back", "responding again", "ok")
        time.sleep(CFG["intervals"]["services"])


# ===== Atalhos (aba Atalhos do painel) =====
ACTION_BUILTINS = ("lock", "screen_off", "mute", "vol_up", "vol_down", "play_pause", "next", "prev")
ACTION_ICONS = ACTION_BUILTINS + ("app", "link", "terminal", "power", "bolt")
ACTION_COLORS = ("cyan", "green", "orange", "pink", "purple", "red", "yellow")
ACTION_COOLDOWN_S = 1.0  # toque repetido no mesmo botão dentro disso é ignorado
_actions_lock = threading.Lock()
_actions_last = {}  # índice -> time.monotonic() da última execução


def _action_list(log=False):
    """Ações válidas do config, já normalizadas: [{name, icon, color, kind, target}].
    O índice nessa lista é o id que o painel manda no POST."""
    out = []
    for i, a in enumerate(CFG["actions"]):
        kinds = [k for k in ("do", "open", "run") if isinstance(a, dict) and a.get(k)]
        if not isinstance(a, dict) or not a.get("name") or len(kinds) != 1:
            if log:
                print(f'[actions] item {i} ignorado: precisa de "name" e de um (so um) entre do/open/run')
            continue
        kind, target = kinds[0], a[kinds[0]]
        if kind == "do" and target not in ACTION_BUILTINS:
            if log:
                print(f'[actions] {a["name"]}: "do" desconhecido ({target}); use {", ".join(ACTION_BUILTINS)}')
            continue
        if kind == "do":
            icon, color = target, "cyan"
        elif kind == "open":
            icon, color = ("link" if str(target).startswith(("http://", "https://")) else "app"), "green"
        else:
            icon, color = "terminal", "orange"
        if a.get("icon") in ACTION_ICONS:
            icon = a["icon"]
        if a.get("color") in ACTION_COLORS:
            color = a["color"]
        out.append({"name": _to_ascii(str(a["name"]))[:14], "icon": icon, "color": color,
                    "kind": kind, "target": target})
    return out[:8]


def _win_key(vk):
    """Aperta e solta uma tecla de mídia/volume (as mesmas do teclado multimídia)."""
    import ctypes
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, 1, 0)      # KEYEVENTF_EXTENDEDKEY
    user32.keybd_event(vk, 0, 1 | 2, 0)  # + KEYEVENTF_KEYUP


_WIN_VK = {"mute": 0xAD, "vol_down": 0xAE, "vol_up": 0xAF, "next": 0xB0, "prev": 0xB1, "play_pause": 0xB3}
_OSA_VOL = "set volume output volume ((output volume of (get volume settings)) {} 6)"
_POSIX_BUILTINS = {
    "Linux": {
        "lock": ["loginctl", "lock-session"],
        "screen_off": ["xset", "dpms", "force", "off"],
        "mute": ["pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle"],
        "vol_up": ["pactl", "set-sink-volume", "@DEFAULT_SINK@", "+5%"],
        "vol_down": ["pactl", "set-sink-volume", "@DEFAULT_SINK@", "-5%"],
        "play_pause": ["playerctl", "play-pause"],
        "next": ["playerctl", "next"],
        "prev": ["playerctl", "previous"],
    },
    "Darwin": {
        "lock": ["pmset", "displaysleepnow"],   # bloqueia se o Mac pede senha ao acordar
        "screen_off": ["pmset", "displaysleepnow"],
        "mute": ["osascript", "-e", "set volume output muted (not (output muted of (get volume settings)))"],
        "vol_up": ["osascript", "-e", _OSA_VOL.format("+")],
        "vol_down": ["osascript", "-e", _OSA_VOL.format("-")],
    },
}


def _run_builtin(name):
    if os.name == "nt":
        import ctypes
        user32 = ctypes.windll.user32
        if name == "lock":
            return (True, "") if user32.LockWorkStation() else (False, "failed")
        if name == "screen_off":
            # HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2 = desligar
            user32.PostMessageW(0xFFFF, 0x0112, 0xF170, 2)
            return True, ""
        _win_key(_WIN_VK[name])
        return True, ""
    cmd = _POSIX_BUILTINS.get(os.uname().sysname, {}).get(name)
    if not cmd:
        return False, "n/a"  # não existe nesse sistema
    try:
        r = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=5)
    except FileNotFoundError:
        print(f"[actions] {name}: {cmd[0]} nao encontrado")
        return False, "missing"
    except subprocess.TimeoutExpired:
        return False, "timeout"
    return (True, "") if r.returncode == 0 else (False, "failed")


def _spawn(args):
    """Inicia sem esperar terminar, sem janela de console e solto do agente."""
    kw = {"cwd": HERE, "stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL,
          "stderr": subprocess.DEVNULL, "close_fds": True}
    if os.name == "nt":
        kw["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        kw["start_new_session"] = True
    subprocess.Popen(args, **kw)


def _run_action(action):
    """Executa uma ação de _action_list(). Devolve (ok, erro pro painel — até 8
    letras, cabe no botão: missing, failed, timeout, n/a)."""
    kind, target = action["kind"], action["target"]
    try:
        if kind == "do":
            return _run_builtin(target)
        if kind == "open":
            if os.name == "nt":
                os.startfile(target)
            else:
                _spawn(["open" if os.uname().sysname == "Darwin" else "xdg-open", str(target)])
        else:
            if isinstance(target, list):
                args = [str(x) for x in target]
            else:
                # No Windows a string vai direto pro CreateProcess (aspas como no cmd)
                args = target if os.name == "nt" else shlex.split(target)
            _spawn(args)
    except FileNotFoundError:
        print(f"[actions] {action['name']}: nao encontrado ({target})")
        return False, "missing"
    except (OSError, ValueError) as e:
        print(f"[actions] {action['name']}: {e}")
        return False, "failed"
    return True, ""


def _trigger_action(index):
    """(status HTTP, resposta) pro POST /actions/<n>."""
    actions = _action_list()
    if not 0 <= index < len(actions):
        return 404, {"ok": False, "error": "unknown"}
    now = time.monotonic()
    with _actions_lock:
        if now - _actions_last.get(index, -ACTION_COOLDOWN_S) < ACTION_COOLDOWN_S:
            return 429, {"ok": False, "error": "busy"}
        _actions_last[index] = now
    action = actions[index]
    ok, error = _run_action(action)
    print(f"[actions] {action['name']}: {'ok' if ok else error}")
    return (200, {"ok": True}) if ok else (500, {"ok": False, "error": error})


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


# Todas as funções _hist_* esperam _quotes_lock já adquirido (o histórico é
# escrito pela thread de cotações e lido pelas threads HTTP)
def _hist_add(sym, ts, price):
    pts = _quotes_hist.setdefault(sym, [])
    if pts and ts <= pts[-1][0]:
        return  # mais antigo que o último (seed chegando atrasado): ignora
    pts.append((ts, price))


def _hist_trim():
    cutoff = time.time() - CFG["quotes"]["history_hours"] * 3600
    for sym in list(_quotes_hist):
        _quotes_hist[sym] = [p for p in _quotes_hist[sym] if p[0] >= cutoff]


def _hist_load():
    try:
        with open(_HIST_FILE, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return
    if data.get("base") != CFG["quotes"]["base"].upper():
        return  # moeda base mudou: o histórico salvo não vale mais
    with _quotes_lock:
        for sym, pts in (data.get("history") or {}).items():
            _quotes_hist[sym] = [(float(t), float(p)) for t, p in pts]
        _hist_trim()


def _hist_save():
    tmp = _HIST_FILE + ".tmp"
    try:
        with _quotes_lock:
            snapshot = {sym: list(pts) for sym, pts in _quotes_hist.items()}
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump({"base": CFG["quotes"]["base"].upper(), "history": snapshot}, f)
        os.replace(tmp, _HIST_FILE)
    except OSError as e:
        print(f"[quotes] falha ao salvar historico: {e}")


def _hist_seed(fiat, crypto, base):
    """Preenche o histórico ao subir: 24h do CoinGecko pra cripto (pontos de
    5 min) e as cotações recentes da AwesomeAPI pra fiat (esparsas)."""
    hours = CFG["quotes"]["history_hours"]
    cutoff = time.time() - hours * 3600
    days = max(1, -(-hours // 24))
    for sym, cid in crypto:
        if len(_quotes_hist.get(sym, [])) > 10:
            continue  # já veio do arquivo
        try:
            data = _fetch_json(f"https://api.coingecko.com/api/v3/coins/{cid}/market_chart"
                               f"?vs_currency={base.lower()}&days={days}")
            with _quotes_lock:
                for ms, price in data.get("prices", []):
                    if ms / 1000 >= cutoff:
                        _hist_add(sym, ms / 1000, float(price))
        except (OSError, ValueError, KeyError, TypeError) as e:
            print(f"[quotes] sem historico inicial de {sym}: {e}")
        time.sleep(2)  # plano gratuito do CoinGecko limita requests por minuto
    for sym in fiat:
        if len(_quotes_hist.get(sym, [])) > 10:
            continue
        try:
            data = _fetch_json(f"https://economia.awesomeapi.com.br/json/{sym}-{base}/100")
            pts = sorted((float(q["timestamp"]), float(q["bid"])) for q in data)
            with _quotes_lock:
                for ts, price in pts:
                    if ts >= cutoff:
                        _hist_add(sym, ts, price)
        except (OSError, ValueError, KeyError, TypeError) as e:
            print(f"[quotes] sem historico inicial de {sym}: {e}")


def _hist_series(sym):
    """history_points valores igualmente espaçados no TEMPO (não por índice:
    o seed de 5 min e as consultas de 1 min não podem pesar diferente). Último
    preço de cada janela; janela vazia repete o anterior; antes do primeiro
    dado não tem ponto (o gráfico começa mais tarde)."""
    pts = _quotes_hist.get(sym) or []
    n = CFG["quotes"]["history_points"]
    if len(pts) < 2:
        return []
    end = time.time()
    span = CFG["quotes"]["history_hours"] * 3600
    start = end - span
    out, i, last = [], 0, None
    for b in range(n):
        edge = start + span * (b + 1) / n
        while i < len(pts) and pts[i][0] <= edge:
            last = pts[i][1]
            i += 1
        if last is not None:
            out.append(last)
    return out if len(out) >= 2 else []


# Ações: o gráfico é o pregão do dia (vem pronto do Yahoo a cada consulta), não
# o histórico de 24h montado pelo agente
_stock_hist = {}

# Prefixo quando a moeda da ação é diferente da base (ASCII: a fonte do painel
# não tem €/£/¥)
_CURRENCY_PREFIX = {"USD": "$", "BRL": "R$", "EUR": "EUR ", "GBP": "GBP ", "JPY": "JPY ", "CAD": "C$"}


def _downsample(values, n):
    """n valores igualmente espaçados (pelo índice) de uma lista em ordem."""
    if len(values) <= n:
        return list(values)
    return [values[round(i * (len(values) - 1) / (n - 1))] for i in range(n)]


def _fetch_stocks(assets, base):
    """Yahoo Finance (chart API, sem chave): {symbol: (preço, variação % do dia,
    pregão do dia, prefixo ou None)}. assets = [(symbol, ticker, prefixo do config ou None)].
    Cada ação falha sozinha (ticker errado não derruba as outras)."""
    out = {}
    for sym, ticker, prefix in assets:
        try:
            data = _fetch_json("https://query1.finance.yahoo.com/v8/finance/chart/"
                               f"{urllib.parse.quote(ticker)}?range=1d&interval=15m")
            res = (data.get("chart") or {}).get("result")
            if not res:
                err = (data.get("chart") or {}).get("error") or {}
                print(f"[quotes] {ticker}: {err.get('description', 'sem dados')}")
                continue
            meta = res[0]["meta"]
            price = float(meta["regularMarketPrice"])
            prev = float(meta.get("chartPreviousClose") or meta.get("previousClose") or price)
            closes = [c for c in res[0]["indicators"]["quote"][0].get("close") or [] if c is not None]
            if prefix is None:
                cur = (meta.get("currency") or "").upper()
                if ticker.startswith("^"):
                    prefix = ""  # índice: pontos, não dinheiro
                elif cur and cur != base:
                    prefix = _CURRENCY_PREFIX.get(cur, cur + " ")
            pct = (price - prev) / prev * 100 if prev else 0.0
            out[sym] = (price, pct, _downsample(closes, CFG["quotes"]["history_points"]), prefix)
        except (OSError, ValueError, KeyError, TypeError, IndexError) as e:
            print(f"[quotes] falha ao atualizar {ticker}: {e}")
    return out


def _quotes_monitor():
    qcfg = CFG["quotes"]
    if not qcfg["enabled"]:
        return
    base = qcfg["base"].upper()
    fiat = [a["symbol"].upper() for a in qcfg["assets"] if a.get("type") == "fiat"]
    crypto = [(a["symbol"].upper(), a["id"]) for a in qcfg["assets"] if a.get("type") == "crypto"]
    stocks = [(a["symbol"].upper(), a.get("id") or a["symbol"], a.get("prefix"))
              for a in qcfg["assets"] if a.get("type") == "stock"]
    _hist_load()
    _hist_seed(fiat, crypto, base)
    polls = 0
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
                now = time.time()
                for sym, (price, _) in fetched.items():
                    _hist_add(sym, now, price)
                _hist_trim()
            polls += 1
            if polls % 10 == 1:  # salva no 1º poll e depois a cada ~10 min
                _hist_save()

        if stocks:
            got = _fetch_stocks(stocks, base)
            with _quotes_lock:
                for sym, (price, pct, hist, prefix) in got.items():
                    _quotes_state["prices"][sym] = {
                        "price": round(price, 4 if price < 1000 else 2),
                        "change_pct": round(pct, 2),
                        "prefix": prefix,
                    }
                    _stock_hist[sym] = hist
                if got:
                    _quotes_state["updated_at"] = int(time.time())

        time.sleep(CFG["intervals"]["quotes"])


def _build_quotes():
    qcfg = CFG["quotes"]
    with _quotes_lock:
        prices = dict(_quotes_state["prices"])
        updated_at = _quotes_state["updated_at"]
        history = {a["symbol"].upper(): (list(_stock_hist.get(a["symbol"].upper(), []))
                                         if a.get("type") == "stock" else _hist_series(a["symbol"].upper()))
                   for a in qcfg["assets"]}
    items = []
    if qcfg["enabled"]:
        for a in qcfg["assets"]:
            sym = a["symbol"].upper()
            q = prices.get(sym, {})
            # price null = ainda não buscado (o painel não desenha a linha)
            item = {"symbol": sym, "price": q.get("price"),
                    "change_pct": q.get("change_pct", 0.0),
                    "history": history.get(sym, [])}
            if q.get("prefix") is not None:
                item["prefix"] = q["prefix"]  # sobrescreve o prefixo global nessa linha
            items.append(item)
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
        elif path == "/services":
            with _services_lock:
                self._reply({"configured": bool(CFG["services"]),
                             "services": [dict(s) for s in _services_state["services"]],
                             "updated_at": _services_state["updated_at"]})
        elif path == "/actions":
            actions = _action_list()
            self._reply({"configured": bool(actions),
                         "actions": [{k: a[k] for k in ("name", "icon", "color")} for a in actions]})
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

    def do_POST(self):
        url = urlsplit(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        if length:
            self.rfile.read(min(length, 4096))  # corpo não é usado
        if not self._authorized(parse_qs(url.query)):
            self._reply({"error": "invalid or missing token"}, status=401)
            return
        # Só o painel: navegador manda Origin e não consegue mandar cabeçalho
        # próprio sem preflight (CORS) — uma página qualquer aberta no PC não
        # dispara ações num agente sem token.
        if self.headers.get("Origin") or self.headers.get("X-CYD-Panel") != "1":
            self._reply({"ok": False, "error": "forbidden"}, status=403)
            return
        m = re.fullmatch(r"/actions/(\d+)", url.path)
        if not m:
            self._reply({"ok": False, "error": "not found"}, status=404)
            return
        status, payload = _trigger_action(int(m.group(1)))
        self._reply(payload, status=status)

    def _build_stats(self):
        with _io_lock:
            io = dict(_io_state)

        return {
            "cpu": io["cpu_pct"],
            "ram": psutil.virtual_memory().percent,
            "disk": io["disk_pct"],
            "disks": io["disks"],
            "cpu_temp": _temp_state["cpu_temp"],  # °C ou null (sem fonte)
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


# ===== Executável do Windows: autostart e modo em segundo plano =====
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
RUN_VALUE = "CYD PC Dashboard Agent"


def _run_key():
    # CYDPC_RUN_KEY: só pros testes (aponta pra uma chave que não inicia nada)
    return os.environ.get("CYDPC_RUN_KEY", RUN_KEY)


def _start_detached(extra_args):
    """Relança o .exe sem janela nenhuma (CREATE_NO_WINDOW) e volta na hora."""
    import subprocess
    env = dict(os.environ, CYDPC_DETACHED="1")
    flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
    subprocess.Popen([sys.executable] + extra_args, env=env, creationflags=flags, close_fds=True,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _stop_other_instances():
    """Encerra outras cópias deste .exe rodando (a do autostart, uma aberta à mão).

    No "onefile" do PyInstaller cada execução são DOIS processos com o mesmo
    .exe: o carregador e, como filho, o Python. Por isso pula o próprio processo
    e os ancestrais — senão mata o próprio carregador (saída com código 15)."""
    me = psutil.Process()
    skip = {me.pid} | {p.pid for p in me.parents()}
    exe = os.path.normcase(os.path.abspath(sys.executable))
    for proc in psutil.process_iter(["pid", "exe"]):
        try:
            if proc.info["pid"] not in skip and proc.info["exe"] and os.path.normcase(proc.info["exe"]) == exe:
                proc.terminate()
        except psutil.Error:
            pass


def _autostart(install):
    """--install / --uninstall: inicia sozinho no login (chave Run do usuário,
    sem admin) e já sobe/derruba o agente em segundo plano."""
    if not getattr(sys, "frozen", False) or os.name != "nt":
        sys.exit("--install/--uninstall são do executável do Windows. Rodando pelo Python, use "
                 "agent/install_autostart.bat (Windows) ou agent/install_autostart.sh (Linux/macOS).")
    import subprocess
    import winreg
    _stop_other_instances()
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, _run_key()) as key:
        if install:
            winreg.SetValueEx(key, RUN_VALUE, 0, winreg.REG_SZ, f'"{sys.executable}" --background')
        else:
            try:
                winreg.DeleteValue(key, RUN_VALUE)
            except FileNotFoundError:
                pass
    if install:
        _start_detached(["--background"])
        print("Pronto: o agente inicia sozinho a cada login e já está rodando em segundo plano.")
        print(f"Log: {os.path.join(HERE, 'agent.log')}")
        print(f"Pra desfazer: {os.path.basename(sys.executable)} --uninstall")
        # O autostart do script Python (Agendador de Tarefas) subiria um segundo agente na mesma porta
        r = subprocess.run(["schtasks", "/Query", "/TN", RUN_VALUE], capture_output=True)
        if r.returncode == 0:
            print("Aviso: existe também a tarefa agendada do agente em Python. Remova com "
                  "agent/uninstall_autostart.bat pra não rodarem dois agentes.")
    else:
        print("Autostart removido e agente em segundo plano encerrado.")


def main():
    global CFG
    # "cyd-pc-agent notify ..." = o agent/notify.py, pra quem usa o .exe sem Python
    if len(sys.argv) > 1 and sys.argv[1] == "notify":
        import notify
        sys.argv = [sys.argv[0] + " notify"] + sys.argv[2:]
        return notify.main()

    parser = argparse.ArgumentParser(description="Agente HTTP do CYD PC Dashboard")
    parser.add_argument("--config", default=os.path.join(HERE, "config.json"),
                        help="caminho do config.json (default: ao lado do script)")
    parser.add_argument("--port", type=int, help="sobrescreve a porta do config")
    parser.add_argument("--log", help="grava a saída nesse arquivo (útil com pythonw, que não tem console)")
    parser.add_argument("--background", action="store_true",
                        help="roda sem janela, com log em agent.log (é o que o autostart usa)")
    parser.add_argument("--install", action="store_true",
                        help="executável do Windows: inicia sozinho no login e já sobe em segundo plano")
    parser.add_argument("--uninstall", action="store_true", help="desfaz o --install")
    args = parser.parse_args()

    if args.install or args.uninstall:
        _autostart(args.install)
        return
    if args.background:
        if os.name == "nt" and not os.environ.get("CYDPC_DETACHED"):
            # Lançado com janela (ex.: pela chave Run no login): relança sem janela e sai
            _start_detached(sys.argv[1:] if getattr(sys, "frozen", False) else [os.path.abspath(__file__)] + sys.argv[1:])
            return
        args.log = args.log or os.path.join(HERE, "agent.log")

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
    if _action_list(log=True) and not CFG["token"]:
        print('[actions] aviso: atalhos sem "token" - qualquer aparelho da rede pode dispara-los')

    targets = [_io_monitor, _quotes_monitor, _procs_monitor, _sys_monitor, _youtube_monitor, _temp_monitor,
               _services_monitor]
    if CFG["discovery"]["enabled"]:
        targets.append(_discovery_beacon)
    for target in targets:
        threading.Thread(target=target, daemon=True).start()

    try:
        server = ThreadingHTTPServer((CFG["bind"], CFG["port"]), StatsHandler)
    except OSError as e:
        sys.exit(f"Nao foi possivel abrir {CFG['bind']}:{CFG['port']} - {e}")
    print(f"Agente rodando em http://{CFG['bind']}:{CFG['port']} "
          "(/stats /quotes /procs /sysinfo /youtube /services /actions)"
          + (" com token" if CFG["token"] else " sem token"))
    server.serve_forever()


if __name__ == "__main__":
    main()
