#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "settings.h"
#include "alerts.h"
#include "notify.h"

// LED RGB da CYD como luz de status — avisa mesmo com a tela apagada à noite.
//   alerta ativo            -> vermelho pulsando (respiração ~2s)
//   notificação na tela     -> pisca na cor do nível (info azul, ok verde,
//                              warn laranja, error vermelho)
//   atualização OTA         -> roxo
//   resto                   -> apagado
// Os pinos (4/16/17) são ligados ao contrário na CYD (acende com nível baixo):
// LED_ACTIVE_LOW inverte o PWM. Canais 1-3 do LEDC (o 0 é do backlight).
namespace statusled {

static const int CH_R = 1, CH_G = 2, CH_B = 3;
static uint32_t overrideColor_ = 0;      // cor forçada (teste pela API, OTA)
static unsigned long overrideUntilMs_ = 0;
static bool overrideForever_ = false;
static uint32_t lastWritten_ = 0xFFFFFFFF;

inline void writeRgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t key = (uint32_t)r << 16 | g << 8 | b;
    if (key == lastWritten_) return;
    lastWritten_ = key;
    auto duty = [](uint8_t v) -> uint32_t { return LED_ACTIVE_LOW ? 255 - v : v; };
    ledcWrite(CH_R, duty(r));
    ledcWrite(CH_G, duty(g));
    ledcWrite(CH_B, duty(b));
}

inline void begin() {
    const int pins[3] = {LED_R_PIN, LED_G_PIN, LED_B_PIN};
    const int chans[3] = {CH_R, CH_G, CH_B};
    for (int i = 0; i < 3; i++) {
        ledcSetup(chans[i], 5000, 8);
        ledcAttachPin(pins[i], chans[i]);
    }
    writeRgb(0, 0, 0);
}

// Cor 0xRRGGBB na intensidade configurada (o LED é forte; 100% ofusca no escuro)
inline void show(uint32_t rgb, float factor = 1.0f) {
    float k = settings::get().ledBright / 100.0f * factor;
    writeRgb((uint8_t)(((rgb >> 16) & 0xFF) * k), (uint8_t)(((rgb >> 8) & 0xFF) * k), (uint8_t)((rgb & 0xFF) * k));
}

// Cor fixa por um tempo (ms) ou até clearOverride() (ms = 0)
inline void setOverride(uint32_t rgb, unsigned long ms) {
    overrideColor_ = rgb;
    overrideForever_ = ms == 0;
    overrideUntilMs_ = millis() + ms;
}

inline void clearOverride() {
    overrideForever_ = false;
    overrideUntilMs_ = 0;
}

inline uint32_t ledColor(notify::Level l) {
    switch (l) {
        case notify::OK: return 0x00FF00;
        case notify::WARN: return 0xFF6000;
        case notify::ERROR: return 0xFF0000;
        default: return 0x0040FF;
    }
}

// Chamado no loop() (~20ms)
inline void update() {
    unsigned long now = millis();
    if (overrideForever_ || (long)(overrideUntilMs_ - now) > 0) {
        show(overrideColor_);
        return;
    }
    if (!settings::get().ledOn) {
        writeRgb(0, 0, 0);
        return;
    }
    if (alerts::anyActive()) {
        // Respiração: 15% -> 100% -> 15% a cada 2s
        float phase = (now % 2000) / 2000.0f;
        show(0xFF0000, 0.15f + 0.85f * (0.5f - 0.5f * cosf(phase * 2 * (float)M_PI)));
        return;
    }
    if (notify::showing()) {
        bool on = (now / 400) % 2 == 0;  // pisca a 1,25 Hz
        if (on) show(ledColor(notify::currentLevel()));
        else writeRgb(0, 0, 0);
        return;
    }
    writeRgb(0, 0, 0);
}

} // namespace statusled
