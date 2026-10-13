"""Aba Mídia: leitura (Windows com sessão falsa, playerctl), endpoint e comandos."""

import asyncio
import datetime
import json
import types
import urllib.error
import urllib.request

import pytest


def test_app_names(agent):
    assert agent._media_app_name("Spotify.exe") == "Spotify"
    assert agent._media_app_name("308046B0AF4A39CB") == "Firefox"
    assert agent._media_app_name("MSEdge") == "Edge"
    assert agent._media_app_name("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic") == "Media Player"
    assert agent._media_app_name("foobar2000.exe") == "foobar2000"
    assert agent._media_app_name("") == ""


def test_result_is_ascii_and_clamped(agent):
    r = agent._media_result(True, "Canção nº 1", "Artista", "Spotify", 250.04, 200)
    assert r["title"] == "Cancao no 1"
    assert r["position_s"] == 200            # não passa da duração
    r = agent._media_result(False, "x", "", "", -3, 0)
    assert r["position_s"] == 0 and r["duration_s"] is None  # duração 0 = desconhecida


# ===== Windows (API de mídia simulada) =====

def _fake_manager(session):
    async def props():
        return types.SimpleNamespace(title="Song", artist="Band")
    if session:
        session.try_get_media_properties_async = props
    return types.SimpleNamespace(get_current_session=lambda: session)


def _session(status, position, end, updated_ago):
    now = datetime.datetime.now(datetime.timezone.utc)
    tl = types.SimpleNamespace(position=datetime.timedelta(seconds=position),
                               start_time=datetime.timedelta(0), end_time=datetime.timedelta(seconds=end),
                               last_updated_time=now - datetime.timedelta(seconds=updated_ago))
    return types.SimpleNamespace(source_app_user_model_id="Spotify.exe",
                                 get_playback_info=lambda: types.SimpleNamespace(playback_status=status),
                                 get_timeline_properties=lambda: tl)


def test_windows_playing_extrapolates_position(agent):
    r = asyncio.run(agent._media_read_windows(_fake_manager(_session(4, 60, 200, 5))))
    assert (r["playing"], r["title"], r["artist"], r["app"]) == (True, "Song", "Band", "Spotify")
    assert 64.5 <= r["position_s"] <= 66 and r["duration_s"] == 200


def test_windows_paused_keeps_position(agent):
    r = asyncio.run(agent._media_read_windows(_fake_manager(_session(5, 60, 200, 5))))
    assert r["playing"] is False and r["position_s"] == 60


def test_windows_without_timeline_has_no_position(agent):
    # Firefox às vezes não informa a linha do tempo: tudo zerado
    r = asyncio.run(agent._media_read_windows(_fake_manager(_session(4, 0, 0, 30))))
    assert r["playing"] and r["position_s"] is None and r["duration_s"] is None


def test_windows_nothing_playing(agent):
    r = asyncio.run(agent._media_read_windows(_fake_manager(None)))
    assert r["available"] and r["title"] == "" and not r["playing"]


# ===== Linux (playerctl simulado) =====

def test_playerctl(agent, monkeypatch):
    out = "\x1f".join(["Playing", "Song", "Band", "spotify", "61500000", "200000000"]) + "\n"
    monkeypatch.setattr(agent.subprocess, "run", lambda *a, **kw: types.SimpleNamespace(returncode=0, stdout=out))
    r = agent._media_read_playerctl()
    assert (r["playing"], r["title"], r["app"], r["position_s"], r["duration_s"]) == (True, "Song", "Spotify", 61.5, 200)


def test_playerctl_no_players(agent, monkeypatch):
    monkeypatch.setattr(agent.subprocess, "run", lambda *a, **kw: types.SimpleNamespace(returncode=1, stdout=""))
    assert agent._media_read_playerctl()["title"] == ""


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


@pytest.fixture
def keys(agent, monkeypatch):
    pressed = []
    monkeypatch.setattr(agent, "_run_builtin", lambda name: (pressed.append(name), (True, ""))[1])
    return pressed


def test_media_endpoint(agent, port):
    agent._media_state.update(agent._media_result(True, "Song", "Band", "Spotify", 10, 200))
    status, data = _request(port, "/media")
    assert status == 200 and data["title"] == "Song" and data["playing"] is True


def test_media_command(agent, port, keys):
    agent._media_wake.clear()
    assert _request(port, "/media/next", "POST", {"X-CYD-Panel": "1"}) == (200, {"ok": True})
    assert agent._media_wake.is_set()       # releitura na hora
    assert _request(port, "/media/format_c", "POST", {"X-CYD-Panel": "1"})[0] == 404
    assert keys == ["next"]


def test_media_command_has_post_protections(agent, port, keys):
    assert _request(port, "/media/next", "POST")[0] == 403
    assert _request(port, "/media/next", "POST", {"X-CYD-Panel": "1", "Origin": "http://evil.example"})[0] == 403
    agent.CFG["token"] = "s3cret"
    assert _request(port, "/media/next", "POST", {"X-CYD-Panel": "1"})[0] == 401
    assert keys == []
