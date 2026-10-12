#pragma once
#include "screens.h"
#include "agent_client.h"
#include <string.h>

// Rede/Sistema — latência (ping TCP medido pelo agente), uptime do PC,
// IP público e histórico de download com sparkline.
class SysInfoScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_SYS; }
    uint16_t accentColor() override { return THEME_PURPLE; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconNet(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastPoll_ = 0;
        drawn_ = false;
        lastPing_ = -999;
        lastUptimeH_ = -1;
        lastIp_[0] = '\0';
        netDown_ = netUp_ = -1;

        theme::drawCard(tft, 8, TOP_Y, 148, TOP_H);
        theme::drawCard(tft, 164, TOP_Y, 148, TOP_H);
        theme::drawCard(tft, 8, IP_Y, 304, IP_H);
        theme::iconArrowDown(tft, 24, NET_CY, 5, THEME_CYAN);
        theme::iconArrowUp(tft, 178, NET_CY, 5, THEME_PURPLE);

        theme::beginLabelFont(tft);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(THEME_COMMENT, THEME_CARD);
        tft.drawString(i18n::tr().LBL_PING, 20, TOP_Y + 13);
        tft.drawString(i18n::tr().LBL_UPTIME, 176, TOP_Y + 13);
        tft.drawString(i18n::tr().LBL_IP, 20, IP_Y + IP_H / 2);
        tft.setTextColor(THEME_COMMENT, THEME_BG);
        tft.drawString(i18n::tr().LBL_DL_HISTORY, 20, SPARK_Y - 9);
        tft.drawString("Mb/s", 114, NET_CY);
        tft.drawString("Mb/s", 268, NET_CY);
        theme::endLabelFont(tft);

        drawSpark(tft);
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ < SYS_POLL_MS && drawn_) return;
        lastPoll_ = now;
        poll(tft);
    }

private:
    static const int TOP_Y = 40;
    static const int TOP_H = 48;
    static const int IP_Y = 96;
    static const int IP_H = 30;
    static const int SPARK_X = 20, SPARK_Y = 152, SPARK_W = 280, SPARK_H = 22;
    static const int SPARK_N = 56;
    static const int NET_CY = 200;

    uint16_t pingColor(float ms) {
        if (ms < 0) return THEME_RED;
        if (ms >= 100) return THEME_RED;
        if (ms >= 40) return THEME_ORANGE;
        return THEME_GREEN;
    }

    void drawSpark(TFT_eSPI& tft) {
        // drawSparkline espera 0-100 — normaliza pelo pico do buffer
        float peak = 1.0f;
        for (int i = 0; i < histCount_; i++) {
            if (hist_[i] > peak) peak = hist_[i];
        }
        float norm[SPARK_N];
        for (int i = 0; i < histCount_; i++) {
            norm[i] = hist_[i] / peak * 100.0f;
        }
        theme::drawSparkline(tft, SPARK_X, SPARK_Y, SPARK_W, SPARK_H,
                             norm, histCount_, THEME_CYAN);
    }

    void pushHistory(float mbps) {
        if (histCount_ < SPARK_N) {
            hist_[histCount_++] = mbps;
        } else {
            memmove(hist_, hist_ + 1, (SPARK_N - 1) * sizeof(float));
            hist_[SPARK_N - 1] = mbps;
        }
    }

    void poll(TFT_eSPI& tft) {
        JsonDocument doc;
        if (!agent::fetch(tft, "/sysinfo", doc)) return;

        char buf[24];

        float ping = doc["ping_ms"] | -1.0f;
        if ((int)ping != lastPing_) {
            lastPing_ = (int)ping;
            if (ping >= 0) {
                snprintf(buf, sizeof(buf), "%d ms", (int)(ping + 0.5f));
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            theme::drawValue(tft, 20, TOP_Y + 23, 124, 22, buf, 4,
                             pingColor(ping), THEME_CARD, ML_DATUM);
        }

        long uptime = doc["uptime_s"] | 0L;
        int uptimeH = (int)(uptime / 3600);
        if (uptimeH != lastUptimeH_) {
            lastUptimeH_ = uptimeH;
            if (uptime >= 86400) {
                snprintf(buf, sizeof(buf), "%ldd %ldh", uptime / 86400, (uptime % 86400) / 3600);
            } else {
                snprintf(buf, sizeof(buf), "%ldh %ldm", uptime / 3600, (uptime % 3600) / 60);
            }
            theme::drawValue(tft, 176, TOP_Y + 23, 124, 22, buf, 4,
                             THEME_FG, THEME_CARD, ML_DATUM);
        }

        const char* ip = doc["public_ip"] | "?";
        if (strcmp(ip, lastIp_) != 0) {
            strncpy(lastIp_, ip, sizeof(lastIp_) - 1);
            lastIp_[sizeof(lastIp_) - 1] = '\0';
            theme::drawValue(tft, 60, IP_Y + IP_H / 2 - 11, 240, 22, lastIp_, 4,
                             THEME_FG, THEME_CARD, MR_DATUM);
        }

        float dl = doc["net_down_mbps"] | 0.0f;
        float up = doc["net_up_mbps"] | 0.0f;
        pushHistory(dl);
        drawSpark(tft);
        if (dl != netDown_ || up != netUp_) {
            netDown_ = dl;
            netUp_ = up;
            snprintf(buf, sizeof(buf), "%.1f", dl);
            theme::drawValue(tft, 36, NET_CY - 12, 74, 24, buf, 4,
                             THEME_FG, THEME_BG, ML_DATUM);
            snprintf(buf, sizeof(buf), "%.1f", up);
            theme::drawValue(tft, 190, NET_CY - 12, 74, 24, buf, 4,
                             THEME_FG, THEME_BG, ML_DATUM);
        }

        agent::showOk(tft);
        drawn_ = true;
    }

    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
    int lastPing_ = -999;
    int lastUptimeH_ = -1;
    char lastIp_[20] = "";
    float netDown_ = -1, netUp_ = -1;
    float hist_[SPARK_N];
    int histCount_ = 0;
};
