"""agent/notify.py contra um "painel" de mentira (servidor HTTP local)."""

import base64
import json
from http.server import BaseHTTPRequestHandler

import pytest


@pytest.fixture
def panel(http_server):
    """Painel falso: guarda os requests e responde com o status configurado."""
    state = {"requests": [], "status": 200}

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
            state["requests"].append({"path": self.path, "auth": self.headers.get("Authorization"),
                                      "json": json.loads(body)})
            self.send_response(state["status"])
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"ok":true,"pending":1}')

        def log_message(self, *a):
            pass

    srv = http_server(Handler, threaded=False)
    state["host"] = f"127.0.0.1:{srv.server_address[1]}"
    return state


def test_send_posts_json(notify, panel):
    resp = notify.send(panel["host"], "", {"text": "Build ok", "title": "CI", "level": "ok"})
    assert resp["ok"] is True
    req = panel["requests"][0]
    assert req["path"] == "/api/notify"
    assert req["json"] == {"text": "Build ok", "title": "CI", "level": "ok"}
    assert req["auth"] is None


def test_send_with_password_uses_basic_auth(notify, panel):
    notify.send(panel["host"], "pw", {"text": "x"})
    expected = "Basic " + base64.b64encode(b"admin:pw").decode()
    assert panel["requests"][0]["auth"] == expected


@pytest.mark.parametrize("status", [400, 401])
def test_client_errors_are_not_retried(notify, panel, status):
    panel["status"] = status
    with pytest.raises(notify.urllib.error.HTTPError):
        notify.send(panel["host"], "", {"text": "x"})
    assert len(panel["requests"]) == 1  # erro "de verdade": uma tentativa só


def test_server_errors_are_retried(notify, panel, monkeypatch):
    monkeypatch.setattr(notify.time, "sleep", lambda s: None)
    panel["status"] = 503
    with pytest.raises(notify.urllib.error.HTTPError):
        notify.send(panel["host"], "", {"text": "x"}, retries=3)
    assert len(panel["requests"]) == 3


def test_connection_refused_is_retried_then_raised(notify, monkeypatch):
    # Simulado: no Windows uma conexão recusada de verdade leva ~2s por tentativa
    monkeypatch.setattr(notify.time, "sleep", lambda s: None)
    calls = []

    def refused(req, timeout):
        calls.append(1)
        raise ConnectionRefusedError("painel desligado")

    monkeypatch.setattr(notify.urllib.request, "urlopen", refused)
    with pytest.raises(ConnectionRefusedError):
        notify.send("cyd-dashboard.local", "", {"text": "x"}, retries=2)
    assert len(calls) == 2


def test_panel_config_from_file(notify, tmp_path):
    p = tmp_path / "config.json"
    p.write_text(json.dumps({"panel": {"host": "192.168.1.8"}}), encoding="utf-8")
    assert notify.load_panel_config(str(p)) == {"host": "192.168.1.8", "password": ""}
    assert notify.load_panel_config(str(tmp_path / "nao-existe.json"))["host"] == "cyd-dashboard.local"
