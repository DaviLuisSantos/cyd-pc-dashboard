#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "screens.h"
#include <WiFi.h>
#include "settings.h"
#include "improv.h"

// Comandos de debug pelo monitor serial (115200). Um caractere por comando:
//   s  screenshot: despeja o framebuffer do display (ver tools/screenshot.py)
//   n  vai pra próxima aba
//   a  liga/desliga a troca automática de abas
//   r  apaga as configurações do portal (NVS) e reinicia
//   i  mostra versão, IP e o endereço da interface web
// Pacotes do Improv Wi-Fi (instalador web) chegam pela mesma serial: todo byte
// passa antes por improv::feed(), que fica com o que for do Improv.
namespace serialcmds {

// Formato: linha "SCREENSHOT <w> <h>\n", depois w*h pixels RGB565 big-endian
// (linha por linha, de cima pra baixo), depois "\nEND\n".
inline void dumpScreen(TFT_eSPI& tft) {
    const int w = tft.width(), h = tft.height();
    static uint16_t line[320];
    Serial.printf("SCREENSHOT %d %d\n", w, h);
    for (int y = 0; y < h; y++) {
        tft.readRect(0, y, w, 1, line);
        Serial.write((const uint8_t*)line, w * sizeof(uint16_t));
    }
    Serial.print("\nEND\n");
}

inline void poll(TFT_eSPI& tft, ScreenManager& manager) {
    while (Serial.available()) {
        int c = Serial.read();
        if (improv::feed((uint8_t)c)) continue;
        switch (c) {
            case 's':
                dumpScreen(tft);
                break;
            case 'n':
                manager.nextScreen();
                Serial.println("OK next");
                break;
            case 'a':
                manager.setAutoNext(!manager.autoNext());
                Serial.printf("OK autonext=%d\n", manager.autoNext());
                break;
            case 'i':
                Serial.printf("INFO version=%s ip=%s web=http://%s/ mdns=http://%s.local/\n", FW_VERSION,
                              WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str(), MDNS_NAME);
                break;
            case 'r':
                settings::reset();
                Serial.println("OK settings reset, rebooting");
                delay(200);
                ESP.restart();
                break;
        }
    }
}

} // namespace serialcmds
