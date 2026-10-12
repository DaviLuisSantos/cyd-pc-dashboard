"""Discos (nomes, "all", lista) e filtro de processos."""

from collections import namedtuple

import pytest

Part = namedtuple("Part", "device mountpoint fstype opts")
Usage = namedtuple("Usage", "total used free percent")


# ===== nomes =====

@pytest.mark.parametrize("path,name", [
    ("C:\\", "C:"),
    ("d:\\", "D:"),
    ("/", "/"),
    ("/mnt/data/", "data"),
    ("D:\\Games", "Games"),
])
def test_disk_name(agent, path, name):
    assert agent._disk_name(path) == name


# ===== resolução =====

def test_disks_default_is_disk_path(agent):
    agent.CFG["disks"] = None
    agent.CFG["disk_path"] = "D:\\"
    assert agent._resolve_disks() == [("D:\\", "D:")]


def test_disks_all_skips_removable_cdrom_and_pseudo(agent, monkeypatch):
    parts = [
        Part("C:\\", "C:\\", "NTFS", "rw,fixed"),
        Part("F:\\", "F:\\", "FAT32", "rw,removable"),
        Part("E:\\", "E:\\", "", "cdrom"),
        Part("/dev/loop0", "/snap/core", "squashfs", "ro"),
        Part("tmpfs", "/run", "tmpfs", "rw"),
        Part("/dev/sda1", "/home", "ext4", "rw"),
    ]
    monkeypatch.setattr(agent.psutil, "disk_partitions", lambda all=False: parts)
    agent.CFG["disks"] = "all"
    assert agent._resolve_disks() == [("C:\\", "C:"), ("/home", "home")]


def test_disks_all_with_nothing_falls_back(agent, monkeypatch):
    monkeypatch.setattr(agent.psutil, "disk_partitions", lambda all=False: [])
    agent.CFG["disks"] = "all"
    agent.CFG["disk_path"] = "/"
    assert agent._resolve_disks() == [("/", "/")]


def test_disks_explicit_list_with_custom_names(agent):
    agent.CFG["disks"] = ["C:\\", {"path": "D:\\", "name": "GAMES"}, {"name": "sem path"}]
    assert agent._resolve_disks() == [("C:\\", "C:"), ("D:\\", "GAMES")]


def test_disk_usage_skips_missing_disk(agent, monkeypatch):
    def fake_usage(path):
        if path == "Z:\\":
            raise OSError("não existe")
        return Usage(1_000e9, 845.6e9, 154.4e9, 84.56)
    monkeypatch.setattr(agent.psutil, "disk_usage", fake_usage)
    out = agent._disk_usage([("D:\\", "GAMES"), ("Z:\\", "Z:")])
    assert out == [{"name": "GAMES", "pct": 84.6, "used_gb": 845.6, "total_gb": 1000.0}]


def test_disk_usage_name_is_ascii_and_short(agent, monkeypatch):
    monkeypatch.setattr(agent.psutil, "disk_usage", lambda p: Usage(1e9, 5e8, 5e8, 50.0))
    out = agent._disk_usage([("/x", "Músicas e vídeos")])
    assert out[0]["name"] == "Musicas"  # sem acento, até 8 caracteres, sem espaço no fim


# ===== processos =====

@pytest.mark.parametrize("name,patterns,ignored", [
    ("svchost", ["svchost"], True),
    ("chrome", ["msedge*"], False),
    ("msedgewebview2", ["msedge*"], True),
    ("memory compression", ["memory compression"], True),
    ("code", [], False),
])
def test_proc_ignored(agent, name, patterns, ignored):
    assert agent._proc_ignored(name, patterns) is ignored


def test_strip_exe(agent):
    assert agent._strip_exe("chrome.exe") == "chrome"
    assert agent._strip_exe("python3") == "python3"
