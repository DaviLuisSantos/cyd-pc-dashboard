#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "config.h"
#include "settings.h"

// Descoberta automática do agente na rede local.
//
// O agente manda um broadcast UDP {"cydpc": 1, "port": 8377, "name": "MEU-PC"}
// a cada ~5s (DISCOVERY_PORT). Aqui só se escuta — não precisa de regra de
// firewall no PC, já que pacote de saída é liberado por padrão.
//
// Com o IP do agente configurado como "auto" (ou vazio), resolve() usa o agente
// escolhido e só troca de agente se ele parar de anunciar por AGENT_STALE_MS.
// Com um IP manual, resolve() devolve ele direto (os anúncios só aparecem na
// interface web, pra ajudar a escolher).
namespace discovery {

static const int MAX_AGENTS = 4;
static const unsigned long AGENT_STALE_MS = 30000;

struct Agent {
    IPAddress ip;
    uint16_t port;
    char name[32];
    bool auth;  // o agente exige token
    unsigned long lastSeenMs;
};

static WiFiUDP udp_;
static bool started_ = false;
static Agent agents_[MAX_AGENTS];
static int count_ = 0;
static int chosen_ = -1;

inline bool isAuto() {
    const char* h = settings::get().agentHost;
    return !*h || !strcasecmp(h, "auto");
}

inline bool fresh(const Agent& a) { return millis() - a.lastSeenMs < AGENT_STALE_MS; }

inline int count() { return count_; }
inline const Agent& at(int i) { return agents_[i]; }

inline void remember(IPAddress ip, uint16_t port, const char* name, bool auth) {
    int slot = -1;
    for (int i = 0; i < count_; i++) {
        if (agents_[i].ip == ip) { slot = i; break; }
    }
    if (slot < 0) {
        if (count_ < MAX_AGENTS) {
            slot = count_++;
        } else {
            // Lista cheia: substitui o que está há mais tempo sem anunciar
            slot = 0;
            for (int i = 1; i < count_; i++) {
                if (agents_[i].lastSeenMs < agents_[slot].lastSeenMs) slot = i;
            }
            if (slot == chosen_) chosen_ = -1;
        }
        Serial.printf("discovery: agente %s:%u (%s)\n", ip.toString().c_str(), port, name);
    }
    agents_[slot].ip = ip;
    agents_[slot].port = port;
    agents_[slot].auth = auth;
    strlcpy(agents_[slot].name, name, sizeof(agents_[slot].name));
    agents_[slot].lastSeenMs = millis();
}

// Chamado no loop(): lê os anúncios que chegaram
inline void poll() {
    if (!started_) {
        if (WiFi.status() != WL_CONNECTED) return;
        started_ = udp_.begin(DISCOVERY_PORT);
    }
    while (udp_.parsePacket() > 0) {
        char buf[192];
        int len = udp_.read(buf, sizeof(buf) - 1);
        if (len <= 0) continue;
        buf[len] = '\0';
        JsonDocument doc;
        if (deserializeJson(doc, buf) || doc["cydpc"].as<int>() != 1) continue;
        uint16_t port = doc["port"] | (uint16_t)AGENT_PORT;
        const char* name = doc["name"] | "?";
        remember(udp_.remoteIP(), port, name, doc["auth"] | false);
    }
}

// Endereço do agente a usar agora. false = modo auto e nenhum agente ouvido ainda.
inline bool resolve(char* host, size_t len, uint16_t& port) {
    const settings::Settings& s = settings::get();
    if (!isAuto()) {
        strlcpy(host, s.agentHost, len);
        port = s.agentPort;
        return true;
    }
    if (chosen_ < 0 || !fresh(agents_[chosen_])) {
        chosen_ = -1;
        for (int i = 0; i < count_; i++) {
            if (fresh(agents_[i]) && (chosen_ < 0 || agents_[i].lastSeenMs > agents_[chosen_].lastSeenMs)) chosen_ = i;
        }
        if (chosen_ < 0) return false;
    }
    strlcpy(host, agents_[chosen_].ip.toString().c_str(), len);
    port = agents_[chosen_].port;
    return true;
}

} // namespace discovery
