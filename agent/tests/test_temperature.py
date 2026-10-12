"""Temperatura da CPU: escolha do sensor, LibreHardwareMonitor e psutil."""

from collections import namedtuple

import pytest


def sensor(text, value, sid=None, typ=None):
    n = {"Text": text, "Value": value, "Children": []}
    if sid:
        n["SensorId"] = sid
    if typ:
        n["Type"] = typ
    return n


def hardware(text, icon, sensors):
    return {"Text": text, "ImageURL": f"images_icon/{icon}", "Children": [
        {"Text": "Temperatures", "ImageURL": "images_icon/temperature.png", "Children": sensors}]}


def tree(*hw):
    return {"Text": "Sensor", "Children": [{"Text": "PC", "ImageURL": "images_icon/computer.png",
                                            "Children": list(hw)}]}


# ===== _pick_sensor =====

def test_pick_prefers_whole_package_sensor(agent):
    cand = [("CPU Core #1", 55.0), ("CPU Package", 59.5), ("CPU Core #2", 63.0)]
    assert agent._pick_sensor(cand, None) == 59.5


def test_pick_falls_back_to_hottest_core(agent):
    # Sem sensor de pacote: o núcleo mais quente, não o primeiro da lista
    assert agent._pick_sensor([("CPU Core #1", 55.0), ("CPU Core #2", 63.0)], None) == 63.0


def test_pick_forced_sensor_by_name(agent):
    cand = [("CPU Core #1", 55.0), ("CPU Package", 59.5), ("CPU Core #2", 63.0)]
    assert agent._pick_sensor(cand, "core #1") == 55.0


def test_pick_empty(agent):
    assert agent._pick_sensor([], None) is None


# ===== LibreHardwareMonitor =====

def test_lhm_new_format_amd(agent):
    t = tree(hardware("AMD Ryzen 7 5800X", "cpu.png", [
        sensor("Core (Tctl/Tdie)", "61.4 °C", "/amdcpu/0/temperature/2", "Temperature"),
        sensor("CCD1 (Tdie)", "58.0 °C", "/amdcpu/0/temperature/3", "Temperature"),
    ]))
    temps = agent._lhm_cpu_temps(t)
    assert temps == [("Core (Tctl/Tdie)", 61.4), ("CCD1 (Tdie)", 58.0)]
    assert agent._pick_sensor(temps, None) == 61.4


def test_lhm_old_format_with_decimal_comma(agent):
    # LHM antigo: sem SensorId/Type; Windows em pt-BR usa vírgula decimal
    t = tree(hardware("Intel Core i7-9700K", "cpu.png", [
        sensor("CPU Core #1", "55,0 °C"), sensor("CPU Package", "59,5 °C")]))
    assert agent._pick_sensor(agent._lhm_cpu_temps(t), None) == 59.5


def test_lhm_ignores_gpu_and_disk(agent):
    t = tree(
        hardware("NVIDIA GeForce RTX 3070", "nvidia.png",
                 [sensor("GPU Core", "72.0 °C", "/gpu-nvidia/0/temperature/0", "Temperature")]),
        hardware("Generic Hard Disk", "hdd.png", [sensor("Temperature", "41,0 °C")]),
        hardware("Intel Core i5", "cpu.png", [sensor("CPU Package", "50.0 °C")]),
    )
    assert agent._lhm_cpu_temps(t) == [("CPU Package", 50.0)]


def test_lhm_no_cpu_sensors(agent):
    t = tree(hardware("NVIDIA", "nvidia.png", [sensor("GPU Core", "72.0 °C")]))
    assert agent._lhm_cpu_temps(t) == []


def test_lhm_fetch_uses_url(agent, monkeypatch):
    t = tree(hardware("CPU", "cpu.png", [sensor("CPU Package", "47.5 °C")]))

    class Resp:
        def __init__(self, data):
            import json
            self.body = json.dumps(data).encode()

        def read(self, *a):
            return self.body

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

    seen = {}

    def fake_urlopen(url, timeout):
        seen["url"] = url
        return Resp(t)

    monkeypatch.setattr(agent.urllib.request, "urlopen", fake_urlopen)
    assert agent._temp_lhm("http://127.0.0.1:8085/data.json", None) == 47.5
    assert seen["url"] == "http://127.0.0.1:8085/data.json"


# ===== psutil (Linux) =====

Temp = namedtuple("Temp", "label current high critical")


def test_psutil_prefers_known_cpu_driver(agent, monkeypatch):
    groups = {
        "nvme": [Temp("Composite", 70.0, None, None)],
        "coretemp": [Temp("Core 0", 50.0, None, None), Temp("Package id 0", 54.0, None, None)],
    }
    monkeypatch.setattr(agent.psutil, "sensors_temperatures", lambda: groups, raising=False)
    assert agent._temp_psutil(None) == 54.0


def test_psutil_raspberry_pi(agent, monkeypatch):
    groups = {"cpu_thermal": [Temp("", 48.3, None, None)]}
    monkeypatch.setattr(agent.psutil, "sensors_temperatures", lambda: groups, raising=False)
    assert agent._temp_psutil(None) == 48.3


def test_psutil_unavailable_on_windows(agent, monkeypatch):
    monkeypatch.delattr(agent.psutil, "sensors_temperatures", raising=False)
    assert agent._temp_psutil(None) is None


@pytest.mark.parametrize("exc", [OSError("no sensors"), RuntimeError("boom")])
def test_psutil_errors_are_no_temperature(agent, monkeypatch, exc):
    def boom():
        raise exc
    monkeypatch.setattr(agent.psutil, "sensors_temperatures", boom, raising=False)
    assert agent._temp_psutil(None) is None
