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
    showStatus(tft, THEME_GREEN, i18n::tr().ST_OK);
}

enum Result { OK, NO_WIFI, SEARCHING, BAD_TOKEN, DOWN, BAD_JSON };

// Prepara a requisição pro agente: endereço via discovery::resolve (IP manual
// do settings.h, ou o agente achado na rede no modo "auto") + token.
inline Result open(HTTPClient& http, const char* path) {
    if (WiFi.status() != WL_CONNECTED) return NO_WIFI;

    char host[64];
    uint16_t port;
    if (!discovery::resolve(host, sizeof(host), port)) return SEARCHING;

    String url = String("http://") + host + ":" + port + path;
    http.setTimeout(AGENT_HTTP_TIMEOUT_MS);
    http.begin(url);
    const char* token = settings::get().agentToken;
    if (*token) http.addHeader("X-Agent-Token", token);
    return OK;
}

// GET http://<agente><path> e desserializa em `doc`, sem desenhar nada (usado
// direto pelos alertas em segundo plano).
inline Result get(const char* path, JsonDocument& doc) {
    HTTPClient http;
    Result r = open(http, path);
    if (r != OK) return r;
    int code = http.GET();

    if (code == 401) { http.end(); return BAD_TOKEN; }
    if (code != 200) { http.end(); return DOWN; }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    return err ? BAD_JSON : OK;
}

// POST sem corpo (aba Atalhos). Devolve o status HTTP, um erro do HTTPClient
// (negativo: agente fora/timeout) ou POST_NO_WIFI/POST_SEARCHING, e
// desserializa a resposta em `doc` (o agente manda {"ok", "error"} também nos
// erros). X-CYD-Panel: o agente recusa POST sem ele (navegador não manda sem preflight).
static const int POST_NO_WIFI = -1000;
static const int POST_SEARCHING = -1001;

inline int post(const char* path, JsonDocument& doc) {
    HTTPClient http;
    Result r = open(http, path);
    if (r == NO_WIFI) return POST_NO_WIFI;
    if (r != OK) return POST_SEARCHING;
    http.addHeader("X-CYD-Panel", "1");
    int code = http.POST((uint8_t*)nullptr, 0);
    if (code > 0 && http.getSize() != 0) deserializeJson(doc, http.getStream());
    http.end();
    return code;
}

// Status no rodapé pra um resultado de get() (OK = "ok" verde)
inline void showResult(TFT_eSPI& tft, Result r) {
    const i18n::Lang& L = i18n::tr();
    switch (r) {
        case OK:        showOk(tft); break;
        case NO_WIFI:   showStatus(tft, THEME_RED, L.ST_NO_WIFI); break;
        case SEARCHING: showStatus(tft, THEME_COMMENT, L.ST_SEARCHING); break;
        case BAD_TOKEN: showStatus(tft, THEME_RED, L.ST_BAD_TOKEN); break;
        case DOWN:      showStatus(tft, THEME_ORANGE, L.ST_AGENT_DOWN); break;
        case BAD_JSON:  showStatus(tft, THEME_ORANGE, L.ST_BAD_JSON); break;
    }
}

// get() + status no rodapé da tela. Em qualquer falha (sem WiFi, agente fora,
// token, JSON quebrado) desenha o status correspondente e retorna false — a
// tela mantém os últimos valores.
inline bool fetch(TFT_eSPI& tft, const char* path, JsonDocument& doc) {
    Result r = get(path, doc);
    if (r == OK) return true;
    showResult(tft, r);
    return false;
}

} // namespace agent
