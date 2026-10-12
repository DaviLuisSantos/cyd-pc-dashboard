"""Cotações: histórico de 24h, arquivo do histórico, ações (Yahoo) e /quotes."""

import json

import pytest

NOW = 1_800_000_000.0  # "agora" fixo pros testes de janela de tempo
HOUR = 3600.0


@pytest.fixture
def frozen(agent, monkeypatch):
    monkeypatch.setattr(agent.time, "time", lambda: NOW)
    agent.CFG["quotes"]["history_hours"] = 24
    agent.CFG["quotes"]["history_points"] = 4  # janelas de 6h, fácil de raciocinar
    return agent


# ===== histórico =====

def test_hist_add_ignores_out_of_order_points(agent):
    agent._hist_add("BTC", 100.0, 1.0)
    agent._hist_add("BTC", 90.0, 2.0)  # mais antigo que o último: ignorado
    agent._hist_add("BTC", 110.0, 3.0)
    assert agent._quotes_hist["BTC"] == [(100.0, 1.0), (110.0, 3.0)]


def test_hist_trim_drops_older_than_window(frozen):
    a = frozen
    a._quotes_hist["BTC"] = [(NOW - 30 * HOUR, 1.0), (NOW - 2 * HOUR, 2.0)]
    a._hist_trim()
    assert a._quotes_hist["BTC"] == [(NOW - 2 * HOUR, 2.0)]


def test_hist_series_buckets_by_time_not_index(frozen):
    a = frozen
    # 1 ponto na 1ª janela, 10 pontos colados na última: espaçamento por TEMPO
    a._quotes_hist["BTC"] = [(NOW - 23 * HOUR, 100.0)] + [(NOW - 0.5 * HOUR + i, 200.0 + i) for i in range(10)]
    # janelas: [..-18h] tem o 100; (-18h..-12h] e (-12h..-6h] vazias repetem; última = último preço
    assert a._hist_series("BTC") == [100.0, 100.0, 100.0, 209.0]


def test_hist_series_partial_history_has_no_leading_points(frozen):
    a = frozen
    a._quotes_hist["EUR"] = [(NOW - 7 * HOUR, 1.08), (NOW - 1 * HOUR, 1.09)]
    # Antes do primeiro dado não há ponto: o painel alinha à direita
    assert a._hist_series("EUR") == [1.08, 1.09]


def test_hist_series_needs_two_points(frozen):
    frozen._quotes_hist["EUR"] = [(NOW - HOUR, 1.08)]
    assert frozen._hist_series("EUR") == []
    assert frozen._hist_series("NAOEXISTE") == []


def test_hist_save_and_load_roundtrip(frozen):
    a = frozen
    a._quotes_hist["BTC"] = [(NOW - 2 * HOUR, 1.5), (NOW - HOUR, 2.5)]
    a._hist_save()
    a._quotes_hist.clear()
    a._hist_load()
    assert a._quotes_hist["BTC"] == [(NOW - 2 * HOUR, 1.5), (NOW - HOUR, 2.5)]


def test_hist_load_discards_other_base_currency(frozen):
    a = frozen
    with open(a._HIST_FILE, "w", encoding="utf-8") as f:
        json.dump({"base": "EUR", "history": {"BTC": [[NOW - HOUR, 1.0]]}}, f)
    a.CFG["quotes"]["base"] = "USD"
    a._hist_load()
    assert a._quotes_hist == {}


def test_hist_load_tolerates_missing_or_broken_file(frozen):
    frozen._hist_load()  # não existe
    with open(frozen._HIST_FILE, "w") as f:
        f.write("{quebrado")
    frozen._hist_load()
    assert frozen._quotes_hist == {}


# ===== ações (Yahoo) =====

def yahoo(currency, price, prev, closes):
    return {"chart": {"result": [{
        "meta": {"currency": currency, "regularMarketPrice": price, "chartPreviousClose": prev},
        "indicators": {"quote": [{"close": closes}]},
    }], "error": None}}


