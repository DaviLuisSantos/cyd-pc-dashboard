"""
Fixtures dos testes do agente.

Rodar (da raiz do repositório):
    pip install -r agent/requirements.txt pytest
    python -m pytest agent/tests -q

Nada aqui fala com a internet: as APIs externas (Yahoo, CoinGecko,
LibreHardwareMonitor...) são simuladas, então os testes não dependem delas.
"""

import copy
import os
import socketserver
import sys
import threading
from http.server import HTTPServer, ThreadingHTTPServer

import pytest

AGENT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, AGENT_DIR)

import pc_stats_agent as agent_module  # noqa: E402
import notify as notify_module  # noqa: E402

_DEFAULT_CONFIG = copy.deepcopy(agent_module.DEFAULT_CONFIG)


@pytest.fixture
def agent(tmp_path, monkeypatch):
    """O módulo do agente com CFG e todo o estado em memória zerados.

    CFG vira uma cópia do DEFAULT_CONFIG (o agente nunca carrega o config.json
    de verdade aqui) e o arquivo de histórico das cotações aponta pra tmp_path.
    """
    a = agent_module
    cfg = copy.deepcopy(_DEFAULT_CONFIG)
    cfg["disk_path"] = os.path.abspath(os.sep)
    monkeypatch.setattr(a, "CFG", cfg)
    monkeypatch.setattr(a, "_HIST_FILE", str(tmp_path / "quotes_history.json"))
    # Estado global: zera no lugar (as funções guardam referência aos dicts)
    a._quotes_hist.clear()
    a._stock_hist.clear()
    a._quotes_state.clear()
    a._quotes_state.update({"prices": {}, "updated_at": 0})
    a._temp_state.update({"cpu_temp": None, "source": None})
    a._procs_state.update({"procs": []})
    a._yt_state.update({"configured": False, "channels": [], "updated_at": 0})
    return a


@pytest.fixture
def notify():
    return notify_module


def _make_server(handler, threaded=True):
    """Servidor HTTP de teste numa porta livre, rodando numa thread.

    Pula o socket.getfqdn() do HTTPServer.server_bind: nos runners de macOS ele
    leva segundos por servidor (a suíte foi de ~1s pra 35s). poll_interval curto
    porque o shutdown() espera um ciclo inteiro (0,5s por padrão).
    """
    base = ThreadingHTTPServer if threaded else HTTPServer

    class FastServer(base):
        def server_bind(self):
            socketserver.TCPServer.server_bind(self)
            self.server_name = "localhost"
            self.server_port = self.server_address[1]

    srv = FastServer(("127.0.0.1", 0), handler)
    threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True).start()
    return srv


@pytest.fixture
def http_server():
    """Fábrica de servidores de teste; desliga todos no fim do teste."""
    started = []

    def start(handler, threaded=True):
        srv = _make_server(handler, threaded)
        started.append(srv)
        return srv

    yield start
    for srv in started:
        srv.shutdown()
        srv.server_close()
