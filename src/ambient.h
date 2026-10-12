#pragma once
#include <Arduino.h>
#include "config.h"

// Sensor de luz ambiente (LDR) da CYD, no GPIO34 (só entrada, ADC1 — funciona
// com o WiFi ligado). Lido a cada AMBIENT_SAMPLE_MS com média móvel, pra uma
// mão passando na frente não fazer o brilho pular.
//
// A leitura crua depende da unidade e de onde a placa está; LDR_DARK_RAW e
// LDR_BRIGHT_RAW (config.h) dizem o que é "escuro" e "claro" — a ordem entre os
// dois pode ser qualquer uma. /api/status mostra a leitura crua pra calibrar.
namespace ambient {

static float avg_ = -1;
static unsigned long lastSampleMs_ = 0;

inline void begin() {
    pinMode(LDR_PIN, INPUT);
    // 0 dB: faixa de ~0-1,1 V. O divisor do LDR na CYD fica nessa região, então
    // a atenuação padrão (11 dB, até 3,3 V) desperdiçaria quase toda a resolução.
    analogSetPinAttenuation(LDR_PIN, ADC_0db);
}

inline int readRaw() { return analogRead(LDR_PIN); }

// Chamado no loop()
inline void poll() {
    if (millis() - lastSampleMs_ < AMBIENT_SAMPLE_MS) return;
    lastSampleMs_ = millis();
    int raw = readRaw();
    avg_ = avg_ < 0 ? raw : avg_ * 0.85f + raw * 0.15f;
}

inline int raw() { return avg_ < 0 ? readRaw() : (int)avg_; }

// 0 = escuro (LDR_DARK_RAW), 1 = claro (LDR_BRIGHT_RAW), limitado nas pontas
inline float level() {
    float span = (float)LDR_BRIGHT_RAW - (float)LDR_DARK_RAW;
    if (span == 0) return 1;
    float x = (raw() - (float)LDR_DARK_RAW) / span;
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

} // namespace ambient
