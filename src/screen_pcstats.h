#pragma once
#include "screens.h"
#include "agent_client.h"
#include "alerts.h"
#include <math.h>

// PC Stats — layout misto: anéis (gauges) pra CPU e RAM, sparkline com o
// histórico da CPU, card com barra pro disco e rodapé com rede + status.
// Com vários discos (/stats "disks"), o card mostra o nome do disco e alterna
// entre eles a cada DISK_CYCLE_MS; tocar no card pula pro próximo.
// Com temperatura da CPU (/stats "cpu_temp"), ela aparece à direita do
// sparkline, que encolhe pra dar espaço; sem ela, o sparkline usa a largura toda.
class PcStatsScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_PC; }
    uint16_t accentColor() override { return THEME_GREEN; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconCpu(tft, cx, cy, 8, color);
    }

    bool alerting() override { return alerts::anyActive(); }

    void onTouch(TFT_eSPI& tft, int x, int y) override {
        if (diskCount_ > 1 && y >= CARD_Y && y < CARD_Y + CARD_H) nextDisk(tft);
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
        theme::iconDisk(tft, 32, CARD_CY, 10, THEME_COMMENT);
        theme::iconArrowDown(tft, 24, NET_CY, 5, THEME_CYAN);
        theme::iconArrowUp(tft, 178, NET_CY, 5, THEME_PURPLE);

        drawDiskLabel(tft);

        theme::beginLabelFont(tft);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(THEME_COMMENT, THEME_BG);
        tft.drawString("Mb/s", 114, NET_CY);
        tft.drawString("Mb/s", 268, NET_CY);
        theme::endLabelFont(tft);

        lastDrawnTemp_ = INT16_MIN;
        drawSpark(tft);
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ >= AGENT_POLL_MS || !drawn_) {
            lastPoll_ = now;
            poll(tft);
        }
        if (diskCount_ > 1 && now - lastCycleMs_ >= DISK_CYCLE_MS) nextDisk(tft);
        // Anima anéis e barra a cada tick, independente do poll (easing)
        animateRing(tft, RING_CPU_CX, dispCpu_, targetCpu_, lastDrawnCpu_, alerts::CPU, THEME_GREEN, "CPU");
        animateRing(tft, RING_RAM_CX, dispRam_, targetRam_, lastDrawnRam_, alerts::RAM, THEME_CYAN, "RAM");
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
    static const int TEMP_W = 44;  // "100°C" na font 2
    // Card do disco (158..194)
    static const int CARD_Y = 158;
    static const int CARD_H = 36;
    static const int CARD_CY = CARD_Y + CARD_H / 2;
    // Rodapé: rede (região 198..218, com folga do card) + status (221..240)
    static const int NET_CY = 208;

    void animateRing(TFT_eSPI& tft, int cx, float& disp, float target,
                     int& lastDrawn, alerts::Metric metric, uint16_t base, const char* label) {
        disp += (target - disp) * 0.3f;
        if (fabsf(disp - target) < 0.1f) disp = target;
        int ip = (int)(disp + 0.5f);
        if (ip == lastDrawn) return;
        lastDrawn = ip;
        theme::drawRingGauge(tft, cx, RING_CY, RING_R, RING_TH, disp,
                             alerts::color(metric, disp, base), label);
    }

    void animateDiskBar(TFT_eSPI& tft) {
        dispDisk_ += (targetDisk_ - dispDisk_) * 0.3f;
        if (fabsf(dispDisk_ - targetDisk_) < 0.1f) dispDisk_ = targetDisk_;
        int ip = (int)(dispDisk_ + 0.5f);
        if (ip == lastDrawnDisk_) return;
        lastDrawnDisk_ = ip;

        theme::drawBar(tft, 108, CARD_CY - 5, 134, 10, dispDisk_,
                       alerts::color(alerts::DISK, dispDisk_, THEME_GREEN), THEME_CARD);
        char buf[6];
        snprintf(buf, sizeof(buf), "%d%%", ip);
        theme::drawValue(tft, 250, CARD_CY - 11, 52, 22, buf, 4,
                         THEME_FG, THEME_CARD, MR_DATUM);
    }

    // Rótulo do card: "DISK" com um disco só, o nome dele ("C:", "GAMES") com
    // vários. Sprite + smooth font: troca o texto sem flicker.
    void drawDiskLabel(TFT_eSPI& tft) {
        const char* label = diskCount_ > 1 ? disks_[diskIdx_].name : i18n::tr().LBL_DISK;
        TFT_eSprite& s = theme::canvas().begin(tft, 58, 20, THEME_CARD);
        s.loadFont(NotoSansBold15);
        s.setTextDatum(ML_DATUM);
        s.setTextColor(THEME_COMMENT, THEME_CARD);
        s.drawString(label, 0, 10);
        s.unloadFont();
        theme::canvas().push(46, CARD_CY - 10);
    }

    void nextDisk(TFT_eSPI& tft) {
        lastCycleMs_ = millis();
        if (diskCount_ < 2) return;
        diskIdx_ = (diskIdx_ + 1) % diskCount_;
        targetDisk_ = disks_[diskIdx_].pct;  // a barra anima até o valor novo
        drawDiskLabel(tft);
    }

    // Lê "disks" (agente novo) ou só "disk" (agente antigo)
    void readDisks(TFT_eSPI& tft, JsonDocument& doc) {
        int prevCount = diskCount_;
        JsonArray arr = doc["disks"].as<JsonArray>();
        diskCount_ = 0;
        if (!arr.isNull()) {
            for (JsonObject d : arr) {
                if (diskCount_ >= MAX_DISKS) break;
                strlcpy(disks_[diskCount_].name, d["name"] | "?", sizeof(disks_[0].name));
                disks_[diskCount_].pct = d["pct"] | 0.0f;
                diskCount_++;
            }
        }
        if (diskCount_ == 0) {
            disks_[0].name[0] = '\0';
            disks_[0].pct = doc["disk"] | 0.0f;
            diskCount_ = 1;
        }
        if (diskIdx_ >= diskCount_) diskIdx_ = 0;
        targetDisk_ = disks_[diskIdx_].pct;
        // Mudou de 1 pra vários discos (ou o contrário): troca "DISK" <-> nome
        if ((prevCount > 1) != (diskCount_ > 1) || prevCount == 0) drawDiskLabel(tft);
    }

    // Sparkline do histórico da CPU + temperatura à direita (se houver)
    void drawSpark(TFT_eSPI& tft) {
        int w = hasTemp_ ? SPARK_W - TEMP_W - 6 : SPARK_W;
        theme::drawSparkline(tft, SPARK_X, SPARK_Y, w, SPARK_H, hist_, histCount_, THEME_GREEN);
        if (hasTemp_) drawTemp(tft);
    }

    void readTemp(TFT_eSPI& tft, JsonDocument& doc) {
        bool has = !doc["cpu_temp"].isNull();
        if (has) temp_ = doc["cpu_temp"].as<float>();
        if (has != hasTemp_) {
            // Apareceu/sumiu a temperatura: muda a largura do sparkline
            hasTemp_ = has;
            tft.fillRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, THEME_BG);
            lastDrawnTemp_ = INT16_MIN;
        }
    }

    // "61°C" alinhado à direita, cor pelo limite de alerta. As fontes não têm
    // "°", então o grau é um circulozinho desenhado.
    void drawTemp(TFT_eSPI& tft) {
        int t = (int)lroundf(temp_);
        uint16_t color = alerts::color(alerts::TEMP, temp_, THEME_FG);
        int key = t * 4 + (color == THEME_RED ? 1 : color == THEME_ORANGE ? 2 : 0);
        if (key == lastDrawnTemp_) return;
        lastDrawnTemp_ = key;

        char num[6];
        snprintf(num, sizeof(num), "%d", t);
        TFT_eSprite& s = theme::canvas().begin(tft, TEMP_W, SPARK_H, THEME_BG);
        int wN = s.textWidth(num, 2), wC = s.textWidth("C", 2);
        int x = TEMP_W - (wN + 6 + wC);
        s.setTextColor(color, THEME_BG);
        s.setTextDatum(TL_DATUM);
        s.drawString(num, x, 0, 2);
        s.drawCircle(x + wN + 3, 3, 2, color);
        s.drawString("C", x + wN + 6, 0, 2);
        theme::canvas().push(SPARK_X + SPARK_W - TEMP_W, SPARK_Y);
    }

    float maxDiskPct() const {
        float m = 0;
        for (int i = 0; i < diskCount_; i++) m = max(m, disks_[i].pct);
        return m;
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
        readDisks(tft, doc);
        readTemp(tft, doc);
        alerts::feed(targetCpu_, targetRam_, maxDiskPct(), hasTemp_ ? temp_ : NAN);
        float dl = doc["net_down_mbps"] | 0.0f;
        float up = doc["net_up_mbps"]   | 0.0f;
        drawNet(tft, dl, up);

        pushHistory(targetCpu_);
        drawSpark(tft);

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
                         THEME_FG, THEME_BG, ML_DATUM);
        snprintf(buf, sizeof(buf), "%.1f", up);
        theme::drawValue(tft, 190, NET_CY - 12, 74, 24, buf, 4,
                         THEME_FG, THEME_BG, ML_DATUM);
    }

    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
    float dispCpu_ = 0, dispRam_ = 0, dispDisk_ = 0;
    float targetCpu_ = 0, targetRam_ = 0, targetDisk_ = 0;
    int lastDrawnCpu_ = -1, lastDrawnRam_ = -1, lastDrawnDisk_ = -1;
    float netDown_ = -1, netUp_ = -1;
    float hist_[SPARK_N];
    int histCount_ = 0;

    static const int MAX_DISKS = 8;
    struct Disk { char name[9]; float pct; };
    Disk disks_[MAX_DISKS] = {};
    int diskCount_ = 0;
    int diskIdx_ = 0;
    unsigned long lastCycleMs_ = 0;

    bool hasTemp_ = false;
    float temp_ = 0;
    int lastDrawnTemp_ = INT16_MIN;
};
