#pragma once
#include "screens.h"
#include "i18n.h"
#include <time.h>

class ClockScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_CLOCK; }
    uint16_t accentColor() override { return DRACULA_CYAN; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconClock(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastMinute_ = -1;
        lastDay_ = -1;
        lastSecond_ = -1;
        lastBlinkOn_ = -1;
        tft.drawFastHLine(60, 160, 200, DRACULA_CURRENT);
    }

    void update(TFT_eSPI& tft) override {
        struct tm t;
        if (!getLocalTime(&t, 50)) return;

        bool blinkOn = (millis() / 500) % 2 == 0;

        // Redesenha a hora quando o minuto muda ou o ":" pisca (via sprite,
        // push atômico — sem o flicker do redesenho direto da fonte gigante)
        if (t.tm_min != lastMinute_ || (int)blinkOn != lastBlinkOn_) {
            lastMinute_ = t.tm_min;
            lastBlinkOn_ = blinkOn;
            char buf[6];
            snprintf(buf, sizeof(buf), "%02d%c%02d", t.tm_hour, blinkOn ? ':' : ' ', t.tm_min);
            // 280px de largura: "88:88" na font 8 tem ~250px — com 220 o
            // último dígito era clipado pelo sprite
            theme::drawValue(tft, 20, 60, 280, 80, buf, 8, DRACULA_FG, DRACULA_BG, MC_DATUM);
        }

        // Data só quando o dia muda
        if (t.tm_mday != lastDay_) {
            lastDay_ = t.tm_mday;
            char buf[32];
            i18n::formatDate(buf, sizeof(buf), t);
            theme::beginLabelFont(tft);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(DRACULA_CYAN, DRACULA_BG, true);
            tft.setTextPadding(240);
            tft.drawString(buf, 160, 190, 4);
            tft.setTextPadding(0);
            theme::endLabelFont(tft);
        }

        // Barrinha discreta mostrando o progresso dos segundos no minuto
        if (t.tm_sec != lastSecond_) {
            lastSecond_ = t.tm_sec;
            theme::drawBar(tft, 80, 222, 160, 6, (t.tm_sec / 59.0f) * 100.0f, DRACULA_CYAN);
        }
    }

private:
    int lastMinute_ = -1;
    int lastDay_ = -1;
    int lastSecond_ = -1;
    int lastBlinkOn_ = -1;
};
