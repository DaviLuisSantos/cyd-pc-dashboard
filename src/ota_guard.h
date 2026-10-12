#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include "config.h"
#include "settings.h"
#include "notify.h"

// Rollback de OTA. O bootloader do arduino-esp32 tem rollback habilitado
// (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE), mas o core marca o app como válido
// sozinho no boot. main.cpp sobrescreve verifyRollbackLater() pra assumir isso:
//
// - Firmware recém-gravado por OTA sobe "pendente de verificação".
// - poll() marca como válido quando estiver saudável: WiFi conectado e o loop()
//   rodando há OTA_VERIFY_MIN_MS.
// - Se travar/resetar antes disso, o bootloader volta sozinho pra versão anterior
//   no próximo boot. Se ficar pendurado sem resetar (sem WiFi, loop travado),
//   um esp_timer (roda fora do loop) força o rollback em OTA_VERIFY_TIMEOUT_MS.
// - Depois de um rollback, avisa uma vez (notificação na tela) qual versão falhou.
namespace otaguard {

static bool pending_ = false;
static esp_timer_handle_t timer_ = nullptr;
static char failedVersion_[32] = "";

inline void rollbackNow(void*) {
    // Roda no task do esp_timer: funciona mesmo com o loop() travado
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

inline void begin() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        pending_ = true;
        Serial.printf("ota: firmware novo em verificacao (%s), rollback em %lus se nao ficar saudavel\n",
                      running->label, (unsigned long)(OTA_VERIFY_TIMEOUT_MS / 1000));
        esp_timer_create_args_t args = {};
        args.callback = rollbackNow;
        args.name = "ota_rollback";
        if (esp_timer_create(&args, &timer_) == ESP_OK) {
            esp_timer_start_once(timer_, (uint64_t)OTA_VERIFY_TIMEOUT_MS * 1000);
        }
    }

    // Cada slot guarda no NVS a versão que rodou nele (o esp_app_desc do Arduino
    // não tem o FW_VERSION). Grava cedo, antes de qualquer coisa que possa travar.
    Preferences p;
    p.begin(settings::NVS_NAMESPACE, false);
    String slotKey = String("v_") + running->label;
    if (p.getString(slotKey.c_str(), "") != FW_VERSION) p.putString(slotKey.c_str(), FW_VERSION);

    // Houve rollback? esp_ota_get_last_invalid_partition = o slot que falhou.
    const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
    if (invalid) {
        String failed = p.getString((String("v_") + invalid->label).c_str(), "?");
        strlcpy(failedVersion_, failed.c_str(), sizeof(failedVersion_));
        // Avisa uma vez por OTA (onUpdated() rearma)
        if (!p.getBool("rbnoted", false)) {
            p.putBool("rbnoted", true);
            char text[120];
            snprintf(text, sizeof(text), "Firmware %s didn't start properly, so the panel went back to %s.",
                     failedVersion_, FW_VERSION);
            notify::push("Update rolled back", text, notify::WARN, 0);
        }
    }
    p.end();
}

// OTA gravada com sucesso (antes de reiniciar): rearma o aviso de rollback
inline void onUpdated() {
    Preferences p;
    p.begin(settings::NVS_NAMESPACE, false);
    p.putBool("rbnoted", false);
    p.end();
}

inline bool pending() { return pending_; }
inline const char* failedVersion() { return failedVersion_; }
inline const char* runningSlot() { return esp_ota_get_running_partition()->label; }

// Chamado no loop()
inline void poll() {
    if (!pending_) return;
    if (WiFi.status() != WL_CONNECTED || millis() < OTA_VERIFY_MIN_MS) return;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        pending_ = false;
        if (timer_) esp_timer_stop(timer_);
        Serial.println("ota: firmware marcado como valido");
    }
}

} // namespace otaguard
