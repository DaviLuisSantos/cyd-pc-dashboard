"""
LibreHardwareMonitor de mentira, pra testar a temperatura da CPU sem o LHM.

Serve GET /data.json no formato do Remote Web Server do LHM, com um sensor de
CPU (e um de GPU, que o agente precisa ignorar). A temperatura vem do arquivo
passado em --file (dá pra editar com o servidor rodando) ou de --temp.

Uso:
    python tools/mock_lhm.py --temp 61.4
    python tools/mock_lhm.py --file temp.txt        # echo 90,2 > temp.txt
e no agent/config.json:
    "temperature": {"source": "lhm", "lhm_url": "http://127.0.0.1:18085/data.json"}
"""

import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer


def tree(temp):
    def sensor(text, value, sid):
        return {"Text": text, "Value": f"{value} °C", "SensorId": sid, "Type": "Temperature", "Children": []}

    return {"Text": "Sensor", "Children": [{"Text": "PC", "ImageURL": "images_icon/computer.png", "Children": [
        {"Text": "AMD Ryzen 7 5800X", "ImageURL": "images_icon/cpu.png", "Children": [
            {"Text": "Temperatures", "ImageURL": "images_icon/temperature.png", "Children": [
                sensor("Core (Tctl/Tdie)", temp, "/amdcpu/0/temperature/2")]}]},
        {"Text": "NVIDIA GeForce RTX 3070", "ImageURL": "images_icon/nvidia.png", "Children": [
            {"Text": "Temperatures", "ImageURL": "images_icon/temperature.png", "Children": [
                sensor("GPU Core", "72.0", "/gpu-nvidia/0/temperature/0")]}]},
    ]}]}


def main():
    p = argparse.ArgumentParser(description="LibreHardwareMonitor de mentira")
    p.add_argument("--port", type=int, default=18085)
    p.add_argument("--temp", default="55.0", help="temperatura fixa (aceita vírgula)")
    p.add_argument("--file", help="arquivo com a temperatura, relido a cada request")
    args = p.parse_args()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            temp = open(args.file).read().strip() if args.file else args.temp
            body = json.dumps(tree(temp), ensure_ascii=False).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    print(f"LHM de mentira em http://127.0.0.1:{args.port}/data.json")
    HTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
