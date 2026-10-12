#pragma once
#include <Arduino.h>
#include "config.h"
#include "agent_client.h"

// Dados da aba Serviços (GET /services do agente: os hosts/URLs de "services"
// no agent/config.json, verificados pelo agente). Consultado em segundo plano
// a cada SERVICES_POLL_MS, mesmo com outra aba aberta, pra o ícone da aba piscar
// e o LED pulsar quando algo cai (anyDown()).
namespace services {

static const int MAX = 6;  // cabe na tela

struct Svc {
    char name[22];
    int8_t ok;        // 1 no ar, 0 fora, -1 ainda não verificado
    int16_t ms;       // latência da última resposta (-1 = nenhuma)
    long downForS;    // há quanto tempo está no estado atual (s)
    char detail[24];  // motivo da falha ("timeout", "HTTP 503")
};

static Svc list_[MAX];
static int count_ = 0;
static bool configured_ = true;
static uint32_t version_ = 0;          // muda a cada resposta nova (a tela compara)
static agent::Result lastResult_ = agent::SEARCHING;
static unsigned long lastPollMs_ = 0;
static unsigned long lastDataMs_ = 0;
static bool polledOnce_ = false;

inline int count() { return count_; }
inline const Svc& at(int i) { return list_[i]; }
inline bool configured() { return configured_; }
inline uint32_t version() { return version_; }
inline agent::Result lastResult() { return lastResult_; }

// Algum serviço fora (com dado de no máximo 2 min — agente sumido não deixa
// um "fora" velho aceso)
inline bool anyDown() {
    if (!lastDataMs_ || millis() - lastDataMs_ > 120000) return false;
    for (int i = 0; i < count_; i++) {
        if (list_[i].ok == 0) return true;
    }
    return false;
}

inline void parse(JsonDocument& doc) {
    configured_ = doc["configured"] | false;
    long now = doc["updated_at"] | 0L;
    count_ = 0;
    for (JsonObject s : doc["services"].as<JsonArray>()) {
        if (count_ >= MAX) break;
        Svc& v = list_[count_++];
        strlcpy(v.name, s["name"] | "?", sizeof(v.name));
        v.ok = s["ok"].isNull() ? -1 : (s["ok"].as<bool>() ? 1 : 0);
        v.ms = s["ms"].isNull() ? -1 : (int16_t)s["ms"].as<int>();
        long since = s["since"] | now;
        v.downForS = now > since ? now - since : 0;
        strlcpy(v.detail, s["detail"] | "", sizeof(v.detail));
    }
    lastDataMs_ = millis();
    version_++;
}

// Pede uma consulta já (ao entrar na aba)
inline void requestNow() { lastPollMs_ = 0; polledOnce_ = false; }

// Chamado no loop()
inline void poll() {
    unsigned long interval = lastResult_ == agent::OK ? SERVICES_POLL_MS : 60000UL;
    if (polledOnce_ && millis() - lastPollMs_ < interval) return;
    polledOnce_ = true;
    lastPollMs_ = millis();
    JsonDocument doc;
    agent::Result r = agent::get("/services", doc);
    if (r == agent::OK) parse(doc);
    if (r != lastResult_) version_++;  // a tela também redesenha o status
    lastResult_ = r;
}

} // namespace services
