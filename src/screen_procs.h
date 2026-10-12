#pragma once
#include "screens.h"
#include "agent_client.h"
#include <string.h>

// Processos — top 4 processos do PC por uso de CPU (o agente agrega instâncias
// do mesmo nome, ex.: todos os chrome viram uma linha só).
class ProcsScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_PROCS; }
    uint16_t accentColor() override { return DRACULA_PINK; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconList(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastPoll_ = 0;
        drawn_ = false;
        for (int i = 0; i < N_ROWS; i++) {
            rows_[i].name[0] = '\0';
            rows_[i].cpu = -1;
            rows_[i].memMb = -1;
        }
        for (int i = 0; i < N_ROWS; i++) {
            int y = rowY(i);
            theme::drawCard(tft, 8, y, 304, CARD_H);
            // Badge de posição (1º ao 4º), estático
            tft.fillSmoothCircle(28, y + CARD_H / 2, 9, DRACULA_PINK, DRACULA_CARD);
            char rank[2] = { (char)('1' + i), '\0' };
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(DRACULA_BG, DRACULA_PINK);
            tft.drawString(rank, 28, y + CARD_H / 2 + 1, 2);
        }
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ < PROCS_POLL_MS && drawn_) return;
        lastPoll_ = now;
        poll(tft);
    }

private:
    static const int N_ROWS = 4;
    static const int CARD_H = 40;

    struct Row {
        char name[24];
        float cpu = -1;
        int memMb = -1;
        Row() { name[0] = '\0'; }
    };

    int rowY(int i) { return 38 + i * 45; }

    void drawRow(TFT_eSPI& tft, int i, const char* pname, float cpu, int memMb) {
        Row& r = rows_[i];
        bool changed = strcmp(pname, r.name) != 0 ||
                       (int)(cpu + 0.5f) != (int)(r.cpu + 0.5f) ||
                       memMb != r.memMb;
        if (!changed) return;
        strncpy(r.name, pname, sizeof(r.name) - 1);
        r.name[sizeof(r.name) - 1] = '\0';
        r.cpu = cpu;
        r.memMb = memMb;

        int cy = rowY(i) + CARD_H / 2;
        theme::drawValue(tft, 46, cy - 8, 130, 16, r.name, 2,
                         DRACULA_FG, DRACULA_CARD, ML_DATUM);
        char buf[16];
        if (memMb >= 0) {
            snprintf(buf, sizeof(buf), "%d MB", memMb);
        } else {
            buf[0] = '\0';
        }
        theme::drawValue(tft, 180, cy - 8, 64, 16, buf, 2,
                         DRACULA_COMMENT, DRACULA_CARD, MR_DATUM);
        if (cpu >= 0) {
            snprintf(buf, sizeof(buf), "%d%%", (int)(cpu + 0.5f));
        } else {
            buf[0] = '\0';
        }
        theme::drawValue(tft, 250, cy - 11, 54, 22, buf, 4,
                         DRACULA_FG, DRACULA_CARD, MR_DATUM);
    }

    void poll(TFT_eSPI& tft) {
        JsonDocument doc;
        if (!agent::fetch(tft, "/procs", doc)) return;

        JsonArray procs = doc["procs"].as<JsonArray>();
        for (int i = 0; i < N_ROWS; i++) {
            if (i < (int)procs.size()) {
                JsonObject p = procs[i];
                drawRow(tft, i, p["name"] | "?", p["cpu"] | 0.0f, p["mem_mb"] | 0);
            } else {
                drawRow(tft, i, "", -1, -1);
            }
        }

        agent::showOk(tft);
        drawn_ = true;
    }

    Row rows_[N_ROWS];
    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
};
