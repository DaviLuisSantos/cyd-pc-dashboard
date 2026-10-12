#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// Configurações de runtime — editáveis pelo portal de configuração (WiFiManager)
// e salvas no NVS da placa. O que nunca foi salvo cai no default de compilação
// (config.h / user_config.h), então quem compila com user_config.h continua
// funcionando sem passar pelo portal.
namespace settings {

struct Settings {
    char agentHost[64];
    uint16_t agentPort;
    char tz[64];
    uint8_t lang;  // LANG_EN / LANG_PT_BR
};

static const char* NVS_NAMESPACE = "cydpc";

inline Settings& get() {
    static Settings s;
    return s;
}

inline void load() {
    Settings& s = get();
    Preferences p;
    p.begin(NVS_NAMESPACE, false);  // RW: cria o namespace na primeira vez
    strlcpy(s.agentHost, p.getString("host", AGENT_HOST).c_str(), sizeof(s.agentHost));
    s.agentPort = p.getUShort("port", AGENT_PORT);
    strlcpy(s.tz, p.getString("tz", TZ_INFO).c_str(), sizeof(s.tz));
    s.lang = p.getUChar("lang", PANEL_LANG);
    p.end();
}

inline void save() {
    const Settings& s = get();
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putString("host", s.agentHost);
    p.putUShort("port", s.agentPort);
    p.putString("tz", s.tz);
    p.putUChar("lang", s.lang);
    p.end();
}

// Apaga o que foi salvo pelo portal (volta pros defaults de compilação)
inline void reset() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.clear();
    p.end();
}

} // namespace settings