@pytest.fixture
def fake_yahoo(agent, monkeypatch):
    responses = {
        "PETR4.SA": yahoo("BRL", 56.0, 55.0, [55.1, None, 55.6, 56.0]),
        "AAPL": yahoo("USD", 340.0, 320.0, [321.0, 330.0, 340.0]),
        "%5EBVSP": yahoo("BRL", 209000.0, 200000.0, [201000.0, 209000.0]),
        "TSLA": yahoo("USD", 250.0, 250.0, [250.0]),
    }

    def fake_fetch(url):
        ticker = url.split("/chart/")[1].split("?")[0]
        if ticker not in responses:
            raise OSError("HTTP Error 404: Not Found")
        return responses[ticker]

    monkeypatch.setattr(agent, "_fetch_json", fake_fetch)
    return agent


def test_stocks_prefix_rules(fake_yahoo):
    out = fake_yahoo._fetch_stocks([
        ("PETR4", "PETR4.SA", None),   # moeda = base -> prefixo global (None)
        ("AAPL", "AAPL", None),        # outra moeda -> prefixo próprio
        ("IBOV", "^BVSP", None),       # índice -> sem prefixo
        ("TSLA", "TSLA", "US$ "),      # "prefix" do config sobrescreve
    ], "BRL")
    assert {s: v[3] for s, v in out.items()} == {"PETR4": None, "AAPL": "$", "IBOV": "", "TSLA": "US$ "}


def test_stocks_change_and_session_history(fake_yahoo):
    price, pct, hist, _ = fake_yahoo._fetch_stocks([("AAPL", "AAPL", None)], "USD")["AAPL"]
    assert price == 340.0
    assert pct == pytest.approx(6.25)          # (340 - 320) / 320
    assert hist == [321.0, 330.0, 340.0]       # o pregão, sem os None do Yahoo


def test_stocks_bad_ticker_does_not_break_others(fake_yahoo):
    out = fake_yahoo._fetch_stocks([("XXX", "NAOEXISTE", None), ("AAPL", "AAPL", None)], "USD")
    assert list(out) == ["AAPL"]


def test_stocks_yahoo_error_payload(agent, monkeypatch):
    monkeypatch.setattr(agent, "_fetch_json", lambda url: {"chart": {"result": None, "error": {
        "code": "Not Found", "description": "No data found, symbol may be delisted"}}})
    assert agent._fetch_stocks([("X", "X", None)], "USD") == {}


@pytest.mark.parametrize("n_values,n,expected_first_last", [(3, 32, (0, 2)), (100, 32, (0, 99))])
def test_downsample(agent, n_values, n, expected_first_last):
    values = list(range(n_values))
    out = agent._downsample(values, n)
    assert len(out) == min(n_values, n)
    assert (out[0], out[-1]) == expected_first_last  # primeiro e último sempre ficam


# ===== /quotes =====

def test_build_quotes_items(agent):
    agent.CFG["quotes"]["assets"] = [
        {"symbol": "usd", "type": "fiat"},
        {"symbol": "AAPL", "type": "stock", "id": "AAPL"},
        {"symbol": "BTC", "type": "crypto", "id": "bitcoin"},
    ]
    agent._quotes_state["prices"].update({
        "USD": {"price": 4.98, "change_pct": -0.1},
        "AAPL": {"price": 340.0, "change_pct": 6.25, "prefix": "$"},
    })
    agent._stock_hist["AAPL"] = [321.0, 340.0]
    q = agent._build_quotes()
    items = {it["symbol"]: it for it in q["items"]}
    assert list(items) == ["USD", "AAPL", "BTC"]           # ordem do config, símbolo em maiúsculas
    assert "prefix" not in items["USD"]                    # usa o global
    assert items["AAPL"]["prefix"] == "$"
    assert items["AAPL"]["history"] == [321.0, 340.0]      # ação: pregão do Yahoo
    assert items["BTC"]["price"] is None                   # ainda não buscado: painel pula
