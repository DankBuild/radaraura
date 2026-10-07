/**
 * Copyright (c) 2026 DankBuild - RadarAura (https://github.com/DankBuild/radaraura)
 * Licensed under the RadarAura Build Licence 1.0 - see LICENSE.md
 *
 * SEN66 Air Quality Monitor - sensor, alarms and audio.
 * The 480x800 dashboard itself lives in ui.c.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp_board_extra.h"
#include "lvgl.h"
#include "ui.h"
#include "driver/gpio.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "airquality";

#define I2C_SDA_PIN           GPIO_NUM_7
#define I2C_SCL_PIN           GPIO_NUM_8
#define SEN66_I2C_ADDR        0x6B
#define SEN66_CMD_RESET       0xD304
#define SEN66_CMD_START       0x0021
#define SEN66_CMD_READ_VALUES 0x0300

// Alarm cooldowns: how long before the same alert can re-trigger
#define ALARM_COOLDOWN_US      (5LL * 60 * 1000 * 1000)  // 5 minutes - normal alerts
#define FIRE_COOLDOWN_US       (1LL * 60 * 1000 * 1000)  // 1 minute - fire alert (more urgent)
#define FIRE_REPEAT_COUNT      3                          // Fire alarm plays this many times per trigger
#define BEEP_SAMPLE_RATE       44100
#define BEEP_FREQ_HZ           880   // A5 - clearly audible
#define BEEP_DURATION_MS       400

// Fire/smoke alert thresholds (NOT user-adjustable - safety-critical, research-based).
// All three must be exceeded simultaneously for the fire alarm to trigger.
//   PM2.5 > 50 ug/m3  : sustained smoke particles (above EPA "unhealthy" threshold)
//   VOC index > 300   : strong combustion VOC signal
//   NOx index > 100   : combustion NOx signal (well above ambient ~50)
#define FIRE_PM25_THRESHOLD   50.0f
#define FIRE_VOC_THRESHOLD    300.0f
#define FIRE_NOX_THRESHOLD    100.0f

// === Alert configuration ===
// There is no on-device settings menu: edit these and rebuild. Sound needs a
// speaker on the board's speaker connector; without one everything is silent.
#define ALERT_VOLUME          70   // 0-100
#define FIRE_ALERT_ENABLED    1    // smoke/fire alarm (PM2.5 + VOC + NOx all high at once)
#define METRIC_ALERTS_ENABLED 0    // beep when a single metric passes metric_thresholds[]
#define ALERT_VOICE_ENABLED   1    // spoken message after the beep (TTS MP3s in SPIFFS/SD)
#define ALERT_VOICE_INDEX     0    // 0=Calm 1=Lovely 2=Kind 3=Graceful (voice_dirs below)

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t sen66_dev = NULL;

// Metric indices used in arrays
enum {
    M_CO2 = 0,
    M_PM1,
    M_PM25,
    M_PM4,
    M_PM10,
    M_VOC,
    M_NOX,
    M_TEMP_HI,
    M_HUM_HI,
    M_COUNT
};

static const char *metric_names[M_COUNT] = {
    "CO2 ppm", "PM1.0 ug/m3", "PM2.5 ug/m3", "PM4.0 ug/m3", "PM10 ug/m3",
    "VOC Index", "NOx Index", "Temp high (C)", "Humidity high (%)"
};
// Filenames (without path/extension) for TTS audio playback
static const char *metric_tts_files[M_COUNT] = {
    "co2", "pm1", "pm25", "pm4", "pm10", "voc", "nox", "temp", "humid"
};
// Per-metric alert thresholds (only used when METRIC_ALERTS_ENABLED)
static const float metric_thresholds[M_COUNT] = {
    1500, 25, 55, 75, 150, 300, 150, 35, 85
};

// Voice options - match generate_tts.py VOICES order. Subdirectory name in /spiffs or /sdcard/alerts/
#define VOICE_COUNT 4
static const char *voice_dirs[VOICE_COUNT] = { "voice1", "voice2", "voice3", "voice4" };

static bool audio_ready = false;
static bool sdcard_ready = false;
static bool spiffs_ready = false;
#define SDCARD_MOUNT_POINT  "/sdcard"
#define SPIFFS_MOUNT_POINT  "/spiffs"
// We try SPIFFS first (firmware-bundled audio), then SD card as fallback
#define TTS_EXT             ".mp3"

// Last alarm timestamp per metric + fire (to enforce cooldown)
static int64_t last_alarm_us[M_COUNT + 1] = {0};  // last is fire

// Generate and play a sine wave beep on the speaker
static void play_beep(int freq_hz, int duration_ms)
{
    if (!audio_ready) return;

    size_t samples = (BEEP_SAMPLE_RATE * duration_ms) / 1000;
    int16_t *buf = malloc(samples * 2 * sizeof(int16_t));  // stereo
    if (!buf) return;

    float omega = 2.0f * (float)M_PI * freq_hz / BEEP_SAMPLE_RATE;
    int fade = BEEP_SAMPLE_RATE / 100;  // 10ms fade in/out to avoid pops
    for (size_t i = 0; i < samples; i++) {
        float env = 1.0f;
        if ((int)i < fade) env = (float)i / fade;
        else if ((int)i > (int)samples - fade) env = (float)(samples - i) / fade;
        int16_t s = (int16_t)(sinf(omega * i) * 28000 * env);
        buf[i*2]     = s;
        buf[i*2 + 1] = s;
    }

    size_t bytes_written = 0;
    bsp_extra_i2s_write(buf, samples * 2 * sizeof(int16_t), &bytes_written, 5000);
    free(buf);
}

// Try to play a TTS file. Looks in SPIFSS (firmware-bundled) first,
// then SD card (/sdcard/alerts/). Returns true if started.
static bool try_play_tts(const char *name)
{
    if (!ALERT_VOICE_ENABLED) return false;
    char path[128];

    // 1) SPIFFS bundled (preferred) - files were packed at build time
    if (spiffs_ready) {
        snprintf(path, sizeof(path), "%s/%s/%s%s",
                 SPIFFS_MOUNT_POINT, voice_dirs[ALERT_VOICE_INDEX], name, TTS_EXT);
        FILE *f = fopen(path, "r");
        if (f) {
            fclose(f);
            if (bsp_extra_player_play_file(path) == ESP_OK) {
                ESP_LOGI(TAG, "Playing TTS (spiffs): %s", path);
                return true;
            }
        }
    }

    // 2) SD card fallback (/sdcard/alerts/voiceN/...)
    if (sdcard_ready) {
        snprintf(path, sizeof(path), "%s/alerts/%s/%s%s",
                 SDCARD_MOUNT_POINT, voice_dirs[ALERT_VOICE_INDEX], name, TTS_EXT);
        FILE *f = fopen(path, "r");
        if (f) {
            fclose(f);
            if (bsp_extra_player_play_file(path) == ESP_OK) {
                ESP_LOGI(TAG, "Playing TTS (sd): %s", path);
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "TTS file not found for '%s'", name);
    return false;
}

// Async task: play attention beep, then voice file (if available)
static void play_metric_alert_task(void *arg)
{
    int idx = (int)(intptr_t)arg;
    play_beep(BEEP_FREQ_HZ, 250);
    vTaskDelay(pdMS_TO_TICKS(80));
    play_beep(BEEP_FREQ_HZ + 200, 250);
    vTaskDelay(pdMS_TO_TICKS(300));
    if (idx >= 0 && idx < M_COUNT) {
        try_play_tts(metric_tts_files[idx]);
    }
    vTaskDelete(NULL);
}

static void play_fire_alert_task(void *arg)
{
    // Repeat the urgent pattern several times - fire is safety-critical
    for (int rep = 0; rep < FIRE_REPEAT_COUNT; rep++) {
        // Urgent rapid beeps
        for (int i = 0; i < 4; i++) {
            play_beep(1200, 150);
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        try_play_tts("fire");
        vTaskDelay(pdMS_TO_TICKS(800));  // pause between repeats
    }
    vTaskDelete(NULL);
}

static void trigger_metric_alert(int metric_idx)
{
    if (!audio_ready) return;
    xTaskCreate(play_metric_alert_task, "alert", 4096,
                (void*)(intptr_t)metric_idx, 4, NULL);
}

static void maybe_alarm_metric(int metric_idx, float value)
{
    if (!METRIC_ALERTS_ENABLED) return;
    if (value < metric_thresholds[metric_idx]) return;
    int64_t now = esp_timer_get_time();
    if (now - last_alarm_us[metric_idx] < ALARM_COOLDOWN_US) return;
    last_alarm_us[metric_idx] = now;
    ESP_LOGW(TAG, "ALARM: %s exceeded (%.1f >= %.1f)",
             metric_names[metric_idx], value, metric_thresholds[metric_idx]);
    trigger_metric_alert(metric_idx);
}

// Called while the combined fire condition holds (see handle_sample)
static void maybe_fire_alarm(float pm25, float voc, float nox)
{
    int64_t now = esp_timer_get_time();
    // Fire uses its own shorter cooldown - re-warn aggressively while smoke persists
    if (now - last_alarm_us[M_COUNT] < FIRE_COOLDOWN_US) return;
    last_alarm_us[M_COUNT] = now;
    ESP_LOGE(TAG, "FIRE ALARM: PM2.5=%.1f VOC=%.0f NOx=%.0f", pm25, voc, nox);
    if (!audio_ready) return;
    xTaskCreate(play_fire_alert_task, "fire_alert", 4096, NULL, 5, NULL);
}

static uint8_t sensirion_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

static esp_err_t sen66_send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = {cmd >> 8, cmd & 0xFF};
    return i2c_master_transmit(sen66_dev, buf, 2, 1000);
}

static esp_err_t sen66_read_with_crc(uint8_t *out, size_t len)
{
    size_t raw_len = (len / 2) * 3;
    uint8_t *raw = malloc(raw_len);
    if (!raw) return ESP_ERR_NO_MEM;
    esp_err_t ret = i2c_master_receive(sen66_dev, raw, raw_len, 1000);
    if (ret == ESP_OK) {
        for (size_t i = 0, j = 0; i < raw_len; i += 3, j += 2) {
            if (sensirion_crc8(&raw[i], 2) != raw[i + 2]) {
                ESP_LOGW(TAG, "CRC error - frame dropped");
                ret = ESP_ERR_INVALID_CRC;
            }
            out[j] = raw[i];
            out[j + 1] = raw[i + 1];
        }
    }
    free(raw);
    return ret;
}

// SEN66 "unknown" markers (datasheet: returned until a value is available,
// e.g. CO2 during the first seconds and VOC/NOx during warm-up)
#define SEN66_U16_UNKNOWN 0xFFFF
#define SEN66_I16_UNKNOWN 0x7FFF

// Consecutive failed reads before the dashboard shows "Sensor offline"
#define SENSOR_OFFLINE_AFTER 5

static void handle_sample(air_sample_t *s)
{
    // Combined fire/smoke condition: PM2.5 + VOC + NOx all spike together
    s->fire = FIRE_ALERT_ENABLED && s->pm_ok && s->voc_ok && s->nox_ok &&
              s->pm25 >= FIRE_PM25_THRESHOLD &&
              s->voc  >= FIRE_VOC_THRESHOLD &&
              s->nox  >= FIRE_NOX_THRESHOLD;

    ui_update(s);

    // Per-metric alarms (each with own threshold + cooldown) - only on valid data
    if (s->co2_ok) maybe_alarm_metric(M_CO2, (float)s->co2);
    if (s->pm_ok) {
        maybe_alarm_metric(M_PM1,  s->pm1);
        maybe_alarm_metric(M_PM25, s->pm25);
        maybe_alarm_metric(M_PM4,  s->pm4);
        maybe_alarm_metric(M_PM10, s->pm10);
    }
    if (s->voc_ok) maybe_alarm_metric(M_VOC, s->voc);
    if (s->nox_ok) maybe_alarm_metric(M_NOX, s->nox);
    if (s->rht_ok) {
        maybe_alarm_metric(M_TEMP_HI, s->temp);
        maybe_alarm_metric(M_HUM_HI,  s->humid);
    }
    if (s->fire) maybe_fire_alarm(s->pm25, s->voc, s->nox);
}

static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Starting sensor task...");

    // Reset sensor
    sen66_send_cmd(SEN66_CMD_RESET);
    vTaskDelay(pdMS_TO_TICKS(1500));  // SEN66 needs time after reset

    // Start measurement
    sen66_send_cmd(SEN66_CMD_START);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Sensor started, reading data...");

    int failures = 0;
    while (1) {
        uint8_t data[18];
        esp_err_t ret = sen66_send_cmd(SEN66_CMD_READ_VALUES);
        if (ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            ret = sen66_read_with_crc(data, 18);
        }

        if (ret == ESP_OK) {
            failures = 0;
            uint16_t r_pm1  = (data[0] << 8) | data[1];
            uint16_t r_pm25 = (data[2] << 8) | data[3];
            uint16_t r_pm4  = (data[4] << 8) | data[5];
            uint16_t r_pm10 = (data[6] << 8) | data[7];
            int16_t  r_hum  = (int16_t)((data[8] << 8) | data[9]);
            int16_t  r_temp = (int16_t)((data[10] << 8) | data[11]);
            int16_t  r_voc  = (int16_t)((data[12] << 8) | data[13]);
            int16_t  r_nox  = (int16_t)((data[14] << 8) | data[15]);
            uint16_t r_co2  = (data[16] << 8) | data[17];

            air_sample_t s = {
                .pm_ok  = r_pm1 != SEN66_U16_UNKNOWN && r_pm25 != SEN66_U16_UNKNOWN &&
                          r_pm4 != SEN66_U16_UNKNOWN && r_pm10 != SEN66_U16_UNKNOWN,
                .rht_ok = r_hum != SEN66_I16_UNKNOWN && r_temp != SEN66_I16_UNKNOWN,
                // Index range is 1..500; the SEN66 reports 0 until its
                // VOC/NOx algorithms have warmed up (~1-3 min after start)
                .voc_ok = r_voc != SEN66_I16_UNKNOWN && r_voc > 0,
                .nox_ok = r_nox != SEN66_I16_UNKNOWN && r_nox > 0,
                .co2_ok = r_co2 != SEN66_U16_UNKNOWN,
                .pm1 = r_pm1 / 10.0f, .pm25 = r_pm25 / 10.0f,
                .pm4 = r_pm4 / 10.0f, .pm10 = r_pm10 / 10.0f,
                .humid = r_hum / 100.0f, .temp = r_temp / 200.0f,
                .voc = r_voc / 10.0f, .nox = r_nox / 10.0f,
                .co2 = r_co2,
            };

            ESP_LOGI(TAG, "CO2:%d PM1:%.1f PM2.5:%.1f PM4:%.1f PM10:%.1f T:%.1f H:%.0f VOC:%.0f NOx:%.0f",
                     s.co2, s.pm1, s.pm25, s.pm4, s.pm10, s.temp, s.humid, s.voc, s.nox);

            bsp_display_lock(0);
            handle_sample(&s);
            bsp_display_unlock();
        } else if (++failures == SENSOR_OFFLINE_AFTER) {
            ESP_LOGE(TAG, "SEN66 not responding (%s)", esp_err_to_name(ret));
            bsp_display_lock(0);
            ui_set_sensor_state(UI_SENSOR_OFFLINE);
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* Radio kill-switch: the ESP32-C6 co-processor is this module's ONLY radio
 * (the P4 has none in silicon). Its enable/reset line is wired to P4 GPIO54
 * (active-high to run; SDIO on GPIO14-19 — per JC-ESP32P4-M3 documentation).
 * Driving GPIO54 LOW from first boot holds the C6 permanently in reset: it
 * never executes a single instruction — no WiFi, no BLE, no radio firmware
 * running. Combined with this application containing no radio stack at all
 * (no esp_wifi_remote / esp_hosted components are compiled in), the product
 * has no functioning radio. Do NOT change this in a production build. */
