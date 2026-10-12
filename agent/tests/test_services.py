"""Aba Serviços: verificação tcp/http, limite de falhas e avisos ao painel."""

import json
import socket
from http.server import BaseHTTPRequestHandler

import pytest


@pytest.fixture
def tcp_port():
    """Porta TCP local escutando de verdade."""
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen()
    yield srv.getsockname()[1]
    srv.close()


@pytest.fixture
def closed_port():
    """Porta onde ninguém escuta (aberta e fechada na hora)."""
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


@pytest.fixture
def http_url(http_server):
    """Servidor HTTP local: /ok responde 200, /erro 503, /redir redireciona pro /ok."""
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/redir":
                self.send_response(302)
                self.send_header("Location", "/ok")
                self.end_headers()
                return
            self.send_response(200 if self.path == "/ok" else 503)
            self.end_headers()
            self.wfile.write(b"x")

        def log_message(self, *a):
            pass

    srv = http_server(Handler)
    return f"http://127.0.0.1:{srv.server_address[1]}"


# ===== uma verificação =====

def test_tcp_up(agent, tcp_port):
    ok, ms, detail = agent._check_service(f"tcp://127.0.0.1:{tcp_port}", 2)
    assert ok and ms is not None and detail == ""


def test_tcp_down(agent, closed_port):
    # Timeout curto: no Windows uma conexão recusada só volta depois de ~2s
    ok, ms, detail = agent._check_service(f"tcp://127.0.0.1:{closed_port}", 0.3)
    assert not ok and ms is None and detail in ("refused", "timeout")


def test_tcp_without_port(agent):
    assert agent._check_service("tcp://nas", 2) == (False, None, "bad check")


def test_http_ok_redirect_and_error(agent, http_url):
    assert agent._check_service(http_url + "/ok", 2)[0] is True
    assert agent._check_service(http_url + "/redir", 2)[0] is True   # segue o redirecionamento
    assert agent._check_service(http_url + "/erro", 2) == (False, None, "HTTP 503")


def test_unknown_scheme(agent):
    assert agent._check_service("ftp://x", 2) == (False, None, "bad check")


@pytest.mark.parametrize("exc, reason", [
    (ConnectionRefusedError(10061, "No connection could be made"), "refused"),
    (socket.gaierror(11001, "getaddrinfo failed"), "no DNS"),
    (TimeoutError(), "timeout"),
    (OSError(10065, "A socket operation was attempted to an unreachable host"), "unreachable"),
])
def test_short_reason(agent, exc, reason):
    import urllib.error
    assert agent._short_reason(exc) == reason
    assert agent._short_reason(urllib.error.URLError(exc)) == reason  # como o urllib embrulha


# ===== rodadas e estado =====

@pytest.fixture
def scripted(agent, monkeypatch):
    """Resultados de verificação controlados pelo teste: results[nome] = True/False."""
    results = {}
    monkeypatch.setattr(agent, "_check_service",
                        lambda check, timeout: (True, 12, "") if results[check] else (False, None, "timeout"))
    agent.CFG["services"] = [{"name": "NAS", "check": "NAS"}, {"name": "Site", "check": "Site"}]
    agent.CFG["services_options"]["fail_threshold"] = 2
    return results


def run(agent, prev_list, fails):
    prev = {s["name"]: s for s in prev_list}
    return agent._services_round(prev, fails)


def test_first_round_has_no_events(agent, scripted):
    scripted.update(NAS=True, Site=False)
    svcs, events = run(agent, [], {})
    assert [(s["name"], s["ok"]) for s in svcs] == [("NAS", True), ("Site", None)]  # 1 falha: ainda não sabe
    assert events == []


def test_goes_down_only_after_threshold_and_back_on_first_success(agent, scripted):
    fails = {}
    scripted.update(NAS=True, Site=True)
    svcs, _ = run(agent, [], fails)
    scripted["NAS"] = False
    svcs, events = run(agent, svcs, fails)          # 1ª falha: continua "no ar"
    assert svcs[0]["ok"] is True and events == []
    svcs, events = run(agent, svcs, fails)          # 2ª falha: caiu
    assert svcs[0]["ok"] is False and events == [("NAS", "down", "timeout")]
    scripted["NAS"] = True
    svcs, events = run(agent, svcs, fails)          # volta na primeira resposta
    assert svcs[0]["ok"] is True and events == [("NAS", "up", "")]


def test_since_marks_state_changes(agent, scripted, monkeypatch):
    clock = {"t": 1000}
    monkeypatch.setattr(agent.time, "time", lambda: clock["t"])
    fails = {}
    scripted.update(NAS=True, Site=True)
    svcs, _ = run(agent, [], fails)
    clock["t"] = 1060
    svcs, _ = run(agent, svcs, fails)               # sem mudança: "since" fica
    assert svcs[0]["since"] == 1000
    scripted["NAS"] = False
    clock["t"] = 1120
    svcs, _ = run(agent, svcs, fails)
    clock["t"] = 1180
    svcs, _ = run(agent, svcs, fails)               # caiu agora
    assert svcs[0]["since"] == 1180


def test_invalid_entries_are_skipped(agent, scripted):
    agent.CFG["services"] = [{"name": "Sem check"}, {"check": "sem nome"}, {"name": "NAS", "check": "NAS"}]
    scripted["NAS"] = True
    svcs, _ = run(agent, [], {})
    assert [s["name"] for s in svcs] == ["NAS"]


def test_name_is_ascii_and_short(agent, scripted):
    agent.CFG["services"] = [{"name": "Servidor de mídia da sala de estar", "check": "x"}]
    scripted["x"] = True
    assert run(agent, [], {})[0][0]["name"] == "Servidor de midia da"


# ===== aviso ao painel =====

def test_notify_panel_posts_error_notification(agent, http_server):
    got = []

    class Panel(BaseHTTPRequestHandler):
        def do_POST(self):
            got.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
            self.send_response(200)
            self.end_headers()

        def log_message(self, *a):
            pass

    srv = http_server(Panel)
    agent.CFG["panel"] = {"host": f"127.0.0.1:{srv.server_address[1]}", "password": ""}
    agent._notify_panel("NAS is down", "timeout", "error")
    assert got == [{"title": "NAS is down", "text": "timeout", "level": "error", "duration": 0}]


def test_notify_panel_offline_does_not_raise(agent, monkeypatch):
    def refused(req, timeout):
        raise ConnectionRefusedError("painel desligado")
    monkeypatch.setattr(agent.urllib.request, "urlopen", refused)
    agent._notify_panel("x", "y", "ok")  # só loga, não derruba a thread


# ===== endpoint =====

def test_services_endpoint(agent, http_server):
    import urllib.request
    agent.CFG["services"] = [{"name": "NAS", "check": "tcp://x:1"}]
    agent._services_state.update({"services": [{"name": "NAS", "ok": False, "ms": None,
                                                "detail": "timeout", "since": 5}], "updated_at": 9})
    srv = http_server(agent.StatsHandler)
    with urllib.request.urlopen(f"http://127.0.0.1:{srv.server_address[1]}/services", timeout=5) as r:
        data = json.loads(r.read())
    assert data["configured"] is True
    assert data["services"][0]["ok"] is False
