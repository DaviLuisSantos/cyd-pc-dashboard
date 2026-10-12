#pragma once
#include "screens.h"
#include "i18n.h"
#include <time.h>

class ClockScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_CLOCK; }
    uint16_t accentColor() override { return THEME_CYAN; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconClock(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastMinute_ = -1;
        lastDay_ = -1;
        lastSecond_ = -1;
        lastBlinkOn_ = -1;
        tft.drawFastHLine(60, 160, 200, THEME_CURRENT);
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
            drawTime(tft, t, blinkOn);
        }

        // Data só quando o dia muda
        if (t.tm_mday != lastDay_) {
            lastDay_ = t.tm_mday;
            char buf[32];
            i18n::formatDate(buf, sizeof(buf), t);
            theme::beginLabelFont(tft);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(THEME_CYAN, THEME_BG, true);
            tft.setTextPadding(240);
            tft.drawString(buf, 160, 190, 4);
            tft.setTextPadding(0);
            theme::endLabelFont(tft);
        }

        // Barrinha discreta mostrando o progresso dos segundos no minuto
        if (t.tm_sec != lastSecond_) {
            lastSecond_ = t.tm_sec;
            theme::drawBar(tft, 80, 222, 160, 6, (t.tm_sec / 59.0f) * 100.0f, THEME_CYAN);
        }
    }

private:
    // Hora num sprite só (push atômico, sem flicker da fonte gigante). Horas,
    // ":" e minutos são desenhados separados em posições fixas — o ":" apagado
    // no pisca não desloca os dígitos. No modo 12h vai "AM"/"PM" à direita,
    // alinhado pela base dos dígitos.
    void drawTime(TFT_eSPI& tft, const struct tm& t, bool colonOn) {
        static const int X = 10, Y = 60, W = 300, H = 80;
        const bool h12 = settings::get().clockFmt == 12;
        char hh[3], mm[3];
        int hour = t.tm_hour;
        if (h12) hour = (hour % 12 == 0) ? 12 : hour % 12;
        snprintf(hh, sizeof(hh), h12 ? "%d" : "%02d", hour);
        snprintf(mm, sizeof(mm), "%02d", t.tm_min);
        const char* ampm = t.tm_hour < 12 ? "AM" : "PM";

        TFT_eSprite& s = theme::canvas().begin(tft, W, H, THEME_BG);
        const int hW = s.textWidth(hh, 8), cW = s.textWidth(":", 8), mW = s.textWidth(mm, 8);
        const int gap = 8, aW = h12 ? s.textWidth(ampm, 4) : 0;
        const int total = hW + cW + mW + (h12 ? gap + aW : 0);
        int x = (W - total) / 2;
        const int cy = H / 2;

        s.setTextColor(THEME_FG, THEME_BG);
        s.setTextDatum(ML_DATUM);
        s.drawString(hh, x, cy, 8);
        if (colonOn) s.drawString(":", x + hW, cy, 8);
        s.drawString(mm, x + hW + cW, cy, 8);
        if (h12) {
            // Base dos dígitos da font 8 (75px de altura) fica perto de cy + 37
            s.setTextColor(THEME_CYAN, THEME_BG);
            s.setTextDatum(BL_DATUM);
            s.drawString(ampm, x + hW + cW + mW + gap, cy + 34, 4);
        }
        theme::canvas().push(X, Y);
    }

    int lastMinute_ = -1;
    int lastDay_ = -1;
    int lastSecond_ = -1;
    int lastBlinkOn_ = -1;
};
