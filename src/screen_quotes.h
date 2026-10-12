#pragma once
#include "screens.h"
#include "agent_client.h"
#include <math.h>
#include <string.h>

// Cotações — um card por ativo, preço com easing ("rolando" até o valor novo,
// efeito de painel de bolsa) e badge colorido com a variação percentual. Atrás
// do preço, o histórico ("history" do /quotes: 24h pra moedas, o pregão do dia
// pra ações) como gráfico esmaecido, verde ou vermelho pela tendência — preço e
// gráfico no mesmo sprite. Cada linha pode ter prefixo próprio ("prefix" no
// item: ação em outra moeda, índice sem prefixo).
// Os ativos, a moeda base e o prefixo de preço vêm do agente (seção "quotes"
// do agent/config.json) — o firmware só mostra os N_ROWS primeiros.
class QuotesScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_QUOTES; }
    uint16_t accentColor() override { return THEME_ORANGE; }

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

    static const int HIST_N = 32;
    // Área do preço + gráfico: começa depois do ticker (no mínimo em x=88) e
    // termina antes do badge (que começa por volta de x=246)
    static const int PRICE_MIN_X = 88, PRICE_END = 238;

    struct Row {
        char ticker[8] = "";
        float target = 0, disp = 0, pct = 0;
        char lastStr[20] = "";
        bool hasData = false;
        bool pctDrawn = false;
        float hist[HIST_N] = {};
        int histN = 0;
        char prefix[8] = "";   // prefixo da linha ("prefix" do item)
        bool hasPrefix = false;
        int priceX = PRICE_MIN_X;  // depende da largura do ticker ("IVVB11" > "USD")
    };

    // Cor de identidade de cada linha, por posição
    static uint16_t rowColor(int i) {
        static const uint16_t colors[N_ROWS] = {
            THEME_GREEN, THEME_CYAN, THEME_ORANGE, THEME_PURPLE};
        return colors[i % N_ROWS];
    }

    // 4º card termina em y=216 — folga de 5px pro status (221..240)
    int rowY(int i) { return 36 + i * 46; }

    void drawLayout(TFT_eSPI& tft) {
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H, THEME_BG);
        theme::beginLabelFont(tft);
        for (int i = 0; i < nRows_; i++) {
            int y = rowY(i);
            theme::drawCard(tft, 8, y, 304, CARD_H);
            theme::iconCoin(tft, 30, y + CARD_H / 2, 11, rowColor(i));
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(THEME_COMMENT, THEME_CARD);
            tft.drawString(rows_[i].ticker, 48, y + CARD_H / 2);
            // Ticker comprido empurra o preço (senão o sprite do preço o corta)
            rows_[i].priceX = max(PRICE_MIN_X, 48 + (int)tft.textWidth(rows_[i].ticker) + 6);
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
        formatPrice(buf, sizeof(buf), r.disp, r.hasPrefix ? r.prefix : prefix_);
        if (strcmp(buf, r.lastStr) != 0) {
            strncpy(r.lastStr, buf, sizeof(r.lastStr) - 1);
            r.lastStr[sizeof(r.lastStr) - 1] = '\0';
            drawPrice(tft, i, buf);
        }
        if (!r.pctDrawn) {
            r.pctDrawn = true;
            theme::drawPctBadge(tft, 298, rowY(i) + CARD_H / 2, r.pct, THEME_CARD);
        }
    }

    // Gráfico de 24h esmaecido + preço por cima, num sprite só (sem flicker)
    void drawPrice(TFT_eSPI& tft, int i, const char* price) {
        const Row& r = rows_[i];
        const int h = CARD_H - 6;
        const int w = PRICE_END - r.priceX;
        TFT_eSprite& s = theme::canvas().begin(tft, w, h, THEME_CARD);
        if (r.histN >= 2) {
            float lo = r.hist[0], hi = r.hist[0];
            for (int k = 1; k < r.histN; k++) { lo = fminf(lo, r.hist[k]); hi = fmaxf(hi, r.hist[k]); }
            float range = hi - lo;
            bool up = r.hist[r.histN - 1] >= r.hist[0];
            // ~35% da cor sobre o fundo do card: aparece, mas não briga com o preço
            uint16_t color = tft.alphaBlend(90, up ? THEME_GREEN : THEME_RED, THEME_CARD);
            const int top = 4, bottom = h - 4;
            // Escala fixa de HIST_N janelas (24h): histórico parcial (agente
            // recém-iniciado, fiat esparso) fica alinhado à direita = horas recentes
            float step = (float)(w - 1) / (HIST_N - 1);
            const int offset = HIST_N - r.histN;
            int px = 0, py = 0;
            for (int k = 0; k < r.histN; k++) {
                // Variação mínima (moeda parada): linha reta no meio
                int y = range < 1e-9f ? (top + bottom) / 2
                      : bottom - (int)((r.hist[k] - lo) / range * (bottom - top));
                int x = (int)((offset + k) * step);
                if (k > 0) {
                    s.drawLine(px, py, x, y, color);
                    s.drawLine(px, py + 1, x, y + 1, color);  // 2px de espessura
                }
                px = x;
                py = y;
            }
        }
        // Texto com fundo transparente (setTextColor só com fg), por cima do gráfico
        s.setTextColor(THEME_FG);
        s.setTextDatum(ML_DATUM);
        s.drawString(price, 4, h / 2, 4);
        theme::canvas().push(r.priceX, rowY(i) + 3);
    }

    // Casas decimais pela ordem de grandeza: BTC sem centavos, dólar com 2,
    // altcoins baratas com 4.
    void formatPrice(char* buf, size_t len, float price, const char* prefix) {
        if (price >= 1000.0f) {
            snprintf(buf, len, "%s%.0f", prefix, price);
        } else if (price >= 1.0f) {
            snprintf(buf, len, "%s%.2f", prefix, price);
        } else {
            snprintf(buf, len, "%s%.4f", prefix, price);
        }
    }

    // Prefixo próprio da linha (ou o global, sem "prefix" no item)
    void readPrefix(int i, JsonObject it) {
        Row& r = rows_[i];
        bool has = !it["prefix"].isNull();
        const char* p = it["prefix"] | "";
        if (has == r.hasPrefix && (!has || !strcmp(p, r.prefix))) return;
        r.hasPrefix = has;
        strlcpy(r.prefix, p, sizeof(r.prefix));
        r.lastStr[0] = '\0';  // redesenha com o prefixo novo
    }

    // Histórico novo diferente do desenhado: força redesenhar o preço/gráfico
    void readHistory(int i, JsonArray arr) {
        Row& r = rows_[i];
        float h[HIST_N];
        int n = 0;
        for (JsonVariant v : arr) {
            if (n >= HIST_N) break;
            h[n++] = v.as<float>();
        }
        if (n == r.histN && memcmp(h, r.hist, n * sizeof(float)) == 0) return;
        memcpy(r.hist, h, n * sizeof(float));
        r.histN = n;
        r.lastStr[0] = '\0';
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
            readPrefix(i, it);
            readHistory(i, it["history"].as<JsonArray>());
        }

        if (n == 0) {
            agent::showStatus(tft, THEME_ORANGE, i18n::tr().ST_NO_DATA);
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
