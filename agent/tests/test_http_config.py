"""Servidor HTTP de verdade (token, rotas), config e anúncio na rede."""

import json
import urllib.error
import urllib.request
from collections import namedtuple

import pytest


@pytest.fixture
def server(agent, http_server):
    """Sobe o StatsHandler numa porta livre, sem as threads de coleta."""
    srv = http_server(agent.StatsHandler)
    return f"http://127.0.0.1:{srv.server_address[1]}"


def get(url, headers=None):
    req = urllib.request.Request(url, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        body = e.read()
        return e.code, json.loads(body) if body else None


# ===== rotas =====

def test_routes_and_shapes(agent, server):
    agent._procs_state["procs"] = [{"name": "chrome", "cpu": 9.3, "mem_mb": 3184}]
    agent._temp_state["cpu_temp"] = 61.4

    status, stats = get(server + "/stats")
    assert status == 200
    assert {"cpu", "ram", "disk", "disks", "cpu_temp", "net_down_mbps", "net_up_mbps"} <= set(stats)
    assert stats["cpu_temp"] == 61.4

    assert get(server + "/procs") == (200, {"procs": [{"name": "chrome", "cpu": 9.3, "mem_mb": 3184}]})
    status, info = get(server + "/sysinfo")
    assert status == 200 and {"ping_ms", "public_ip", "uptime_s"} <= set(info)
    status, quotes = get(server + "/quotes")
    assert status == 200 and {"base", "prefix", "items"} <= set(quotes)
    assert get(server + "/youtube")[1]["configured"] is False


def test_unknown_route_is_404(server):
    assert get(server + "/nada")[0] == 404


def test_query_string_does_not_break_routing(server):
    assert get(server + "/procs?x=1")[0] == 200


# ===== token =====

@pytest.fixture
def with_token(agent):
    agent.CFG["token"] = "s3cret"


def test_no_token_configured_is_open(server):
    assert get(server + "/stats")[0] == 200


def test_token_missing_or_wrong_is_401(with_token, server):
    assert get(server + "/stats") == (401, {"error": "invalid or missing token"})
    assert get(server + "/stats", {"X-Agent-Token": "nope"})[0] == 401
    assert get(server + "/stats?token=nope")[0] == 401


def test_token_via_header_or_query(with_token, server):
    assert get(server + "/stats", {"X-Agent-Token": "s3cret"})[0] == 200
    assert get(server + "/procs?token=s3cret")[0] == 200


def test_token_checked_before_404(with_token, server):
    # Sem token não dá nem pra descobrir quais rotas existem
    assert get(server + "/nada")[0] == 401


# ===== config =====

def test_load_config_defaults_without_file(agent, tmp_path):
    cfg = agent.load_config(str(tmp_path / "nao-existe.json"))
    assert cfg["port"] == 8377
    assert cfg["disk_path"]  # preenchido com a raiz do sistema


def test_load_config_deep_merge(agent, tmp_path):
    p = tmp_path / "config.json"
    p.write_text(json.dumps({"quotes": {"base": "BRL"}, "intervals": {"quotes": 30}}), encoding="utf-8")
    cfg = agent.load_config(str(p))
    assert cfg["quotes"]["base"] == "BRL"
    assert cfg["quotes"]["prefix"] == "$"          # o resto da seção vem do default
    assert cfg["intervals"]["quotes"] == 30
    assert cfg["intervals"]["io"] == 1.0
    assert agent.DEFAULT_CONFIG["quotes"]["base"] == "USD"  # o default não foi alterado


def test_load_config_youtube_key_from_env(agent, tmp_path, monkeypatch):
    monkeypatch.setenv("YOUTUBE_API_KEY", "  da-env  ")
    cfg = agent.load_config(str(tmp_path / "nao-existe.json"))
    assert cfg["youtube"]["api_key"] == "da-env"


# ===== anúncio na rede =====

Addr = namedtuple("Addr", "family address netmask broadcast ptp")
Stat = namedtuple("Stat", "isup duplex speed mtu")


def test_broadcast_addrs_per_interface(agent, monkeypatch):
    import socket
    addrs = {
        "Ethernet": [Addr(socket.AF_INET, "192.168.1.20", "255.255.255.0", None, None)],
        "vEthernet": [Addr(socket.AF_INET, "172.20.0.1", "255.255.240.0", None, None)],
        "Loopback": [Addr(socket.AF_INET, "127.0.0.1", "255.0.0.0", None, None)],
        "APIPA": [Addr(socket.AF_INET, "169.254.3.4", "255.255.0.0", None, None)],
        "Down": [Addr(socket.AF_INET, "10.0.0.5", "255.0.0.0", None, None)],
    }
    stats = {k: Stat(k != "Down", 0, 0, 1500) for k in addrs}
    monkeypatch.setattr(agent.psutil, "net_if_addrs", lambda: addrs)
    monkeypatch.setattr(agent.psutil, "net_if_stats", lambda: stats)
    assert agent._broadcast_addrs() == {"192.168.1.255", "172.20.15.255"}


def test_broadcast_addrs_fallback(agent, monkeypatch):
    monkeypatch.setattr(agent.psutil, "net_if_addrs", lambda: {})
    monkeypatch.setattr(agent.psutil, "net_if_stats", lambda: {})
    assert agent._broadcast_addrs() == {"255.255.255.255"}
