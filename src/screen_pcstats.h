#pragma once
#include "screens.h"
#include "agent_client.h"
#include <math.h>

// PC Stats — layout misto: anéis (gauges) pra CPU e RAM, sparkline com o
// histórico da CPU, card com barra pro disco e rodapé com rede + status.
class PcStatsScreen : public Screen {
public:
    const char* name() override { return i18n::NAME_PC; }
    uint16_t accentColor() override { return DRACULA_GREEN; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconCpu(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastPoll_ = 0;
        drawn_ = false;
        dispCpu_ = dispRam_ = dispDisk_ = 0;
        targetCpu_ = targetRam_ = targetDisk_ = 0;
        lastDrawnCpu_ = lastDrawnRam_ = lastDrawnDisk_ = -1;
        netDown_ = netUp_ = -1;

        // Card do disco + labels estáticos (uma vez só)
        theme::drawCard(tft, 12, CARD_Y, 296, CARD_H);
        theme::iconDisk(tft, 32, CARD_CY, 10, DRACULA_COMMENT);
        theme::iconArrowDown(tft, 24, NET_CY, 5, DRACULA_CYAN);
        theme::iconArrowUp(tft, 178, NET_CY, 5, DRACULA_PURPLE);

        theme::beginLabelFont(tft);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_COMMENT, DRACULA_CARD);
        tft.drawString(i18n::LBL_DISK, 46, CARD_CY);
        tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
        tft.drawString("Mb/s", 114, NET_CY);
        tft.drawString("Mb/s", 268, NET_CY);
        theme::endLabelFont(tft);

        theme::drawSparkline(tft, SPARK_X, SPARK_Y, SPARK_W, SPARK_H,
                             hist_, histCount_, DRACULA_GREEN);
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ >= AGENT_POLL_MS || !drawn_) {
            lastPoll_ = now;
            poll(tft);
        }
        // Anima anéis e barra a cada tick, independente do poll (easing)
        animateRing(tft, RING_CPU_CX, dispCpu_, targetCpu_, lastDrawnCpu_, DRACULA_GREEN, "CPU");
        animateRing(tft, RING_RAM_CX, dispRam_, targetRam_, lastDrawnRam_, DRACULA_CYAN, "RAM");
        animateDiskBar(tft);
    }

private:
    // Anéis CPU/RAM (topo do anel em y=52)
    static const int RING_CY = 90;
    static const int RING_CPU_CX = 88;
    static const int RING_RAM_CX = 232;
    static const int RING_R = 36;
    static const int RING_TH = 9;
    // Sparkline do histórico de CPU (~2 min a cada 2s de poll)
    static const int SPARK_X = 20, SPARK_Y = 136, SPARK_W = 280, SPARK_H = 16;
    static const int SPARK_N = 56;
    // Card do disco (158..194)
    static const int CARD_Y = 158;
    static const int CARD_H = 36;
    static const int CARD_CY = CARD_Y + CARD_H / 2;
    // Rodapé: rede (região 198..218, com folga do card) + status (221..240)
    static const int NET_CY = 208;

    uint16_t loadColor(float pct, uint16_t base) {
        if (pct >= 90) return DRACULA_RED;
        if (pct >= 70) return DRACULA_ORANGE;
        return base;
    }

    void animateRing(TFT_eSPI& tft, int cx, float& disp, float target,
                     int& lastDrawn, uint16_t base, const char* label) {
        disp += (target - disp) * 0.3f;
        if (fabsf(disp - target) < 0.1f) disp = target;
        int ip = (int)(disp + 0.5f);
        if (ip == lastDrawn) return;
        lastDrawn = ip;
        theme::drawRingGauge(tft, cx, RING_CY, RING_R, RING_TH, disp,
                             loadColor(disp, base), label);
    }

    void animateDiskBar(TFT_eSPI& tft) {
        dispDisk_ += (targetDisk_ - dispDisk_) * 0.3f;
        if (fabsf(dispDisk_ - targetDisk_) < 0.1f) dispDisk_ = targetDisk_;
        int ip = (int)(dispDisk_ + 0.5f);
        if (ip == lastDrawnDisk_) return;
        lastDrawnDisk_ = ip;

        theme::drawBar(tft, 108, CARD_CY - 5, 134, 10, dispDisk_,
                       loadColor(dispDisk_, DRACULA_GREEN), DRACULA_CARD);
        char buf[6];
        snprintf(buf, sizeof(buf), "%d%%", ip);
        theme::drawValue(tft, 250, CARD_CY - 11, 52, 22, buf, 4,
                         DRACULA_FG, DRACULA_CARD, MR_DATUM);
    }

    void pushHistory(float pct) {
        if (histCount_ < SPARK_N) {
            hist_[histCount_++] = pct;
        } else {
            memmove(hist_, hist_ + 1, (SPARK_N - 1) * sizeof(float));
            hist_[SPARK_N - 1] = pct;
        }
    }

    void poll(TFT_eSPI& tft) {
        JsonDocument doc;
        if (!agent::fetch(tft, "/stats", doc)) return;

        targetCpu_  = doc["cpu"]  | 0.0f;
        targetRam_  = doc["ram"]  | 0.0f;
        targetDisk_ = doc["disk"] | 0.0f;
        float dl = doc["net_down_mbps"] | 0.0f;
        float up = doc["net_up_mbps"]   | 0.0f;
        drawNet(tft, dl, up);

        pushHistory(targetCpu_);
        theme::drawSparkline(tft, SPARK_X, SPARK_Y, SPARK_W, SPARK_H,
                             hist_, histCount_, DRACULA_GREEN);

        agent::showOk(tft);
        drawn_ = true;
    }

    void drawNet(TFT_eSPI& tft, float dl, float up) {
        if (dl == netDown_ && up == netUp_) return;
        netDown_ = dl;
        netUp_ = up;

        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f", dl);
        theme::drawValue(tft, 36, NET_CY - 12, 74, 24, buf, 4,
                         DRACULA_FG, DRACULA_BG, ML_DATUM);
        snprintf(buf, sizeof(buf), "%.1f", up);
        theme::drawValue(tft, 190, NET_CY - 12, 74, 24, buf, 4,
                         DRACULA_FG, DRACULA_BG, ML_DATUM);
    }

    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
    float dispCpu_ = 0, dispRam_ = 0, dispDisk_ = 0;
    float targetCpu_ = 0, targetRam_ = 0, targetDisk_ = 0;
    int lastDrawnCpu_ = -1, lastDrawnRam_ = -1, lastDrawnDisk_ = -1;
    float netDown_ = -1, netUp_ = -1;
    float hist_[SPARK_N];
    int histCount_ = 0;
};
