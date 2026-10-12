#pragma once
#include "screens.h"
#include "agent_client.h"
#include "services.h"
#include <string.h>

// Serviços — os hosts/URLs de "services" no agent/config.json, verificados pelo
// agente: bolinha verde (no ar) / vermelha (fora) / cinza (ainda não sabe), o
// nome e, à direita, a latência ou há quanto tempo está fora. Os dados vêm de
// services.h (consultado em segundo plano); a tela só redesenha quando chega
// resposta nova. Com algum serviço fora, o ícone da aba pisca (alerting()).
class ServicesScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_SERVICES; }
    uint16_t accentColor() override { return THEME_PURPLE; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconServer(tft, cx, cy, 8, color);
    }

    bool alerting() override { return services::anyDown(); }

    void onEnter(TFT_eSPI& tft) override {
        drawnVersion_ = 0xFFFFFFFF;
        layoutCount_ = -1;
        layoutNames_[0] = '\0';
        hintDrawn_ = false;
        services::requestNow();  // dado fresco ao abrir a aba
    }

    void update(TFT_eSPI& tft) override {
        if (services::version() == drawnVersion_) return;
        drawnVersion_ = services::version();

        if (services::lastResult() == agent::OK && !services::configured()) {
            drawNotConfigured(tft);
        } else {
            hintDrawn_ = false;
            drawLayoutIfChanged(tft);
            for (int i = 0; i < services::count(); i++) drawRow(tft, i);
        }
        agent::showResult(tft, services::lastResult());
    }

private:
    static const int CARD_H = 26;
    // 6º card termina em y=214 — folga pro status (221..240)
    int rowY(int i) { return 38 + i * 30; }

    void drawNotConfigured(TFT_eSPI& tft) {
        if (hintDrawn_) return;
        hintDrawn_ = true;
        layoutCount_ = -1;
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
        theme::iconServer(tft, 160, 100, 16, THEME_COMMENT);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(THEME_COMMENT, THEME_BG);
        tft.drawString(i18n::tr().SVC_HINT_1, 160, 145, 2);
        tft.drawString(i18n::tr().SVC_HINT_2, 160, 165, 2);
    }

    // Cards + nomes (estáticos, smooth font) só quando a lista muda
    void drawLayoutIfChanged(TFT_eSPI& tft) {
        char names[sizeof(layoutNames_)] = "";
        for (int i = 0; i < services::count(); i++) {
            strlcat(names, services::at(i).name, sizeof(names));
            strlcat(names, "|", sizeof(names));
        }
        if (services::count() == layoutCount_ && !strcmp(names, layoutNames_)) return;
        layoutCount_ = services::count();
        strlcpy(layoutNames_, names, sizeof(layoutNames_));

        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
        for (int i = 0; i < layoutCount_; i++) theme::drawCard(tft, 8, rowY(i), 304, CARD_H);
        theme::beginLabelFont(tft);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(THEME_FG, THEME_CARD);
        for (int i = 0; i < layoutCount_; i++) {
            tft.drawString(services::at(i).name, 38, rowY(i) + CARD_H / 2);
        }
        theme::endLabelFont(tft);
    }

    // 45s / 12m / 3h / 2d
    static void fmtDuration(char* buf, size_t len, long s) {
        if (s < 60) snprintf(buf, len, "%lds", s);
        else if (s < 3600) snprintf(buf, len, "%ldm", s / 60);
        else if (s < 86400) snprintf(buf, len, "%ldh", s / 3600);
        else snprintf(buf, len, "%ldd", s / 86400);
    }

    void drawRow(TFT_eSPI& tft, int i) {
        const services::Svc& v = services::at(i);
        const int y = rowY(i);
        uint16_t color = v.ok == 1 ? THEME_GREEN : v.ok == 0 ? THEME_RED : THEME_COMMENT;

        // Bolinha de estado
        TFT_eSprite& s = theme::canvas().begin(tft, 14, 14, THEME_CARD);
        s.fillSmoothCircle(7, 7, 5, color, THEME_CARD);
        theme::canvas().push(17, y + CARD_H / 2 - 7);

        // Direita: latência (no ar), motivo + tempo fora (fora), "..." (sem dado)
        char right[32];
        if (v.ok == 1) {
            snprintf(right, sizeof(right), "%d ms", v.ms);
        } else if (v.ok == 0) {
            char dur[8];
            fmtDuration(dur, sizeof(dur), v.downForS);
            char why[12];
            strlcpy(why, v.detail[0] ? v.detail : i18n::tr().SVC_DOWN, sizeof(why));
            snprintf(right, sizeof(right), "%s %s", why, dur);
        } else {
            strlcpy(right, "...", sizeof(right));
        }
        theme::drawValue(tft, 196, y + CARD_H / 2 - 8, 108, 16, right, 2,
                         v.ok == 0 ? THEME_RED : THEME_COMMENT, THEME_CARD, MR_DATUM);
    }

    uint32_t drawnVersion_ = 0xFFFFFFFF;
    int layoutCount_ = -1;
    char layoutNames_[services::MAX * 23] = "";
    bool hintDrawn_ = false;
};
