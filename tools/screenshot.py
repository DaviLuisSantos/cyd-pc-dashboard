"""
Tira screenshots do display do CYD PC Dashboard (só stdlib; o modo serial
precisa de pyserial).

Dois jeitos de falar com o painel:
  --host  pela interface web (GET /screenshot.bmp, troca de aba por /api/tab)
  --port  pela serial: o firmware responde ao comando 's' despejando o
          framebuffer em RGB565 (ver src/serial_cmds.h)

Uso:
    python tools/screenshot.py --host cyd-dashboard.local                 # tela atual
    python tools/screenshot.py --host cyd-dashboard.local --tabs 6 --grid 3
        # tab1..tab6.png + overview.png (grade de 3 colunas) em docs/screenshots/
    python tools/screenshot.py --host cyd-dashboard.local --themes dracula,nord,gruvbox,mocha,tokyonight,latte --grid 3
        # a aba atual em cada tema -> themes.png (e volta pro tema original)
    python tools/screenshot.py --port COM3 --tabs 6                       # pela serial

Pra screenshots públicos sem dados pessoais, rode tools/demo_agent.py no lugar
do agente real.
"""

import argparse
import base64
import json
import os
import struct
import sys
import time
import urllib.request
import zlib


# ===== Imagem =====

def write_png(path, w, h, rgb):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    print(f"salvo {path}")


def rgb565_to_rgb888(data):
    out = bytearray()
    for (v,) in struct.iter_unpack(">H", data):
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        out += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return bytes(out)


def bmp_to_rgb(data):
    """BMP 24 bits (de baixo pra cima, BGR) -> (w, h, RGB de cima pra baixo)."""
    off, = struct.unpack_from("<I", data, 10)
    w, h = struct.unpack_from("<ii", data, 18)
    row = (w * 3 + 3) & ~3
    out = bytearray()
    for y in range(h - 1, -1, -1):
        line = data[off + y * row: off + y * row + w * 3]
        for x in range(w):
            b, g, r = line[x * 3: x * 3 + 3]
            out += bytes((r, g, b))
    return w, h, bytes(out)