#define C6_RADIO_EN_GPIO 54

static void radio_hold_in_reset(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask  = 1ULL << C6_RADIO_EN_GPIO,
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(C6_RADIO_EN_GPIO, 0);   /* EN low = C6 held in reset */
    gpio_hold_en(C6_RADIO_EN_GPIO);        /* latch the level against glitches */
    ESP_LOGI("radio", "ESP32-C6 held in permanent reset (GPIO%d low) — no radio", C6_RADIO_EN_GPIO);
}

void app_main(void)
{
    radio_hold_in_reset();   /* first thing, before anything else runs */

    ESP_LOGI(TAG, "Starting Air Quality Monitor...");

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        }
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Mount SPIFFS (firmware-bundled TTS audio files in 'storage' partition)
    {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = SPIFFS_MOUNT_POINT,
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = false,
        };
        esp_err_t r = esp_vfs_spiffs_register(&conf);
        if (r == ESP_OK) {
            spiffs_ready = true;
            size_t total = 0, used = 0;
            esp_spiffs_info("storage", &total, &used);
            ESP_LOGI(TAG, "SPIFFS mounted at %s (%u/%u bytes used)",
                     SPIFFS_MOUNT_POINT, (unsigned)used, (unsigned)total);
        } else {
            ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(r));
        }
    }

    // Mount SD card (optional fallback)
    if (bsp_sdcard_mount() == ESP_OK) {
        sdcard_ready = true;
        ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);
    } else {
        ESP_LOGW(TAG, "SD card not mounted");
    }

    // Initialize audio codec (ES8311) and apply saved volume/mute
    if (bsp_extra_codec_init() == ESP_OK) {
        bsp_extra_codec_set_fs(BEEP_SAMPLE_RATE, 16, I2S_SLOT_MODE_STEREO);
        int set = ALERT_VOLUME;
        bsp_extra_codec_volume_set(ALERT_VOLUME, &set);
        bsp_extra_codec_mute_set(false);
        // Initialize audio player ONLY if we have TTS files (SPIFFS or SD).
        // Otherwise the player claims the I2S channel and breaks our direct beeps.
        if (spiffs_ready || sdcard_ready) {
            bsp_extra_player_init();
            ESP_LOGI(TAG, "Audio player initialized for TTS playback");
        }
        audio_ready = true;
        ESP_LOGI(TAG, "Audio codec ready");
    } else {
        ESP_LOGW(TAG, "Audio codec init failed - alarms disabled");
    }
    
    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Display ready, UI should be visible now");
    
    // Give LVGL time to render the UI
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Get the existing I2C bus from BSP (shared with touch)
    i2c_bus = bsp_i2c_get_handle();
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SEN66_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &sen66_dev));
    
    ESP_LOGI(TAG, "I2C ready");
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);
}
