/**
 * Copyright (c) 2026 DankBuild - RadarAura (https://github.com/DankBuild/radaraura)
 * Licensed under the RadarAura Build Licence 1.0 - see LICENSE.md
 *
 * Dashboard UI (pure LVGL 8 - no ESP-IDF dependencies, so it can also be
 * rendered on a PC by tools/ui_sim for screenshots).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

// Palette
#define UI_COL_BG         0x0B0E13
#define UI_COL_CARD       0x161B23
#define UI_COL_CARD_PRESS 0x202733
#define UI_COL_LINE       0x262E3A
#define UI_COL_TEXT       0xE9EDF2
#define UI_COL_TEXT_2     0x8B95A5
#define UI_COL_TEXT_3     0x5B6576
#define UI_COL_ACCENT     0x3B82F6
#define UI_COL_GOOD       0x34D399
#define UI_COL_FAIR       0xFACC15
#define UI_COL_POOR       0xFB923C
#define UI_COL_BAD        0xF43F5E

typedef enum {
    UI_SENSOR_STARTING = 0,
    UI_SENSOR_OK,
    UI_SENSOR_OFFLINE,
} ui_sensor_state_t;

// One SEN66 reading. *_ok = false while the sensor reports "unknown"
// (0xFFFF / 0x7FFF), e.g. during warm-up.
typedef struct {
    bool co2_ok, pm_ok, rht_ok, voc_ok, nox_ok;
    uint16_t co2;
    float pm1, pm25, pm4, pm10;
    float temp, humid;
    float voc, nox;
    bool fire;  // combined smoke/fire condition (decided by main.c)
} air_sample_t;

// Build the dashboard on the active screen.
void ui_create(void);

// Push a new reading. Call with the display lock held. Also appends to the
// CO2 history chart every UI_HISTORY_STEP_MS.
void ui_update(const air_sample_t *s);

// Append one CO2 point to the history chart directly (used by the simulator
// to pre-fill history; ui_update calls it on its own schedule).
void ui_history_push(uint16_t co2);

void ui_set_sensor_state(ui_sensor_state_t st);

#define UI_HISTORY_POINTS  120
#define UI_HISTORY_STEP_MS (30 * 1000)   // 120 x 30 s = last hour
