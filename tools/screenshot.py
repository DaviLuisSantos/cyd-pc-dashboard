"""
Tira screenshots do display do CYD PC Dashboard pela serial.

O firmware responde ao comando 's' despejando o framebuffer (ver
src/serial_cmds.h); este script converte RGB565 -> PNG (só stdlib + pyserial).

Uso:
    pip install pyserial
    python tools/screenshot.py --port COM3                 # tela atual
    python tools/screenshot.py --port COM3 --tabs 6        # uma por aba
"""

import argparse
import os
import struct
import sys
import time
import zlib

import serial


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


def rgb565_to_rgb888(data):
    out = bytearray()
    for (v,) in struct.iter_unpack(">H", data):
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        out += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return bytes(out)


def read_line(ser, timeout):
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        c = ser.read(1)
        if not c:
            continue
        if c == b"\n":
            return buf.decode(errors="replace").strip()
        buf += c
    raise TimeoutError("sem resposta do painel")


def capture(ser, path):
    ser.reset_input_buffer()
    ser.write(b"s")
    while True:
        line = read_line(ser, 10)
        if line.startswith("SCREENSHOT"):
            break
    _, w, h = line.split()
    w, h = int(w), int(h)
    size = w * h * 2
    data = b""
    deadline = time.time() + 60
    while len(data) < size and time.time() < deadline:
        data += ser.read(size - len(data))
    if len(data) < size:
        raise TimeoutError(f"recebidos {len(data)}/{size} bytes")
    write_png(path, w, h, rgb565_to_rgb888(data))
    print(f"salvo {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="docs/screenshots")
    ap.add_argument("--tabs", type=int, default=0, help="captura N abas em sequência (0 = só a atual)")
    ap.add_argument("--settle", type=float, default=4.0, help="segundos esperando a aba carregar dados")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    # Abre sem mexer em DTR/RTS pra não resetar a placa
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = args.port, args.baud, 0.5
    ser.dtr = ser.rts = False
    ser.open()
    time.sleep(0.5)

    try:
        if args.tabs <= 0:
            capture(ser, os.path.join(args.out, time.strftime("screen-%Y%m%d-%H%M%S.png")))
            return
        ser.write(b"a")  # pausa a troca automática de abas
        if "autonext=0" not in read_line(ser, 5):
            ser.write(b"a")  # já estava pausada e acabamos de religar
            read_line(ser, 5)
        for i in range(args.tabs):
            time.sleep(args.settle)
            capture(ser, os.path.join(args.out, f"tab{i + 1}.png"))
            ser.write(b"n")
            read_line(ser, 5)
        ser.write(b"a")  # religa a troca automática
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
