#pragma once
#include "screens.h"
#include "agent_client.h"
#include <math.h>
#include <string.h>

// Cotações — um card por ativo, preço com easing ("rolando" até o valor novo,
// efeito de painel de bolsa) e badge colorido com a variação percentual.
// Os ativos, a moeda base e o prefixo de preço vêm do agente (seção "quotes"
// do agent/config.json) — o firmware só mostra os N_ROWS primeiros.
class QuotesScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_QUOTES; }
    uint16_t accentColor() override { return DRACULA_ORANGE; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconCoin(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastPoll_ = 0;
        drawn_ = false;
        for (int i = 0; i < N_ROWS; i++) {
            // Ao voltar pra aba, mostra direto o último valor conhecido
            // (sem rolar do zero de novo); força o redesenho zerando o cache.
            rows_[i].disp = rows_[i].target;
            rows_[i].lastStr[0] = '\0';
            rows_[i].pctDrawn = false;
        }
        if (nRows_ > 0) drawLayout(tft);
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ >= QUOTES_POLL_MS || !drawn_) {
            lastPoll_ = now;
            poll(tft);
        }
        // Easing dos preços a cada tick, independente do poll
        for (int i = 0; i < nRows_; i++) animateRow(tft, i);
    }

private:
    static const int N_ROWS = 4;
    static const int CARD_H = 42;

    struct Row {
        char ticker[8] = "";
        float target = 0, disp = 0, pct = 0;
        char lastStr[20] = "";
        bool hasData = false;
        bool pctDrawn = false;
    };

    // Cor de identidade de cada linha, por posição
    static uint16_t rowColor(int i) {
        static const uint16_t colors[N_ROWS] = {
            DRACULA_GREEN, DRACULA_CYAN, DRACULA_ORANGE, DRACULA_PURPLE};
        return colors[i % N_ROWS];
    }

    // 4º card termina em y=216 — folga de 5px pro status (221..240)
    int rowY(int i) { return 36 + i * 46; }

    void drawLayout(TFT_eSPI& tft) {
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H, DRACULA_BG);
        theme::beginLabelFont(tft);
        for (int i = 0; i < nRows_; i++) {
            int y = rowY(i);
            theme::drawCard(tft, 8, y, 304, CARD_H);
            theme::iconCoin(tft, 30, y + CARD_H / 2, 11, rowColor(i));
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(DRACULA_COMMENT, DRACULA_CARD);
            tft.drawString(rows_[i].ticker, 48, y + CARD_H / 2);
        }
        theme::endLabelFont(tft);
        for (int i = 0; i < nRows_; i++) {
            rows_[i].lastStr[0] = '\0';
            rows_[i].pctDrawn = false;
        }
    }

    void animateRow(TFT_eSPI& tft, int i) {
        Row& r = rows_[i];
        if (!r.hasData) return;

        r.disp += (r.target - r.disp) * 0.25f;
        // Converge de vez quando chega perto (proporcional à escala do ativo)
        if (fabsf(r.disp - r.target) < fmaxf(0.00005f, r.target * 0.0001f)) {
            r.disp = r.target;
        }

        char buf[20];
        formatPrice(buf, sizeof(buf), r.disp);
        if (strcmp(buf, r.lastStr) != 0) {
            strncpy(r.lastStr, buf, sizeof(r.lastStr) - 1);
            r.lastStr[sizeof(r.lastStr) - 1] = '\0';
            theme::drawValue(tft, 92, rowY(i) + CARD_H / 2 - 12, 128, 24,
                             buf, 4, DRACULA_FG, DRACULA_CARD, ML_DATUM);
        }
        if (!r.pctDrawn) {
            r.pctDrawn = true;
            theme::drawPctBadge(tft, 298, rowY(i) + CARD_H / 2, r.pct, DRACULA_CARD);
        }
    }

    // Casas decimais pela ordem de grandeza: BTC sem centavos, dólar com 2,
    // altcoins baratas com 4.
    void formatPrice(char* buf, size_t len, float price) {
        if (price >= 1000.0f) {
            snprintf(buf, len, "%s%.0f", prefix_, price);
        } else if (price >= 1.0f) {
            snprintf(buf, len, "%s%.2f", prefix_, price);
        } else {
            snprintf(buf, len, "%s%.4f", prefix_, price);
        }
    }

    void setRow(int i, float price, float pct) {
        Row& r = rows_[i];
        r.target = price;
        if (fabsf(pct - r.pct) >= 0.005f || !r.hasData) {
            r.pct = pct;
            r.pctDrawn = false;  // badge só redesenha quando a variação muda
        }
        r.hasData = true;
    }

    void poll(TFT_eSPI& tft) {
        JsonDocument doc;
        if (!agent::fetch(tft, "/quotes", doc)) return;

        const char* prefix = doc["prefix"] | "";
        strncpy(prefix_, prefix, sizeof(prefix_) - 1);
        prefix_[sizeof(prefix_) - 1] = '\0';

        JsonArray items = doc["items"].as<JsonArray>();
        int n = (int)items.size() < N_ROWS ? (int)items.size() : N_ROWS;

        // Redesenha o layout só se a lista de ativos mudou (quantidade ou tickers)
        bool layoutChanged = (n != nRows_);
        for (int i = 0; i < n && !layoutChanged; i++) {
            layoutChanged = strcmp(items[i]["symbol"] | "?", rows_[i].ticker) != 0;
        }
        if (layoutChanged) {
            nRows_ = n;
            for (int i = 0; i < N_ROWS; i++) rows_[i] = Row();
            for (int i = 0; i < n; i++) {
                strncpy(rows_[i].ticker, items[i]["symbol"] | "?", sizeof(rows_[i].ticker) - 1);
            }
            drawLayout(tft);
        }

        for (int i = 0; i < n; i++) {
            JsonObject it = items[i];
            if (it["price"].isNull()) continue;  // agente ainda não buscou esse ativo
            setRow(i, it["price"] | 0.0f, it["change_pct"] | 0.0f);
        }

        if (n == 0) {
            agent::showStatus(tft, DRACULA_ORANGE, i18n::tr().ST_NO_DATA);
        } else {
            agent::showOk(tft);
        }
        drawn_ = true;
    }

    Row rows_[N_ROWS];
    int nRows_ = 0;
    char prefix_[6] = "";
    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
};
