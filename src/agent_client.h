#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "i18n.h"
#include "theme.h"
#include "settings.h"
#include "discovery.h"

// Cliente HTTP do agente do PC, compartilhado por todas as telas que consomem
// dados dele. Centraliza URL, timeout e o tratamento de erro (status dot).
namespace agent {

static const int STATUS_RIGHT_X = 310;
static const int STATUS_CY = 231;

inline void showStatus(TFT_eSPI& tft, uint16_t color, const char* label) {
    theme::drawStatusDot(tft, STATUS_RIGHT_X, STATUS_CY, color, label);
}

inline void showOk(TFT_eSPI& tft) {
    showStatus(tft, DRACULA_GREEN, i18n::tr().ST_OK);
}

// GET http://<agente><path> e desserializa em `doc`. O endereço vem de
// discovery::resolve (IP manual do settings.h, ou o agente achado na rede no modo "auto").
// Em qualquer falha (sem WiFi, agente fora, JSON quebrado) desenha o status
// correspondente no rodapé e retorna false — a tela mantém os últimos valores.
inline bool fetch(TFT_eSPI& tft, const char* path, JsonDocument& doc) {
    if (WiFi.status() != WL_CONNECTED) {
        showStatus(tft, DRACULA_RED, i18n::tr().ST_NO_WIFI);
        return false;
    }

    char host[64];
    uint16_t port;
    if (!discovery::resolve(host, sizeof(host), port)) {
        showStatus(tft, DRACULA_COMMENT, i18n::tr().ST_SEARCHING);
        return false;
    }

    HTTPClient http;
    String url = String("http://") + host + ":" + port + path;
    http.setTimeout(AGENT_HTTP_TIMEOUT_MS);
    http.begin(url);
    const char* token = settings::get().agentToken;
    if (*token) http.addHeader("X-Agent-Token", token);
    int code = http.GET();

    if (code == 401) {
        http.end();
        showStatus(tft, DRACULA_RED, i18n::tr().ST_BAD_TOKEN);
        return false;
    }
    if (code != 200) {
        http.end();
        showStatus(tft, DRACULA_ORANGE, i18n::tr().ST_AGENT_DOWN);
        return false;
    }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        showStatus(tft, DRACULA_ORANGE, i18n::tr().ST_BAD_JSON);
        return false;
    }
    return true;
}

} // namespace agent
