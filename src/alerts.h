#pragma once
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "theme.h"
#include "agent_client.h"

// Alertas por limite de CPU/RAM/disco.
//
// Uma métrica entra em alerta quando fica >= limite (settings.alertCpu/Ram/Disk,
// 0 = desligado) por ALERT_HOLD_MS seguidos — pico curto de CPU não dispara.
// Sai do alerta assim que cai abaixo do limite.
//
// Os valores chegam por feed(): a aba PC alimenta a cada poll dela; com a aba
// fechada, poll() busca /stats em segundo plano a cada ALERT_POLL_MS (30s se o
// agente estiver fora, pra não travar as outras telas no timeout).
namespace alerts {

enum Metric { CPU, RAM, DISK, N_METRICS };

static float values_[N_METRICS] = {};
static unsigned long aboveSince_[N_METRICS] = {};
static bool active_[N_METRICS] = {};
static unsigned long lastFeedMs_ = 0;
static unsigned long lastBgPollMs_ = 0;
static bool lastBgFailed_ = false;

inline uint8_t limit(Metric m) {
    const settings::Settings& s = settings::get();
    return m == CPU ? s.alertCpu : m == RAM ? s.alertRam : s.alertDisk;
}

inline bool enabled() {
    return limit(CPU) || limit(RAM) || limit(DISK);
}

inline bool active(Metric m) { return active_[m]; }
inline float value(Metric m) { return values_[m]; }

inline bool anyActive() {
    for (int m = 0; m < N_METRICS; m++) {
        if (active_[m]) return true;
    }
    return false;
}

// Cor de um valor: vermelho a partir do limite, laranja 20 pontos abaixo.
// Limite 0 (alerta desligado) = sempre a cor base.
inline uint16_t color(Metric m, float pct, uint16_t base) {
    uint8_t lim = limit(m);
    if (!lim) return base;
    if (pct >= lim) return DRACULA_RED;
    if (pct >= lim - 20) return DRACULA_ORANGE;
    return base;
}

inline void feed(float cpu, float ram, float disk) {
    const float v[N_METRICS] = {cpu, ram, disk};
    unsigned long now = millis();
    lastFeedMs_ = now;
    for (int m = 0; m < N_METRICS; m++) {
        values_[m] = v[m];
        uint8_t lim = limit((Metric)m);
        if (!lim || v[m] < lim) {
            aboveSince_[m] = 0;
            active_[m] = false;
            continue;
        }
        if (!aboveSince_[m]) aboveSince_[m] = now ? now : 1;
        if (now - aboveSince_[m] >= ALERT_HOLD_MS) active_[m] = true;
    }
}

// Chamado no loop(). Só busca se ninguém alimentou recentemente (aba PC fechada).
inline void poll() {
    if (!enabled()) {
        for (int m = 0; m < N_METRICS; m++) active_[m] = false;
        return;
    }
    unsigned long now = millis();
    if (now - lastFeedMs_ < ALERT_POLL_MS) return;
    // Sem dados há 1min (agente fora): não deixa um alerta velho aceso
    if (now - lastFeedMs_ > 60000) {
        for (int m = 0; m < N_METRICS; m++) { active_[m] = false; aboveSince_[m] = 0; }
    }
    if (now - lastBgPollMs_ < (lastBgFailed_ ? 30000UL : ALERT_POLL_MS)) return;
    lastBgPollMs_ = now;

    JsonDocument doc;
    lastBgFailed_ = agent::get("/stats", doc) != agent::OK;
    if (!lastBgFailed_) feed(doc["cpu"] | 0.0f, doc["ram"] | 0.0f, doc["disk"] | 0.0f);
}

} // namespace alerts