def grid(images, cols, gap=6, gap_color=(255, 255, 255)):
    """Monta uma grade de imagens (w, h, rgb) do mesmo tamanho."""
    w, h = images[0][0], images[0][1]
    rows = -(-len(images) // cols)
    W, H = cols * w + (cols - 1) * gap, rows * h + (rows - 1) * gap
    fill = bytes(gap_color)
    out = bytearray(fill * (W * H))
    for i, (_, _, rgb) in enumerate(images):
        ox, oy = (i % cols) * (w + gap), (i // cols) * (h + gap)
        for y in range(h):
            start = ((oy + y) * W + ox) * 3
            out[start:start + w * 3] = rgb[y * w * 3:(y + 1) * w * 3]
    return W, H, bytes(out)


# ===== Painel pela interface web =====

class HttpPanel:
    def __init__(self, host, password):
        self.base = f"http://{host}"
        self.auth = base64.b64encode(f"admin:{password}".encode()).decode() if password else None

    def _req(self, path, body=None, retries=3):
        for attempt in range(retries):
            req = urllib.request.Request(self.base + path, data=body, method="POST" if body is not None else "GET")
            if body is not None:
                req.add_header("Content-Type", "application/json")
            if self.auth:
                req.add_header("Authorization", f"Basic {self.auth}")
            try:
                with urllib.request.urlopen(req, timeout=20) as r:
                    return r.read()
            except OSError:
                if attempt == retries - 1:
                    raise
                time.sleep(1)

    def settings(self, changes=None):
        if changes is None:
            return json.loads(self._req("/api/settings"))
        return json.loads(self._req("/api/settings", json.dumps(changes).encode()))["settings"]

    def goto_tab(self, index):
        self._req(f"/api/tab?index={index}", b"")

    def capture(self):
        return bmp_to_rgb(self._req("/screenshot.bmp"))


# ===== Painel pela serial =====

class SerialPanel:
    def __init__(self, port, baud):
        import serial  # só o modo serial precisa
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, baud, 0.5
        self.ser.dtr = self.ser.rts = False  # abre sem resetar a placa
        self.ser.open()
        time.sleep(0.5)

    def _line(self, timeout):
        deadline = time.time() + timeout
        buf = b""
        while time.time() < deadline:
            c = self.ser.read(1)
            if c == b"\n":
                return buf.decode(errors="replace").strip()
            buf += c
        raise TimeoutError("sem resposta do painel")

    def pause_autonext(self, pause):
        self.ser.write(b"a")
        state = self._line(5)
        if ("autonext=0" in state) != pause:  # estava no estado oposto: alterna de novo
            self.ser.write(b"a")
            self._line(5)

    def next_tab(self):
        self.ser.write(b"n")
        self._line(5)

    def capture(self):
        self.ser.reset_input_buffer()
        self.ser.write(b"s")
        while True:
            line = self._line(10)
            if line.startswith("SCREENSHOT"):
                break
        _, w, h = line.split()
        w, h = int(w), int(h)
        size = w * h * 2
        data = b""
        deadline = time.time() + 60
        while len(data) < size and time.time() < deadline:
            data += self.ser.read(size - len(data))
        if len(data) < size:
            raise TimeoutError(f"recebidos {len(data)}/{size} bytes")
        return w, h, rgb565_to_rgb888(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--host", help="painel pela interface web (ex.: cyd-dashboard.local)")
    src.add_argument("--port", help="painel pela serial (ex.: COM3)")
    ap.add_argument("--password", default="", help="senha da interface web, se houver")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="docs/screenshots")
    ap.add_argument("--tabs", type=int, default=0, help="captura N abas em sequência (0 = só a atual)")
    ap.add_argument("--themes", help="captura a aba atual em cada tema (lista separada por vírgula; só --host)")
    ap.add_argument("--grid", type=int, default=0, help="também monta overview.png/themes.png com N colunas")
    ap.add_argument("--settle", type=float, default=4.0, help="segundos esperando a aba carregar dados")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    if args.themes:
        if not args.host:
            ap.error("--themes precisa de --host")
        panel = HttpPanel(args.host, args.password)
        original = panel.settings()
        panel.settings({"autonext": 0})
        shots = []
        try:
            for theme in args.themes.split(","):
                panel.settings({"theme": theme.strip()})
                time.sleep(args.settle)
                shots.append(panel.capture())
                write_png(os.path.join(args.out, f"theme-{theme.strip()}.png"), *shots[-1])
        finally:
            panel.settings({"theme": original["theme"], "autonext": original["autonext"]})
        if args.grid:
            write_png(os.path.join(args.out, "themes.png"), *grid(shots, args.grid))
        return

    if args.host:
        panel = HttpPanel(args.host, args.password)
        if args.tabs <= 0:
            write_png(os.path.join(args.out, time.strftime("screen-%Y%m%d-%H%M%S.png")), *panel.capture())
            return
        original = panel.settings()
        panel.settings({"autonext": 0})
        shots = []
        try:
            for i in range(args.tabs):
                panel.goto_tab(i)
                time.sleep(args.settle)
                shots.append(panel.capture())
                write_png(os.path.join(args.out, f"tab{i + 1}.png"), *shots[-1])
        finally:
            panel.settings({"autonext": original["autonext"]})
    else:
        panel = SerialPanel(args.port, args.baud)
        if args.tabs <= 0:
            write_png(os.path.join(args.out, time.strftime("screen-%Y%m%d-%H%M%S.png")), *panel.capture())
            return
        panel.pause_autonext(True)
        shots = []
        try:
            for i in range(args.tabs):
                time.sleep(args.settle)
                shots.append(panel.capture())
                write_png(os.path.join(args.out, f"tab{i + 1}.png"), *shots[-1])
                panel.next_tab()
        finally:
            panel.pause_autonext(False)

    if args.grid:
        write_png(os.path.join(args.out, "overview.png"), *grid(shots, args.grid))


if __name__ == "__main__":
    sys.exit(main())
