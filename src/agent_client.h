#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "i18n.h"
#include "theme.h"

// Cliente HTTP do agente do PC, compartilhado por todas as telas que consomem
// dados dele. Centraliza URL, timeout e o tratamento de erro (status dot).
namespace agent {

static const int STATUS_RIGHT_X = 310;
static const int STATUS_CY = 231;

inline void showStatus(TFT_eSPI& tft, uint16_t color, const char* label) {
    theme::drawStatusDot(tft, STATUS_RIGHT_X, STATUS_CY, color, label);
}

inline void showOk(TFT_eSPI& tft) {
    showStatus(tft, DRACULA_GREEN, i18n::ST_OK);
}

// GET http://AGENT_HOST:AGENT_PORT<path> e desserializa em `doc`.
// Em qualquer falha (sem WiFi, agente fora, JSON quebrado) desenha o status
// correspondente no rodapé e retorna false — a tela mantém os últimos valores.
inline bool fetch(TFT_eSPI& tft, const char* path, JsonDocument& doc) {
    if (WiFi.status() != WL_CONNECTED) {
        showStatus(tft, DRACULA_RED, i18n::ST_NO_WIFI);
        return false;
    }

    HTTPClient http;
    String url = String("http://") + AGENT_HOST + ":" + AGENT_PORT + path;
    http.setTimeout(AGENT_HTTP_TIMEOUT_MS);
    http.begin(url);
    int code = http.GET();

    if (code != 200) {
        http.end();
        showStatus(tft, DRACULA_ORANGE, i18n::ST_AGENT_DOWN);
        return false;
    }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        showStatus(tft, DRACULA_ORANGE, i18n::ST_BAD_JSON);
        return false;
    }
    return true;
}

} // namespace agent
