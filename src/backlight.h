#pragma once
#include <Arduino.h>
#include <time.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "settings.h"
#include "ambient.h"

// Backlight via PWM + modo noturno.
//
// Entre nightStart e nightEnd (hora local, via NTP) o brilho cai para
// nightBright. Com nightBright = 0 a tela apaga: o primeiro toque só acende
// (não chega na tela/aba) e ela volta a apagar depois de NIGHT_WAKE_MS sem
// toque. Início == fim desativa o modo noturno. Sem hora válida (NTP ainda
// não sincronizou, sem WiFi) fica sempre no brilho de dia.
namespace backlight {

static const int PWM_CH = 0;

static uint8_t current_ = 0;
static bool awake_ = false;            // acordado por toque durante a noite
static unsigned long lastTouchMs_ = 0;
static unsigned long lastCheckMs_ = 0;

inline void set(uint8_t level) {
    ledcWrite(PWM_CH, level);
    current_ = level;
}

// Transição suave (~200ms no pior caso, bloqueante)
inline void fadeTo(uint8_t level) {
    int duty = current_;
    while (duty != level) {
        duty += (level > duty) ? min(5, level - duty) : -min(5, duty - level);
        ledcWrite(PWM_CH, duty);
        delay(4);
    }
    current_ = level;
}

inline void begin() {
    ledcSetup(PWM_CH, 5000, 8);
    ledcAttachPin(TFT_BL, PWM_CH);
    set(0);
}

inline bool inNightWindow() {
    const settings::Settings& s = settings::get();
    if (s.nightStart == s.nightEnd) return false;
    struct tm t;
    if (!getLocalTime(&t, 0)) return false;
    int now = t.tm_hour * 60 + t.tm_min;
    if (s.nightStart < s.nightEnd) return now >= s.nightStart && now < s.nightEnd;
    return now >= s.nightStart || now < s.nightEnd;  // atravessa a meia-noite (ex.: 23:00-07:00)
}

// Brilho "de dia": o configurado, ou — com o automático ligado — entre automin
// (escuro) e o configurado (claro), pela luz ambiente. Arredondado em passos de
// 8 pra variação mínima do sensor não ficar refazendo o fade.
inline uint8_t dayLevel() {
    const settings::Settings& s = settings::get();
    if (!s.autoBright) return s.brightness;
    int lo = min(s.autoMin, s.brightness), hi = s.brightness;
    int v = lo + (int)((hi - lo) * ambient::level() + 0.5f);
    v = (v + 4) / 8 * 8;
    return (uint8_t)constrain(v, lo, hi);
}

inline uint8_t targetLevel() {
    const settings::Settings& s = settings::get();
    if (!inNightWindow()) return dayLevel();
    if (awake_ && millis() - lastTouchMs_ < NIGHT_WAKE_MS) return dayLevel();
    awake_ = false;
    return s.nightBright;
}

inline uint8_t level() { return current_; }

// Chamado no loop(); reavalia a cada 1s (force = agora, ex.: config mudou)
inline void update(bool force = false) {
    if (!force && millis() - lastCheckMs_ < 1000) return;
    lastCheckMs_ = millis();
    uint8_t target = targetLevel();
    if (target != current_) fadeTo(target);
}

// Acende a tela como um toque faria (notificação chegando à noite)
inline void wake() {
    lastTouchMs_ = millis();
    if (!inNightWindow()) return;
    awake_ = true;
    if (current_ < dayLevel()) fadeTo(dayLevel());
}

// Chamado a cada toque. Retorna true se o toque foi "consumido" para acordar
// a tela apagada — aí ele não deve ser repassado pras abas.
inline bool onTouch() {
    lastTouchMs_ = millis();
    if (!inNightWindow()) return false;
    awake_ = true;
    if (current_ > 0) return false;
    fadeTo(dayLevel());
    return true;
}

} // namespace backlight
