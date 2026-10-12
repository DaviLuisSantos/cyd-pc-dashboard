#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// Configurações de runtime — editáveis pelo portal de configuração (WiFiManager)
// ou pela interface web (web_ui.h) e salvas no NVS da placa. O que nunca foi salvo cai no default de compilação
// (config.h / user_config.h), então quem compila com user_config.h continua
// funcionando sem passar pelo portal.
namespace settings {

struct Settings {
    char agentHost[64];
    uint16_t agentPort;
    char tz[64];
    uint8_t lang;  // LANG_EN / LANG_PT_BR
    char screens[96];     // ids das abas, na ordem (ex.: "pcstats,procs,quotes")
    uint16_t autoNextS;   // troca automática de aba, em segundos (0 = desativa)
    uint8_t brightness;   // backlight 0-255
    uint8_t rotation;     // 1 = USB à direita, 3 = USB à esquerda
    bool invert;          // inverte as cores do display (varia por unidade do CYD)
    uint16_t nightStart;  // modo noturno: minutos desde 00:00 (início == fim desativa)
    uint16_t nightEnd;
    uint8_t nightBright;  // brilho à noite (0 = tela apagada)
    char webPass[33];     // senha da interface web (vazio = sem senha)
    uint8_t clockFmt;     // 24 ou 12
    uint8_t dateFmt;      // DATE_AUTO, DATE_LONG...
    char agentToken[65];  // token do agente (vazio = agente sem token)
    uint8_t alertCpu;     // limites de alerta em % (0 = desligado)
    uint8_t alertRam;
    uint8_t alertDisk;
    bool alertJump;       // pula pra aba PC quando um alerta começa
};

enum DateFormat : uint8_t { DATE_AUTO, DATE_LONG, DATE_DMY, DATE_MDY, DATE_YMD };
static const char* const DATE_FORMAT_NAMES[] = {"auto", "long", "dmy", "mdy", "ymd"};

inline int parseDateFormat(const char* v) {
    for (int i = 0; i < 5; i++) {
        if (!strcasecmp(v, DATE_FORMAT_NAMES[i])) return i;
    }
    return -1;
}

static const char* NVS_NAMESPACE = "cydpc";

// "HH:MM" -> minutos desde a meia-noite; -1 se inválido
inline int parseHHMM(const char* str) {
    int h, m;
    if (sscanf(str, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

inline void formatHHMM(uint16_t minutes, char* buf, size_t len) {
    snprintf(buf, len, "%02u:%02u", minutes / 60, minutes % 60);
}

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
    strlcpy(s.screens, p.getString("screens", SCREEN_ORDER).c_str(), sizeof(s.screens));
    s.autoNextS = p.getUShort("autonext", AUTO_NEXT_MS / 1000);
    s.brightness = p.getUChar("bright", BACKLIGHT_LEVEL);
    s.rotation = p.getUChar("rot", DISPLAY_ROTATION);
    s.invert = p.getBool("invert", DISPLAY_INVERT);
    s.nightStart = p.getUShort("nstart", max(0, parseHHMM(NIGHT_START)));
    s.nightEnd = p.getUShort("nend", max(0, parseHHMM(NIGHT_END)));
    s.nightBright = p.getUChar("nbright", NIGHT_BRIGHTNESS);
    strlcpy(s.webPass, p.getString("webpass", WEB_PASSWORD).c_str(), sizeof(s.webPass));
    s.clockFmt = p.getUChar("clock", CLOCK_FORMAT == 12 ? 12 : 24);
    s.dateFmt = p.getUChar("date", (uint8_t)max(0, parseDateFormat(DATE_FORMAT)));
    strlcpy(s.agentToken, p.getString("token", AGENT_TOKEN).c_str(), sizeof(s.agentToken));
    s.alertCpu = p.getUChar("acpu", ALERT_CPU);
    s.alertRam = p.getUChar("aram", ALERT_RAM);
    s.alertDisk = p.getUChar("adisk", ALERT_DISK);
    s.alertJump = p.getBool("ajump", ALERT_JUMP);
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
    p.putString("screens", s.screens);
    p.putUShort("autonext", s.autoNextS);
    p.putUChar("bright", s.brightness);
    p.putUChar("rot", s.rotation);
    p.putBool("invert", s.invert);
    p.putUShort("nstart", s.nightStart);
    p.putUShort("nend", s.nightEnd);
    p.putUChar("nbright", s.nightBright);
    p.putString("webpass", s.webPass);
    p.putUChar("clock", s.clockFmt);
    p.putUChar("date", s.dateFmt);
    p.putString("token", s.agentToken);
    p.putUChar("acpu", s.alertCpu);
    p.putUChar("aram", s.alertRam);
    p.putUChar("adisk", s.alertDisk);
    p.putBool("ajump", s.alertJump);
    p.end();
}

// Chaves aceitas por setField/getField — as mesmas no portal, na interface web
// e na API (/api/settings). webpass e token ficam de fora do getField (segredos
// não são devolvidos pela API; a web só mostra se estão definidos).
static const char* const KEYS[] = {
    "host", "port", "tz", "lang", "screens", "autonext", "bright", "rot", "invert",
    "nstart", "nend", "nbright", "webpass", "clock", "date", "token", "acpu", "aram", "adisk", "ajump",
};

// Valida e grava um campo (só em memória; chamar save() depois).
// Retorna false se a chave não existe ou o valor é inválido (campo intacto).
inline bool setField(const char* key, const char* v) {
    Settings& s = get();
    if (!strcmp(key, "host")) {
        if (!*v) return false;
        strlcpy(s.agentHost, v, sizeof(s.agentHost));
    } else if (!strcmp(key, "port")) {
        int port = atoi(v);
        if (port <= 0 || port > 65535) return false;
        s.agentPort = (uint16_t)port;
    } else if (!strcmp(key, "tz")) {
        if (!*v) return false;
        strlcpy(s.tz, v, sizeof(s.tz));
    } else if (!strcmp(key, "lang")) {
        if (!strncasecmp(v, "pt", 2)) s.lang = LANG_PT_BR;
        else if (!strncasecmp(v, "en", 2)) s.lang = LANG_EN;
        else return false;
    } else if (!strcmp(key, "screens")) {
        strlcpy(s.screens, v, sizeof(s.screens));
    } else if (!strcmp(key, "autonext")) {
        s.autoNextS = (uint16_t)constrain(atoi(v), 0, 3600);
    } else if (!strcmp(key, "bright")) {
        // Mínimo 10: brilho 0 deixaria a tela apagada sem jeito óbvio de voltar
        s.brightness = (uint8_t)constrain(atoi(v), 10, 255);
    } else if (!strcmp(key, "rot")) {
        int rot = atoi(v);
        if (rot != 1 && rot != 3) return false;
        s.rotation = (uint8_t)rot;
    } else if (!strcmp(key, "invert")) {
        s.invert = !strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on");
    } else if (!strcmp(key, "nstart") || !strcmp(key, "nend")) {
        int m = parseHHMM(v);
        if (m < 0) return false;
        (key[1] == 's' ? s.nightStart : s.nightEnd) = (uint16_t)m;
    } else if (!strcmp(key, "nbright")) {
        s.nightBright = (uint8_t)constrain(atoi(v), 0, 255);
    } else if (!strcmp(key, "webpass")) {
        strlcpy(s.webPass, v, sizeof(s.webPass));
    } else if (!strcmp(key, "acpu") || !strcmp(key, "aram") || !strcmp(key, "adisk")) {
        int pct = atoi(v);
        if (pct < 0 || pct > 100) return false;
        (key[1] == 'c' ? s.alertCpu : key[1] == 'r' ? s.alertRam : s.alertDisk) = (uint8_t)pct;
    } else if (!strcmp(key, "ajump")) {
        s.alertJump = !strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on");
    } else if (!strcmp(key, "token")) {
        strlcpy(s.agentToken, v, sizeof(s.agentToken));
    } else if (!strcmp(key, "clock")) {
        int c = atoi(v);  // aceita "12", "12h", "24"...
        if (c != 12 && c != 24) return false;
        s.clockFmt = (uint8_t)c;
    } else if (!strcmp(key, "date")) {
        int d = parseDateFormat(v);
        if (d < 0) return false;
        s.dateFmt = (uint8_t)d;
    } else {
        return false;
    }
    return true;
}

// Valor atual de um campo como texto (mesmo formato aceito por setField)
inline bool getField(const char* key, char* buf, size_t len) {
    const Settings& s = get();
    if (!strcmp(key, "host")) strlcpy(buf, s.agentHost, len);
    else if (!strcmp(key, "port")) snprintf(buf, len, "%u", s.agentPort);
    else if (!strcmp(key, "tz")) strlcpy(buf, s.tz, len);
    else if (!strcmp(key, "lang")) strlcpy(buf, s.lang == LANG_PT_BR ? "pt" : "en", len);
    else if (!strcmp(key, "screens")) strlcpy(buf, s.screens, len);
    else if (!strcmp(key, "autonext")) snprintf(buf, len, "%u", s.autoNextS);
    else if (!strcmp(key, "bright")) snprintf(buf, len, "%u", s.brightness);
    else if (!strcmp(key, "rot")) snprintf(buf, len, "%u", s.rotation);
    else if (!strcmp(key, "invert")) strlcpy(buf, s.invert ? "1" : "0", len);
    else if (!strcmp(key, "nstart")) formatHHMM(s.nightStart, buf, len);
    else if (!strcmp(key, "nend")) formatHHMM(s.nightEnd, buf, len);
    else if (!strcmp(key, "nbright")) snprintf(buf, len, "%u", s.nightBright);
    else if (!strcmp(key, "clock")) snprintf(buf, len, "%u", s.clockFmt);
    else if (!strcmp(key, "acpu")) snprintf(buf, len, "%u", s.alertCpu);
    else if (!strcmp(key, "aram")) snprintf(buf, len, "%u", s.alertRam);
    else if (!strcmp(key, "adisk")) snprintf(buf, len, "%u", s.alertDisk);
    else if (!strcmp(key, "ajump")) strlcpy(buf, s.alertJump ? "1" : "0", len);
    else if (!strcmp(key, "date")) strlcpy(buf, DATE_FORMAT_NAMES[s.dateFmt < 5 ? s.dateFmt : 0], len);
    else return false;
    return true;
}

// Apaga o que foi salvo pelo portal (volta pros defaults de compilação)
inline void reset() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.clear();
    p.end();
}

} // namespace settings
