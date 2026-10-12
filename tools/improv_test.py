"""
Testa o Improv Wi-Fi do painel pela serial, sem navegador. Fala o protocolo
do mesmo jeito que o improv-wifi-serial-sdk usado pelo instalador web (ESP Web
Tools): mesmo formato de pacote e mesma leitura linha a linha.

Uso (precisa de pyserial):
    python tools/improv_test.py --port COM3
    python tools/improv_test.py --port COM3 --boot        # mede a resposta logo depois de um reset
    python tools/improv_test.py --port COM3 --wifi "MinhaRede" "senha"   # troca a rede de verdade

Sem --wifi, o teste de credenciais usa uma rede que não existe: o painel tem
que responder UNABLE_TO_CONNECT e voltar sozinho pra rede atual.
"""

import argparse
import time

import serial

HEADER = b"IMPROV\x01"
STATES = {2: "READY", 3: "PROVISIONING", 4: "PROVISIONED"}
ERRORS = {0: "NO_ERROR", 1: "INVALID_RPC", 2: "UNKNOWN_RPC", 3: "UNABLE_TO_CONNECT", 255: "UNKNOWN"}


def packet(ptype, data):
    body = HEADER + bytes([ptype, len(data)]) + bytes(data)
    return body + bytes([sum(body) & 0xFF]) + b"\n"


def rpc(cmd, data=b""):
    return packet(3, [cmd, len(data)] + list(data))


class Reader:
    """Leitura igual à do SDK: linha a linha, pacote só no início da linha."""

    def __init__(self, ser):
        self.ser, self.line, self.is_improv, self.need = ser, [], None, 0
        self.text = bytearray()  # o que não for Improv (logs, resposta de comandos)

    def packets(self, timeout):
        out, deadline = [], time.time() + timeout
        while time.time() < deadline:
            for b in self.ser.read(256):
                if self.is_improv is False:
                    self.text.append(b)
                    if b == 10:
                        self.is_improv = None
                    continue
                if self.is_improv:
                    self.line.append(b)
                    if len(self.line) == self.need:
                        out.append(self.decode(self.line))
                        self.is_improv, self.line = None, []
                    continue
                if b == 10:
                    self.text += bytes(self.line) + b"\n"
                    self.line = []
                    continue
                self.line.append(b)
                if len(self.line) == 9:
                    self.is_improv = bytes(self.line[:6]) == b"IMPROV"
                    if self.is_improv:
                        self.need = 9 + self.line[8] + 1
                    else:
                        self.text += bytes(self.line)
                        self.line = []
        return out

    @staticmethod
    def decode(line):
        ptype, n = line[7], line[8]
        data = line[9:9 + n]
        if (sum(line[:-1]) & 0xFF) != line[-1]:
            return ("BAD_CHECKSUM",)
        if ptype == 1:
            return ("STATE", STATES.get(data[0], data[0]))
        if ptype == 2:
            return ("ERROR", ERRORS.get(data[0], data[0]))
        if ptype == 4:
            total, idx, strs = data[1], 2, []
            while idx < 2 + total:
                strs.append(bytes(data[idx + 1: idx + 1 + data[idx]]).decode(errors="replace"))
                idx += data[idx] + 1
            return ("RESULT", data[0], strs)
        return ("?", ptype)


def open_port(port, reset=False):
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.05
    ser.dtr = ser.rts = False  # não reseta ao abrir
    ser.open()
    if reset:  # pulso no EN via RTS (o mesmo reset que o esptool usa)
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
    return ser


def wifi_payload(ssid, password):
    s, p = ssid.encode(), password.encode()
    return bytes([len(s)]) + s + bytes([len(p)]) + p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--boot", action="store_true", help="reseta a placa e mede quando o Improv responde")
    ap.add_argument("--wifi", nargs=2, metavar=("SSID", "SENHA"), help="manda credenciais de verdade")
    args = ap.parse_args()

    if args.boot:
        ser = open_port(args.port, reset=True)
        r, t0 = Reader(ser), time.time()
        while time.time() - t0 < 15:
            ser.write(rpc(2))
            res = r.packets(0.25)
            if res:
                print(f"primeira resposta {time.time() - t0:.2f}s depois do reset: {res}")
                break
        else:
            print("sem resposta em 15s")
        ser.close()
        return

    ser = open_port(args.port)
    r = Reader(ser)
    time.sleep(0.3)
    ser.reset_input_buffer()

    def ask(name, pkt, wait):
        ser.write(pkt)
        res = r.packets(wait)
        print(f"{name}: {res}")
        return res

    ask("estado", rpc(2), 1.0)
    ask("info", rpc(3), 1.0)
    # O scan leva vários segundos (mais com o WiFi conectado): lê até o
    # resultado vazio que marca o fim da lista
    ser.write(rpc(4))
    nets, deadline = [], time.time() + 30
    while time.time() < deadline and not any(p[0] == "RESULT" and not p[2] for p in nets):
        nets += r.packets(0.5)
    names = [p[2][0] for p in nets if p[0] == "RESULT" and p[2]]
    print(f"redes: {len(names)} encontradas, fim da lista={any(p[0] == 'RESULT' and not p[2] for p in nets)}")
    ask("RPC desconhecido", rpc(9), 1.0)
    bad = bytearray(rpc(2))
    bad[-2] ^= 0xFF
    ask("checksum errado", bytes(bad), 1.0)
    if args.wifi:
        ask(f"WiFi '{args.wifi[0]}'", rpc(1, wifi_payload(*args.wifi)), 25.0)
    else:
        # SSID com 's','n','a','r': não pode disparar os comandos de uma letra
        ask("WiFi inexistente 'snar'", rpc(1, wifi_payload("snar", "")), 25.0)
    r.text.clear()
    ser.write(b"i")  # comando de uma letra continua funcionando
    r.packets(1.0)
    lines = bytes(r.text).decode(errors="replace").strip().splitlines()
    print("comando 'i':", lines[-1] if lines else "(sem resposta)")
    ser.close()


if __name__ == "__main__":
    main()
