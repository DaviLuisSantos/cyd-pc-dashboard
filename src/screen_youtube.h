#pragma once
#include "screens.h"
#include "agent_client.h"
#include <string.h>

// YouTube — inscritos/views/vídeos dos canais configurados no agente
// (seção "youtube" do agent/config.json).
class YouTubeScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_YOUTUBE; }
    uint16_t accentColor() override { return DRACULA_RED; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconPlay(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastPoll_ = 0;
        drawn_ = false;
        layoutDrawn_ = false;
        nShown_ = 0;
        for (int i = 0; i < MAX_CH; i++) rows_[i] = Row();
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ < YT_POLL_MS && drawn_) return;
        lastPoll_ = now;
        poll(tft);
    }

private:
    static const int MAX_CH = 3;
    static const int CARD_H = 56;

    struct Row {
        char title[26];
        long subs = -1, views = -1, videos = -1;
        Row() { title[0] = '\0'; }
    };

    // 3º card termina em y=214 — folga pro status (221..240)
    int rowY(int i) { return 38 + i * 60; }

    // 1234 -> "1.2k", 1234567 -> "1.2M"
    void fmtCompact(char* buf, size_t len, long v) {
        if (v >= 1000000) {
            snprintf(buf, len, "%.1fM", v / 1000000.0f);
        } else if (v >= 10000) {
            snprintf(buf, len, "%.0fk", v / 1000.0f);
        } else if (v >= 1000) {
            snprintf(buf, len, "%.1fk", v / 1000.0f);
        } else {
            snprintf(buf, len, "%ld", v);
        }
    }

    void drawNotConfigured(TFT_eSPI& tft) {
        if (layoutDrawn_) return;
        layoutDrawn_ = true;
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H, DRACULA_BG);
        theme::iconPlay(tft, 160, 110, 16, DRACULA_COMMENT);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
        tft.drawString(i18n::tr().YT_HINT_1, 160, 150, 2);
        tft.drawString(i18n::tr().YT_HINT_2, 160, 170, 2);
    }

    void drawLayout(TFT_eSPI& tft, int n) {
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H, DRACULA_BG);
        for (int i = 0; i < n; i++) {
            int y = rowY(i);
            theme::drawCard(tft, 8, y, 304, CARD_H);
            theme::iconPlay(tft, 30, y + CARD_H / 2, 11, DRACULA_RED);
        }
        theme::beginLabelFont(tft);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_COMMENT, DRACULA_CARD);
        for (int i = 0; i < n; i++) {
            tft.drawString(i18n::tr().LBL_SUBS, 118, rowY(i) + 40);
        }
        theme::endLabelFont(tft);
    }

    void drawRow(TFT_eSPI& tft, int i, const char* title, long subs, long views, long videos) {
        Row& r = rows_[i];
        bool changed = strcmp(title, r.title) != 0 || subs != r.subs ||
                       views != r.views || videos != r.videos;
        if (!changed) return;
        strncpy(r.title, title, sizeof(r.title) - 1);
        r.title[sizeof(r.title) - 1] = '\0';
        r.subs = subs;
        r.views = views;
        r.videos = videos;

        int y = rowY(i);
        theme::drawValue(tft, 50, y + 8, 200, 16, r.title, 2,
                         DRACULA_FG, DRACULA_CARD, ML_DATUM);

        char num[12], buf[24];
        fmtCompact(num, sizeof(num), subs);
        theme::drawValue(tft, 50, y + 29, 64, 22, num, 4,
                         DRACULA_FG, DRACULA_CARD, ML_DATUM);

        fmtCompact(num, sizeof(num), views);
        snprintf(buf, sizeof(buf), i18n::tr().FMT_VIEWS, num);
        theme::drawValue(tft, 176, y + 32, 126, 16, buf, 2,
                         DRACULA_COMMENT, DRACULA_CARD, MR_DATUM);
        snprintf(buf, sizeof(buf), i18n::tr().FMT_VIDEOS, videos);
        theme::drawValue(tft, 176, y + 8, 126, 16, buf, 2,
                         DRACULA_COMMENT, DRACULA_CARD, MR_DATUM);
    }

    void poll(TFT_eSPI& tft) {
        JsonDocument doc;
        if (!agent::fetch(tft, "/youtube", doc)) return;

        bool configured = doc["configured"] | false;
        JsonArray channels = doc["channels"].as<JsonArray>();
        if (!configured || channels.size() == 0) {
            drawNotConfigured(tft);
            agent::showStatus(tft, DRACULA_ORANGE,
                              configured ? i18n::tr().ST_NO_DATA : i18n::tr().ST_NO_API_KEY);
            drawn_ = true;
            return;
        }

        int n = channels.size() < MAX_CH ? channels.size() : MAX_CH;
        if (!layoutDrawn_ || n != nShown_) {
            layoutDrawn_ = true;
            nShown_ = n;
            for (int i = 0; i < MAX_CH; i++) rows_[i] = Row();
            drawLayout(tft, n);
        }
        for (int i = 0; i < n; i++) {
            JsonObject c = channels[i];
            drawRow(tft, i, c["title"] | "?", c["subs"] | 0L,
                    c["views"] | 0L, c["videos"] | 0L);
        }

        agent::showOk(tft);
        drawn_ = true;
    }

    Row rows_[MAX_CH];
    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
    bool layoutDrawn_ = false;
    int nShown_ = 0;
};
