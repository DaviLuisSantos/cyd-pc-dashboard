"""Aba Atalhos: validação do config, execução e as proteções do POST."""

import json
import os
import sys
import time
import urllib.error
import urllib.request

import pytest


# ===== config =====

def test_defaults_by_kind(agent):
    agent.CFG["actions"] = [
        {"name": "Lock", "do": "lock"},
        {"name": "Docs", "open": "https://example.com"},
        {"name": "Notes", "open": "C:\\notes.txt"},
        {"name": "Backup", "run": "backup.sh"},
    ]
    got = [(a["icon"], a["color"]) for a in agent._action_list()]
    assert got == [("lock", "cyan"), ("link", "green"), ("app", "green"), ("terminal", "orange")]


def test_icon_and_color_override(agent):
    agent.CFG["actions"] = [{"name": "Off", "run": "x", "icon": "power", "color": "red"},
                            {"name": "Bad", "run": "x", "icon": "nope", "color": "beige"}]
    got = [(a["icon"], a["color"]) for a in agent._action_list()]
    assert got == [("power", "red"), ("terminal", "orange")]  # inválido cai no padrão


def test_invalid_entries_are_skipped(agent):
    agent.CFG["actions"] = [
        {"do": "lock"},                                  # sem nome
        {"name": "Dois", "do": "lock", "run": "x"},      # mais de um tipo
        {"name": "Nada"},                                # nenhum
        {"name": "Format", "do": "format_c"},            # "do" desconhecido
        "texto solto",
        {"name": "OK", "do": "mute"},
    ]
    assert [a["name"] for a in agent._action_list()] == ["OK"]


def test_name_ascii_short_and_max_8(agent):
    agent.CFG["actions"] = [{"name": "Ação muito comprida demais", "do": "mute"}] + \
                           [{"name": f"A{i}", "do": "mute"} for i in range(10)]
    actions = agent._action_list()
    assert actions[0]["name"] == "Acao muito com"
    assert len(actions) == 8


# ===== execução =====

def test_run_starts_command_without_waiting(agent, tmp_path):
    flag = tmp_path / "ran.txt"
    code = f"import time; time.sleep(0.3); open({str(flag)!r}, 'w').write('x')"
    t0 = time.monotonic()
    ok, err = agent._run_action({"name": "t", "kind": "run", "target": [sys.executable, "-c", code]})
    assert ok and err == ""
    assert time.monotonic() - t0 < 0.3          # não esperou o comando terminar
    for _ in range(100):
        if flag.exists():
            break
        time.sleep(0.05)
    assert flag.read_text() == "x"


def test_run_missing_program(agent):
    ok, err = agent._run_action({"name": "t", "kind": "run", "target": ["programa-que-nao-existe-123"]})
    assert (ok, err) == (False, "missing")


@pytest.mark.skipif(os.name == "nt", reason="caminho POSIX")
def test_builtin_missing_tool(agent, monkeypatch):
    def missing(*a, **kw):
        raise FileNotFoundError
    monkeypatch.setattr(agent.subprocess, "run", missing)
    monkeypatch.setattr(agent.os, "uname", lambda: type("U", (), {"sysname": "Linux"})())
    assert agent._run_builtin("play_pause") == (False, "missing")


@pytest.mark.skipif(os.name == "nt", reason="caminho POSIX")
def test_builtin_unsupported_on_macos(agent, monkeypatch):
    monkeypatch.setattr(agent.os, "uname", lambda: type("U", (), {"sysname": "Darwin"})())
    assert agent._run_builtin("next") == (False, "n/a")


# ===== disparo =====

@pytest.fixture
def ran(agent, monkeypatch):
    """Troca a execução de verdade por uma lista do que seria executado."""
    calls = []

    def fake(action):
        calls.append(action["name"])
        return (False, "failed") if action["name"] == "Falha" else (True, "")
    monkeypatch.setattr(agent, "_run_action", fake)
    agent.CFG["actions"] = [{"name": "Lock", "do": "lock"}, {"name": "Falha", "run": "x"}]
    return calls


def test_trigger(agent, ran):
    assert agent._trigger_action(0) == (200, {"ok": True})
    assert agent._trigger_action(1) == (500, {"ok": False, "error": "failed"})
    assert agent._trigger_action(5)[0] == 404
    assert ran == ["Lock", "Falha"]


def test_repeated_tap_is_ignored(agent, ran, monkeypatch):
    clock = {"t": 100.0}
    monkeypatch.setattr(agent.time, "monotonic", lambda: clock["t"])
    assert agent._trigger_action(0)[0] == 200
    clock["t"] += 0.5
    assert agent._trigger_action(0)[0] == 429      # toque duplo
    assert agent._trigger_action(1)[0] == 500      # outro botão não espera
    clock["t"] += 0.6
    assert agent._trigger_action(0)[0] == 200
    assert ran == ["Lock", "Falha", "Lock"]


# ===== HTTP =====

def _request(port, path, method="GET", headers=None):
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method,
                                 data=b"" if method == "POST" else None, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


@pytest.fixture
def port(agent, http_server):
    return http_server(agent.StatsHandler).server_address[1]


def test_list_does_not_expose_commands(agent, port):
    agent.CFG["actions"] = [{"name": "Backup", "run": "secret-script --password=hunter2"}]
    status, data = _request(port, "/actions")
    assert status == 200
    assert data == {"configured": True, "actions": [{"name": "Backup", "icon": "terminal", "color": "orange"}]}


def test_post_needs_panel_header(agent, ran, port):
    assert _request(port, "/actions/0", "POST")[0] == 403
    assert ran == []


def test_post_from_browser_is_refused(agent, ran, port):
    headers = {"X-CYD-Panel": "1", "Origin": "http://evil.example"}
    assert _request(port, "/actions/0", "POST", headers)[0] == 403
    assert ran == []


def test_post_runs_action(agent, ran, port):
    assert _request(port, "/actions/0", "POST", {"X-CYD-Panel": "1"}) == (200, {"ok": True})
    assert _request(port, "/actions/9", "POST", {"X-CYD-Panel": "1"})[0] == 404
    assert _request(port, "/outra", "POST", {"X-CYD-Panel": "1"})[0] == 404
    assert ran == ["Lock"]


def test_post_requires_token(agent, ran, port):
    agent.CFG["token"] = "s3cret"
    assert _request(port, "/actions/0", "POST", {"X-CYD-Panel": "1"})[0] == 401
    assert _request(port, "/actions/0", "POST", {"X-CYD-Panel": "1", "X-Agent-Token": "s3cret"})[0] == 200
    assert ran == ["Lock"]
