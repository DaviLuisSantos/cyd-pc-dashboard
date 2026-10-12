#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include "config.h"

// Improv Wi-Fi pela serial (https://www.improv-wifi.com/serial/): é o que faz o
// instalador web (ESP Web Tools) oferecer "Connect to Wi-Fi" logo depois de
// gravar — a pessoa escolhe a rede no navegador, sem passar pelo portal.
//
// Pacote (nos dois sentidos):
//   "IMPROV" | versão (1) | tipo | tamanho | dados... | checksum (soma de tudo antes, & 0xFF)
// O lado do navegador lê a serial linha a linha e só reconhece pacote que
// começa no início de uma linha — por isso cada pacote sai entre '\n's.
//
// A serial é compartilhada com os comandos de uma letra (serial_cmds.h): todo
// byte passa primeiro por feed(); o que não for Improv volta pra quem chamou.
namespace improv {

enum Type : uint8_t { CURRENT_STATE = 0x01, ERROR_STATE = 0x02, RPC = 0x03, RPC_RESULT = 0x04 };
enum State : uint8_t { READY = 0x02, PROVISIONING = 0x03, PROVISIONED = 0x04 };
enum Error : uint8_t { NO_ERROR = 0x00, INVALID_RPC = 0x01, UNKNOWN_RPC = 0x02, UNABLE_TO_CONNECT = 0x03 };
enum Command : uint8_t { WIFI_SETTINGS = 0x01, GET_STATE = 0x02, GET_INFO = 0x03, GET_NETWORKS = 0x04 };

// Quem sabe conectar no WiFi (wifi_setup.h durante o boot/portal, main.cpp
// depois). Recebe SSID e senha; devolve true se conectou.
using ConnectFn = std::function<bool(const char* ssid, const char* pass)>;
static ConnectFn connectFn_;

static uint8_t buf_[256];
static int len_ = 0;
static unsigned long lastByteMs_ = 0;

inline void setConnectHandler(ConnectFn fn) { connectFn_ = fn; }

inline void send(Type type, const uint8_t* data, uint8_t n) {
    uint8_t head[9] = {'I', 'M', 'P', 'R', 'O', 'V', 1, type, n};
    uint8_t sum = 0;
    for (uint8_t b : head) sum += b;
    for (int i = 0; i < n; i++) sum += data[i];
    Serial.write('\n');
    Serial.write(head, sizeof(head));
    if (n) Serial.write(data, n);
    Serial.write(sum);
    Serial.write('\n');
    Serial.flush();
}

inline void sendState(State s) {
    uint8_t d = s;
    send(CURRENT_STATE, &d, 1);
}

inline void sendError(Error e) {
    uint8_t d = e;
    send(ERROR_STATE, &d, 1);
}

// RPC result: comando, tamanho total, e cada string como [tamanho][bytes]
inline void sendResult(Command cmd, const char* const* strings, int count) {
    uint8_t d[250];
    int n = 2;
    for (int i = 0; i < count; i++) {
        int l = strlen(strings[i]);
        if (n + 1 + l > (int)sizeof(d)) break;
        d[n++] = (uint8_t)l;
        memcpy(d + n, strings[i], l);
        n += l;
    }
    d[0] = cmd;
    d[1] = (uint8_t)(n - 2);
    send(RPC_RESULT, d, (uint8_t)n);
}

inline String deviceUrl() {
    return String("http://") + WiFi.localIP().toString() + "/";
}

inline void sendProvisioned(Command cmd) {
    String url = deviceUrl();
    const char* s[] = {url.c_str()};
    sendResult(cmd, s, 1);
}

inline bool connected() { return WiFi.status() == WL_CONNECTED; }

inline void handleRpc(const uint8_t* d, int n) {
    if (n < 2 || d[1] != n - 2) {
        sendError(INVALID_RPC);
        return;
    }
    const uint8_t cmd = d[0];
    const uint8_t* data = d + 2;
    const int dlen = n - 2;

    switch (cmd) {
        case GET_STATE:
            sendState(connected() ? PROVISIONED : READY);
            if (connected()) sendProvisioned(GET_STATE);
            break;

        case GET_INFO: {
            const char* s[] = {"CYD PC Dashboard", FW_VERSION, "ESP32", "CYD-Dashboard"};
            sendResult(GET_INFO, s, 4);
            break;
        }

        case GET_NETWORKS: {
            // Bloqueia ~2-3s; o navegador espera (sem timeout nesse comando)
            int found = WiFi.scanNetworks();
            for (int i = 0; i < found; i++) {
                String ssid = WiFi.SSID(i);
                if (!ssid.length()) continue;  // rede oculta
                char rssi[8];
                snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI(i));
                const char* s[] = {ssid.c_str(), rssi, WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"};
                sendResult(GET_NETWORKS, s, 3);
            }
            WiFi.scanDelete();
            sendResult(GET_NETWORKS, nullptr, 0);  // resultado vazio = fim da lista
            break;
        }

        case WIFI_SETTINGS: {
            // dados: [len ssid][ssid][len senha][senha]
            if (dlen < 2 || data[0] + 2 > dlen || data[0] + 1 + data[data[0] + 1] + 1 > dlen) {
                sendError(INVALID_RPC);
                return;
            }
            char ssid[33] = "", pass[65] = "";
            uint8_t sl = min<uint8_t>(data[0], 32), pl = min<uint8_t>(data[data[0] + 1], 64);
            memcpy(ssid, data + 1, sl);
            memcpy(pass, data + data[0] + 2, pl);
            sendState(PROVISIONING);
            Serial.printf("improv: conectando em \"%s\"\n", ssid);
            if (connectFn_ && connectFn_(ssid, pass)) {
                sendState(PROVISIONED);
                sendProvisioned(WIFI_SETTINGS);
            } else {
                sendError(UNABLE_TO_CONNECT);
                sendState(READY);
            }
            break;
        }

        default:
            sendError(UNKNOWN_RPC);
    }
}

inline void handlePacket() {
    // buf_ = "IMPROV" v tipo len dados... checksum
    uint8_t sum = 0;
    for (int i = 0; i < len_ - 1; i++) sum += buf_[i];
    if (sum != buf_[len_ - 1] || buf_[6] != 1) {
        sendError(INVALID_RPC);
        return;
    }
    if (buf_[7] == RPC) handleRpc(buf_ + 9, buf_[8]);
}

// Consome um byte. true = era (ou pode ser) parte de um pacote Improv; false =
// não é, e o byte vale como comando de uma letra.
inline bool feed(uint8_t b) {
    static const char HEADER[] = "IMPROV";
    if (len_ > 0 && millis() - lastByteMs_ > 1000) len_ = 0;  // pacote abandonado
    lastByteMs_ = millis();

    if (len_ < 6) {
        if (b == (uint8_t)HEADER[len_]) {
            buf_[len_++] = b;
            return true;
        }
        bool wasPartial = len_ > 0;
        len_ = 0;
        // 'I' recomeça um cabeçalho; o resto só foi engolido se havia um parcial
        if (b == 'I') {
            buf_[len_++] = b;
            return true;
        }
        return wasPartial;
    }
    buf_[len_++] = b;
    if (len_ >= 9 && len_ == 9 + buf_[8] + 1) {
        handlePacket();
        len_ = 0;
    }
    return true;
}

// Atende só o Improv (boot, portal, espera de conexão — onde os comandos de
// uma letra não fazem sentido)
inline void poll() {
    while (Serial.available()) feed((uint8_t)Serial.read());
}

} // namespace improv
