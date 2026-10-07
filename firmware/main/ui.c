/**
 * Copyright (c) 2026 DankBuild - RadarAura (https://github.com/DankBuild/radaraura)
 * Licensed under the RadarAura Build Licence 1.0 - see LICENSE.md
 *
 * Dashboard UI - 480x800 portrait.
 *
 *   top bar     title + sensor state
 *   hero        air score arc (worst of CO2/PM2.5/PM10/VOC/NOx) + status + advice
 *   CO2         value, trend, last-hour chart
 *   particles   PM2.5 big + PM1/PM4/PM10
 *   2x2 tiles   VOC, NOx, temperature, humidity - value + colour scale
 *
 * Every card is tappable and opens an explanation sheet.
 */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W   480
#define SCR_H   800
#define MARGIN  8
#define GAP     8
#define CARD_R  16

// ---------------------------------------------------------------------------
// Levels, bands and scores
// ---------------------------------------------------------------------------

enum { LV_GOOD = 0, LV_FAIR, LV_POOR, LV_BAD };

static const uint32_t level_colors[4] = { UI_COL_GOOD, UI_COL_FAIR, UI_COL_POOR, UI_COL_BAD };

// A colour scale: consecutive bands [prev_end, end) each with a level
typedef struct { float end; uint8_t level; } band_t;
typedef struct {
    float min;     // left end of the drawn scale
    float ideal;   // value that scores 100 (pollutants only)
    uint8_t n;
    band_t b[7];
} scale_t;

typedef enum { MI_CO2 = 0, MI_PM, MI_VOC, MI_NOX, MI_TEMP, MI_HUM, MI_COUNT } metric_id_t;

static const scale_t scales[MI_COUNT] = {
    [MI_CO2]  = { 400, 600, 4, {{800, LV_GOOD}, {1000, LV_FAIR}, {1500, LV_POOR}, {2500, LV_BAD}} },
    [MI_PM]   = { 0,   0,   4, {{12, LV_GOOD}, {35, LV_FAIR}, {55, LV_POOR}, {150, LV_BAD}} },
    [MI_VOC]  = { 0,   100, 4, {{100, LV_GOOD}, {200, LV_FAIR}, {300, LV_POOR}, {500, LV_BAD}} },
    [MI_NOX]  = { 0,   1,   4, {{20, LV_GOOD}, {50, LV_FAIR}, {150, LV_POOR}, {300, LV_BAD}} },
    [MI_TEMP] = { 5,   0,   7, {{10, LV_BAD}, {15, LV_POOR}, {18, LV_FAIR}, {24, LV_GOOD},
                           {30, LV_FAIR}, {35, LV_POOR}, {40, LV_BAD}} },
    [MI_HUM]  = { 0,   0,   7, {{15, LV_BAD}, {20, LV_POOR}, {30, LV_FAIR}, {60, LV_GOOD},
                           {70, LV_FAIR}, {80, LV_POOR}, {100, LV_BAD}} },
};

// Good/fair/poor borders per particle size: PM1, PM2.5, PM4, PM10 (ug/m3)
enum { PM_1 = 0, PM_25, PM_4, PM_10, PM_SIZES };
static const char *pm_names[PM_SIZES] = { "PM1", "PM2.5", "PM4", "PM10" };
static const float pm_cuts[PM_SIZES][3] = {
    { 10, 25, 50 }, { 12, 35, 55 }, { 25, 50, 75 }, { 50, 100, 150 },
};

static uint8_t pm_level(int size, float v)
{
    for (int i = 0; i < 3; i++) {
        if (v < pm_cuts[size][i]) return i;
    }
    return 3;
}

static uint8_t level_of(metric_id_t m, float v)
{
    const scale_t *s = &scales[m];
    for (int i = 0; i < s->n; i++) {
        if (v < s->b[i].end) return s->b[i].level;
    }
    return s->b[s->n - 1].level;
}

// 0..100 sub-score for the "pollutant" metrics: 100 at or below ideal, 75 at the
// good/fair border, 50 at fair/poor, 25 at poor/bad, 0 at the scale end.
static int score_on(const scale_t *s, float v)
{
    float lo = s->ideal;
    if (v <= lo) return 100;
    for (int i = 0; i < 4; i++) {
        float hi = s->b[i].end;
        if (v < hi) {
            float f = (v - lo) / (hi - lo);
            return (int)lroundf(100 - 25 * i - 25 * f);
        }
        lo = hi;
    }
    return 0;
}

// PM10 only counts towards the air score (coarse dust); its borders match
// pm_cuts[PM_10] so the score agrees with the PM10 dot. WHO 24 h guideline: 45.
static const scale_t pm10_scale = { 0, 0, 4, {{50, LV_GOOD}, {100, LV_FAIR}, {150, LV_POOR}, {250, LV_BAD}} };

static const char *level_word(metric_id_t m, float v)
{
    uint8_t lv = level_of(m, v);
    if (m == MI_TEMP) {
        if (lv == LV_GOOD) return "Ideal";
        return v < 18 ? "Cold" : "Warm";
    }
    if (m == MI_HUM) {
        if (lv == LV_GOOD) return "Ideal";
        return v < 30 ? "Dry" : "Humid";
    }
    static const char *w[4] = { "Good", "Fair", "Poor", "High" };
    return w[lv];
}

// Integer-only formatting (LVGL's printf has no float support)
static void fmt_1dp(char *buf, size_t n, float v)
{
    int t = (int)lroundf(v * 10);
    snprintf(buf, n, "%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
}

static void fmt_pm(char *buf, size_t n, float v)
{
    if (v >= 100) snprintf(buf, n, "%d", (int)lroundf(v));
    else fmt_1dp(buf, n, v);
}

// ---------------------------------------------------------------------------
// Small building blocks
// ---------------------------------------------------------------------------

static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, txt);
    return l;
}

static void show_info(metric_id_t m);

static void card_click_cb(lv_event_t *e)
{
    show_info((metric_id_t)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, metric_id_t info)
{
    lv_obj_t *c = box(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(UI_COL_CARD), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(UI_COL_CARD_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_radius(c, CARD_R, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)info);
    return c;
}

// Pill-shaped status tag: tinted background, coloured text
static lv_obj_t *pill(lv_obj_t *parent)
{
    lv_obj_t *p = label(parent, &lv_font_montserrat_14, UI_COL_TEXT_2, "--");
    lv_obj_set_style_bg_opa(p, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(UI_COL_TEXT_3), 0);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(p, 10, 0);
    lv_obj_set_style_pad_ver(p, 3, 0);
    return p;
}

static void pill_set(lv_obj_t *p, const char *txt, uint32_t color)
{
    lv_label_set_text(p, txt);
    lv_obj_set_style_text_color(p, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(color), 0);
}

// Horizontal colour scale with a marker for the current value
typedef struct {
    lv_obj_t *track;
    lv_obj_t *marker;
    metric_id_t m;
    int w;
} scale_bar_t;

static void scale_bar_create(scale_bar_t *sb, lv_obj_t *parent, int x, int y, int w, metric_id_t m)
{
    const scale_t *s = &scales[m];
    float span = s->b[s->n - 1].end - s->min;
    sb->m = m;
    sb->w = w;
    sb->track = box(parent);
    lv_obj_set_pos(sb->track, x, y);
    lv_obj_set_size(sb->track, w, 6);
    lv_obj_set_style_radius(sb->track, 3, 0);
    lv_obj_set_style_clip_corner(sb->track, true, 0);

    float lo = s->min;
    for (int i = 0; i < s->n; i++) {
        int x0 = (int)lroundf((lo - s->min) / span * w);
        int x1 = (int)lroundf((s->b[i].end - s->min) / span * w);
        lv_obj_t *seg = box(sb->track);
        lv_obj_set_pos(seg, x0, 0);
        lv_obj_set_size(seg, LV_MAX(x1 - x0 - 2, 1), 6);   // 2 px gap between bands
        lv_obj_set_style_bg_opa(seg, LV_OPA_70, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(level_colors[s->b[i].level]), 0);
        lo = s->b[i].end;
    }

    sb->marker = box(parent);
    lv_obj_set_size(sb->marker, 14, 14);
    lv_obj_set_style_radius(sb->marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sb->marker, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sb->marker, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_border_width(sb->marker, 3, 0);
    lv_obj_set_style_border_color(sb->marker, lv_color_hex(UI_COL_CARD), 0);
    lv_obj_set_pos(sb->marker, x - 7, y - 4);
    lv_obj_add_flag(sb->marker, LV_OBJ_FLAG_HIDDEN);
}

static void scale_bar_set(scale_bar_t *sb, bool valid, float v)
{
    if (!valid) {
        lv_obj_add_flag(sb->marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const scale_t *s = &scales[sb->m];
    float span = s->b[s->n - 1].end - s->min;
    float f = (v - s->min) / span;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    lv_obj_set_x(sb->marker, lv_obj_get_x(sb->track) + (int)lroundf(f * sb->w) - 7);
    lv_obj_clear_flag(sb->marker, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

static lv_obj_t *state_dot, *state_lbl;

static lv_obj_t *hero_card, *hero_arc, *hero_score, *hero_cap, *hero_status, *hero_advice;
static int hero_shown_score = -1;

static lv_obj_t *co2_val, *co2_pill, *co2_trend, *co2_chart, *co2_axis_hi, *co2_axis_lo;
static lv_chart_series_t *co2_ser;
static uint32_t last_history_ms;
static bool history_started;

static lv_obj_t *pm25_val, *pm_pill, *pm1_val, *pm4_val, *pm10_val;
static lv_obj_t *pm_dot[PM_SIZES];   // level dots for the small PM1/PM4/PM10 values

typedef struct {
    lv_obj_t *val, *pill;
    scale_bar_t bar;
} tile_t;
static tile_t t_voc, t_nox, t_temp, t_hum;

static ui_sensor_state_t sensor_state = UI_SENSOR_STARTING;
static air_sample_t last;
static bool have_last;

// ---------------------------------------------------------------------------
// Top bar
// ---------------------------------------------------------------------------

static void create_top_bar(lv_obj_t *scr)
{
    lv_obj_t *t = label(scr, &lv_font_montserrat_22, UI_COL_TEXT, "RadarAura");
    lv_obj_set_pos(t, MARGIN + 10, 14);

    state_dot = box(scr);
    lv_obj_set_size(state_dot, 8, 8);
    lv_obj_set_style_radius(state_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(state_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(state_dot, lv_color_hex(UI_COL_FAIR), 0);

    state_lbl = label(scr, &lv_font_montserrat_16, UI_COL_TEXT_2, "Starting sensor");
}

// Status sits right-aligned in the top bar, dot to its left
static void state_realign(void)
{
    lv_obj_align(state_lbl, LV_ALIGN_TOP_RIGHT, -(MARGIN + 10), 18);
    lv_obj_align_to(state_dot, state_lbl, LV_ALIGN_OUT_LEFT_MID, -8, 1);
}

// ---------------------------------------------------------------------------
// Hero: air score
// ---------------------------------------------------------------------------

static void arc_anim_cb(void *arc, int32_t v)
{
    lv_arc_set_value(arc, (int16_t)v);
}

static void create_hero(lv_obj_t *scr, int y, int h)
{
    lv_obj_t *c = card(scr, MARGIN, y, SCR_W - 2 * MARGIN, h, MI_CO2);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);   // the score has no single info sheet
    lv_obj_set_style_border_color(c, lv_color_hex(UI_COL_BAD), 0);
    lv_obj_set_style_border_width(c, 0, 0);
    hero_card = c;

    int d = h - 28;
    hero_arc = lv_arc_create(c);
    lv_obj_set_size(hero_arc, d, d);
    lv_obj_set_pos(hero_arc, 16, 14);
    lv_arc_set_rotation(hero_arc, 135);
    lv_arc_set_bg_angles(hero_arc, 0, 270);
    lv_arc_set_range(hero_arc, 0, 100);
    lv_arc_set_value(hero_arc, 0);
    lv_obj_remove_style(hero_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(hero_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(hero_arc, 0, 0);
    lv_obj_set_style_bg_opa(hero_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(hero_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(hero_arc, lv_color_hex(UI_COL_LINE), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(hero_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(hero_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(hero_arc, lv_color_hex(UI_COL_TEXT_3), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(hero_arc, true, LV_PART_INDICATOR);

    // Fixed arc-wide labels with centred text, so they stay centred when the
    // text changes ("--" -> "87" -> "100"); align_to only positions once.
    hero_score = label(c, &lv_font_montserrat_48, UI_COL_TEXT, "--");
    lv_obj_set_width(hero_score, d);
    lv_obj_set_style_text_align(hero_score, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(hero_score, hero_arc, LV_ALIGN_CENTER, 0, -6);
    hero_cap = label(c, &lv_font_montserrat_12, UI_COL_TEXT_2, "AIR SCORE");
    lv_obj_set_style_text_letter_space(hero_cap, 2, 0);
    lv_obj_set_width(hero_cap, d);
    lv_obj_set_style_text_align(hero_cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(hero_cap, hero_arc, LV_ALIGN_CENTER, 0, 30);

    int tx = 16 + d + 22;
    int tw = SCR_W - 2 * MARGIN - tx - 16;
    hero_status = label(c, &lv_font_montserrat_32, UI_COL_TEXT, "Starting");
    lv_obj_set_pos(hero_status, tx, 34);
    lv_obj_set_width(hero_status, tw);

    hero_advice = label(c, &lv_font_montserrat_18, UI_COL_TEXT_2, "Sensor is warming up. Readings appear in a few seconds.");
    lv_label_set_long_mode(hero_advice, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(hero_advice, 4, 0);
    lv_obj_set_width(hero_advice, tw);
    lv_obj_set_pos(hero_advice, tx, 80);
}

static void hero_set(int score, uint32_t color, const char *status, const char *advice)
{
    lv_obj_set_style_border_width(hero_card, 0, 0);
    lv_obj_clear_flag(hero_cap, LV_OBJ_FLAG_HIDDEN);
    if (score < 0) {
        lv_label_set_text(hero_score, "--");
        lv_arc_set_value(hero_arc, 0);
        hero_shown_score = -1;
    } else {
        lv_label_set_text_fmt(hero_score, "%d", score);
        if (score != hero_shown_score) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, hero_arc);
            lv_anim_set_exec_cb(&a, arc_anim_cb);
            lv_anim_set_values(&a, lv_arc_get_value(hero_arc), score);
            lv_anim_set_time(&a, 600);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
            hero_shown_score = score;
        }
    }
    lv_obj_set_style_arc_color(hero_arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_label_set_text(hero_status, status);
    lv_obj_set_style_text_color(hero_status, lv_color_hex(color), 0);
    lv_label_set_text(hero_advice, advice);
}

// ---------------------------------------------------------------------------
// CO2 card with history chart
// ---------------------------------------------------------------------------

// Soft gradient under the chart line (pattern from the LVGL widgets demo)
static void chart_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->p1 || !dsc->p2) return;

    lv_draw_mask_line_param_t line_mask;
    lv_draw_mask_line_points_init(&line_mask, dsc->p1->x, dsc->p1->y, dsc->p2->x, dsc->p2->y,
                                  LV_DRAW_MASK_LINE_SIDE_BOTTOM);
    int16_t line_id = lv_draw_mask_add(&line_mask, NULL);

    lv_draw_mask_fade_param_t fade_mask;
    lv_draw_mask_fade_init(&fade_mask, &obj->coords, LV_OPA_COVER, obj->coords.y1,
                           LV_OPA_TRANSP, obj->coords.y2);
    int16_t fade_id = lv_draw_mask_add(&fade_mask, NULL);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_30;
    rect.bg_color = dsc->line_dsc->color;

    lv_area_t a;
    a.x1 = dsc->p1->x;
    a.x2 = dsc->p2->x - 1;
    a.y1 = LV_MIN(dsc->p1->y, dsc->p2->y);
    a.y2 = obj->coords.y2;
    lv_draw_rect(dsc->draw_ctx, &rect, &a);

    lv_draw_mask_free_param(&line_mask);
    lv_draw_mask_free_param(&fade_mask);
    lv_draw_mask_remove_id(line_id);
    lv_draw_mask_remove_id(fade_id);
}

static void create_co2(lv_obj_t *scr, int y, int h)
{
    int w = SCR_W - 2 * MARGIN;
    lv_obj_t *c = card(scr, MARGIN, y, w, h, MI_CO2);

    lv_obj_t *t = label(c, &lv_font_montserrat_16, UI_COL_TEXT_2, "CO2");
    lv_obj_set_pos(t, 18, 16);
    co2_pill = pill(c);
    lv_obj_align_to(co2_pill, t, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    co2_val = label(c, &lv_font_montserrat_48, UI_COL_TEXT, "--");
    lv_obj_set_pos(co2_val, 16, 46);
    lv_obj_t *u = label(c, &lv_font_montserrat_16, UI_COL_TEXT_2, "ppm");
    lv_obj_set_pos(u, 18, 102);

    co2_trend = label(c, &lv_font_montserrat_14, UI_COL_TEXT_3, "");
    lv_obj_set_pos(co2_trend, 18, h - 32);

    int cx = 168, cw = w - cx - 52, ch = h - 44;   // 38 px right gutter for scale labels
    co2_chart = lv_chart_create(c);
    lv_obj_set_pos(co2_chart, cx, 14);
    lv_obj_set_size(co2_chart, cw, ch);
    lv_obj_clear_flag(co2_chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(co2_chart, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(co2_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(co2_chart, 0, 0);
    lv_obj_set_style_pad_all(co2_chart, 0, 0);
    lv_obj_set_style_line_color(co2_chart, lv_color_hex(UI_COL_LINE), LV_PART_MAIN);
    lv_obj_set_style_line_width(co2_chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_size(co2_chart, 0, LV_PART_INDICATOR);
    lv_chart_set_type(co2_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(co2_chart, 4, 0);
    lv_chart_set_point_count(co2_chart, UI_HISTORY_POINTS);
    lv_chart_set_update_mode(co2_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(co2_chart, LV_CHART_AXIS_PRIMARY_Y, 400, 1200);
    co2_ser = lv_chart_add_series(co2_chart, lv_color_hex(UI_COL_GOOD), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(co2_chart, co2_ser, LV_CHART_POINT_NONE);

    // Scale labels at the top/bottom grid lines (updated by co2_chart_rescale)
    co2_axis_hi = label(c, &lv_font_montserrat_12, UI_COL_TEXT_3, "1200");
    lv_obj_align_to(co2_axis_hi, co2_chart, LV_ALIGN_OUT_RIGHT_TOP, 6, -6);
    co2_axis_lo = label(c, &lv_font_montserrat_12, UI_COL_TEXT_3, "400");
    lv_obj_align_to(co2_axis_lo, co2_chart, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, 6);
    lv_obj_add_event_cb(co2_chart, chart_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_t *l0 = label(c, &lv_font_montserrat_12, UI_COL_TEXT_3, "1 h ago");
    lv_obj_set_pos(l0, cx, h - 24);
    lv_obj_t *l1 = label(c, &lv_font_montserrat_12, UI_COL_TEXT_3, "now");
    lv_obj_align_to(l1, co2_chart, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 6);
}

// Rescale the chart so the line uses the height but small changes don't
// look dramatic: at least 400..1200 ppm, rounded to 200 ppm steps.
static void co2_chart_rescale(void)
{
    lv_coord_t *ys = lv_chart_get_y_array(co2_chart, co2_ser);
    int lo = 400, hi = 1200;
    for (int i = 0; i < UI_HISTORY_POINTS; i++) {
        if (ys[i] == LV_CHART_POINT_NONE) continue;
        if (ys[i] > hi) hi = ys[i];
        if (ys[i] < lo) lo = ys[i];
    }
    hi = ((hi + 199) / 200) * 200;
    lo = (lo / 200) * 200;
    lv_chart_set_range(co2_chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
    lv_label_set_text_fmt(co2_axis_hi, "%d", hi);
    lv_label_set_text_fmt(co2_axis_lo, "%d", lo);
    lv_obj_align_to(co2_axis_hi, co2_chart, LV_ALIGN_OUT_RIGHT_TOP, 6, -6);
    lv_obj_align_to(co2_axis_lo, co2_chart, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, 6);
}

void ui_history_push(uint16_t co2)
{
    lv_chart_set_next_value(co2_chart, co2_ser, co2);
    co2_chart_rescale();
}

// CO2 change over the last ~10 minutes from the history buffer
static void co2_trend_update(uint16_t now)
{
    lv_coord_t *ys = lv_chart_get_y_array(co2_chart, co2_ser);
    uint16_t start = lv_chart_get_x_start_point(co2_chart, co2_ser);
    int back = (10 * 60 * 1000) / UI_HISTORY_STEP_MS;
    // In shift mode the newest point is at index (start - 1)
    int idx = ((int)start - 1 - back + 2 * UI_HISTORY_POINTS) % UI_HISTORY_POINTS;
    if (ys[idx] == LV_CHART_POINT_NONE) {
        lv_label_set_text(co2_trend, "Collecting trend...");
        return;
    }
    int d = (int)now - ys[idx];
    if (d >= 50) lv_label_set_text_fmt(co2_trend, LV_SYMBOL_UP " %d in 10 min", d);
    else if (d <= -50) lv_label_set_text_fmt(co2_trend, LV_SYMBOL_DOWN " %d in 10 min", -d);
    else lv_label_set_text(co2_trend, "Steady");
}

// ---------------------------------------------------------------------------
// Particles card
// ---------------------------------------------------------------------------

static lv_obj_t *level_dot(lv_obj_t *parent)
{
    lv_obj_t *d = box(parent);
    lv_obj_set_size(d, 8, 8);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(UI_COL_TEXT_3), 0);
    return d;
}

static void dot_set(lv_obj_t *d, bool valid, uint8_t level)
{
    lv_obj_set_style_bg_color(d, lv_color_hex(valid ? level_colors[level] : UI_COL_TEXT_3), 0);
}

static lv_obj_t *mini_stat(lv_obj_t *c, int x, int y, const char *name, lv_obj_t **dot)
{
    *dot = level_dot(c);
    lv_obj_set_pos(*dot, x, y + 5);
    lv_obj_t *n = label(c, &lv_font_montserrat_14, UI_COL_TEXT_3, name);
    lv_obj_set_pos(n, x + 14, y);
    lv_obj_t *v = label(c, &lv_font_montserrat_24, UI_COL_TEXT, "--");
    lv_obj_set_pos(v, x, y + 22);
    return v;
}

static void create_pm(lv_obj_t *scr, int y, int h)
{
    int w = SCR_W - 2 * MARGIN;
    lv_obj_t *c = card(scr, MARGIN, y, w, h, MI_PM);

    lv_obj_t *t = label(c, &lv_font_montserrat_16, UI_COL_TEXT_2, "PM2.5");
    lv_obj_set_pos(t, 18, 16);
    pm_pill = pill(c);
    lv_obj_align_to(pm_pill, t, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    pm25_val = label(c, &lv_font_montserrat_44, UI_COL_TEXT, "--");
    lv_obj_set_pos(pm25_val, 16, 44);
    lv_obj_t *u = label(c, &lv_font_montserrat_14, UI_COL_TEXT_2, "ug/m3 particles");
    lv_obj_set_pos(u, 18, h - 28);

    lv_obj_t *div = box(c);
    lv_obj_set_size(div, 1, h - 32);
    lv_obj_set_pos(div, 236, 16);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, lv_color_hex(UI_COL_LINE), 0);

    int x0 = 258, step = 70;
    pm1_val  = mini_stat(c, x0, 30, "PM1", &pm_dot[PM_1]);
    pm4_val  = mini_stat(c, x0 + step, 30, "PM4", &pm_dot[PM_4]);
    pm10_val = mini_stat(c, x0 + 2 * step, 30, "PM10", &pm_dot[PM_10]);
}

// ---------------------------------------------------------------------------
// Small tiles (VOC, NOx, temperature, humidity)
// ---------------------------------------------------------------------------

static void create_tile(tile_t *t, lv_obj_t *scr, int x, int y, int w, int h,
                        metric_id_t m, const char *title, const char *unit)
{
    lv_obj_t *c = card(scr, x, y, w, h, m);

    lv_obj_t *tl = label(c, &lv_font_montserrat_16, UI_COL_TEXT_2, title);
    lv_obj_set_pos(tl, 16, 14);
    t->pill = pill(c);
    lv_obj_align(t->pill, LV_ALIGN_TOP_RIGHT, -12, 11);

    t->val = label(c, &lv_font_montserrat_36, UI_COL_TEXT, "--");
    lv_obj_set_pos(t->val, 14, 40);
    if (unit && *unit) {
        lv_obj_t *u = label(c, &lv_font_montserrat_16, UI_COL_TEXT_2, unit);
        lv_obj_set_user_data(t->val, u);   // re-aligned after each value change
        lv_obj_align_to(u, t->val, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);
    }

    scale_bar_create(&t->bar, c, 16, h - 22, w - 32, m);
}

static void tile_set(tile_t *t, metric_id_t m, bool valid, float v, const char *txt)
{
    lv_label_set_text(t->val, valid ? txt : "--");
    lv_obj_t *u = lv_obj_get_user_data(t->val);
    if (u) lv_obj_align_to(u, t->val, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);
    if (valid) {
        uint8_t lv = level_of(m, v);
        pill_set(t->pill, level_word(m, v), level_colors[lv]);
    } else {
        pill_set(t->pill, "Warming up", UI_COL_TEXT_3);
    }
    scale_bar_set(&t->bar, valid, v);
}

// ---------------------------------------------------------------------------
// Info sheet
// ---------------------------------------------------------------------------

typedef struct {
    const char *title;
    const char *unit;
    const char *description;
    const char *ranges[4];   // good, fair, poor, bad
} metric_info_t;

static const metric_info_t infos[MI_COUNT] = {
    [MI_CO2] = { "Carbon dioxide (CO2)", "ppm",
        "Builds up indoors when people breathe in a poorly ventilated room. "
        "High levels cause drowsiness, headaches and slower thinking. "
        "Opening a window is the fix.",
        { "Below 800 - fresh air", "800 - 1000 - acceptable", "1000 - 1500 - stale, ventilate", "Above 1500 - open windows now" } },
    [MI_PM] = { "Particulate matter", "ug/m3",
        "Tiny airborne particles, by size. PM1 is the finest (smoke, exhaust), "
        "PM10 the coarsest (dust, pollen). PM2.5 is the main health measure: "
        "it reaches deep into the lungs. Heavy dust (PM10 above 50) also "
        "lowers the air score.",
        { "PM2.5 below 12 - good", "12 - 35 - moderate", "35 - 55 - unhealthy for sensitive people", "Above 55 - unhealthy" } },
    [MI_VOC] = { "VOC index", "",
        "Volatile organic compounds: gases from cleaning products, paint, glue, "
        "perfume, cooking. Sensirion's index learns your room - 100 is its "
        "recent average, so the number shows change rather than an absolute amount.",
        { "Below 100 - normal for this room", "100 - 200 - slightly elevated", "200 - 300 - check for a source", "Above 300 - ventilate" } },
    [MI_NOX] = { "NOx index", "",
        "Nitrogen oxides from combustion: gas stoves, candles, fireplaces, "
        "traffic. The index rests at 1 in clean air and rises when NOx appears.",
        { "Below 20 - low", "20 - 50 - slightly elevated", "50 - 150 - combustion nearby", "Above 150 - strong combustion source" } },
    [MI_TEMP] = { "Temperature", "\xC2\xB0" "C",
        "Room temperature. Most people sleep and concentrate best "
        "around 18 - 24 \xC2\xB0" "C.",
        { "18 - 24 \xC2\xB0" "C - comfortable", "15 - 18 or 24 - 30 \xC2\xB0" "C - acceptable", "10 - 15 or 30 - 35 \xC2\xB0" "C - uncomfortable", "Below 10 or above 35 \xC2\xB0" "C" } },
    [MI_HUM] = { "Humidity", "%",
        "Relative humidity. Too dry irritates skin, eyes and airways; too humid "
        "lets mould and dust mites thrive.",
        { "30 - 60 % - ideal", "20 - 30 or 60 - 70 % - acceptable", "15 - 20 or 70 - 80 % - uncomfortable", "Below 15 or above 80 %" } },
};

static lv_obj_t *info_sheet;

static void info_close_cb(lv_event_t *e)
{
    if (info_sheet) {
        lv_obj_del_async(info_sheet);
        info_sheet = NULL;
    }
}

// Current value as text for the info sheet, or NULL if unknown
static bool current_text(metric_id_t m, char *buf, size_t n, float *v)
{
    if (!have_last) return false;
    char tmp[16];
    switch (m) {
    case MI_CO2:  if (!last.co2_ok) return false; *v = last.co2; snprintf(buf, n, "%d ppm", last.co2); return true;
    case MI_PM:   if (!last.pm_ok) return false; *v = last.pm25; fmt_pm(tmp, sizeof tmp, last.pm25); snprintf(buf, n, "PM2.5 %s ug/m3", tmp); return true;
    case MI_VOC:  if (!last.voc_ok) return false; *v = last.voc; snprintf(buf, n, "%d", (int)lroundf(last.voc)); return true;
    case MI_NOX:  if (!last.nox_ok) return false; *v = last.nox; snprintf(buf, n, "%d", (int)lroundf(last.nox)); return true;
    case MI_TEMP: if (!last.rht_ok) return false; *v = last.temp; fmt_1dp(tmp, sizeof tmp, last.temp); snprintf(buf, n, "%s \xC2\xB0" "C", tmp); return true;
    case MI_HUM:  if (!last.rht_ok) return false; *v = last.humid; snprintf(buf, n, "%d %%", (int)lroundf(last.humid)); return true;
    default: return false;
    }
}

static void show_info(metric_id_t m)
{
    if (info_sheet || m >= MI_COUNT) return;
    const metric_info_t *mi = &infos[m];

    info_sheet = box(lv_scr_act());
    lv_obj_set_size(info_sheet, SCR_W, SCR_H);
    lv_obj_set_style_bg_opa(info_sheet, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(info_sheet, lv_color_black(), 0);
    lv_obj_add_flag(info_sheet, LV_OBJ_FLAG_CLICKABLE);   // swallow taps; tap outside closes
    lv_obj_add_event_cb(info_sheet, info_close_cb, LV_EVENT_CLICKED, NULL);

    int w = SCR_W - 2 * 16;
    lv_obj_t *c = box(info_sheet);
    lv_obj_set_width(c, w);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);            // taps on the sheet don't close it
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(UI_COL_CARD), 0);
    lv_obj_set_style_radius(c, 22, 0);
    lv_obj_set_style_pad_all(c, 24, 0);
    lv_obj_set_style_pad_row(c, 14, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);

    int iw = w - 48;
    lv_obj_t *t = label(c, &lv_font_montserrat_26, UI_COL_TEXT, mi->title);
    lv_obj_set_width(t, iw);

    char buf[40];
    float v = 0;
    int cur = -1;   // level of the current reading, highlighted in the list
    if (current_text(m, buf, sizeof buf, &v)) {
        uint8_t lv = level_of(m, v);
        cur = lv;
        lv_obj_t *row = box(c);
        lv_obj_set_size(row, iw, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        label(row, &lv_font_montserrat_20, UI_COL_TEXT, buf);
        lv_obj_t *p = pill(row);
        pill_set(p, level_word(m, v), level_colors[lv]);
    }

    lv_obj_t *d = label(c, &lv_font_montserrat_18, UI_COL_TEXT_2, mi->description);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(d, 5, 0);
    lv_obj_set_width(d, iw);

    // Particles: every size with its own level
    if (m == MI_PM && have_last && last.pm_ok) {
        const float vals[PM_SIZES] = { last.pm1, last.pm25, last.pm4, last.pm10 };
        lv_obj_t *row = box(c);
        lv_obj_set_size(row, iw, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        for (int i = 0; i < PM_SIZES; i++) {
            lv_obj_t *cell = box(row);
            lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(cell, 4, 0);
            lv_obj_t *head = box(cell);
            lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(head, 6, 0);
            dot_set(level_dot(head), true, pm_level(i, vals[i]));
            label(head, &lv_font_montserrat_14, UI_COL_TEXT_3, pm_names[i]);
            char vb[16];
            fmt_pm(vb, sizeof vb, vals[i]);
            label(cell, &lv_font_montserrat_22, UI_COL_TEXT, vb);
        }
    }

    lv_obj_t *sep = box(c);
    lv_obj_set_size(sep, iw, 1);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(UI_COL_LINE), 0);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = box(c);
        lv_obj_set_size(row, iw, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_t *dot = box(row);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(level_colors[i]), 0);
        bool dim = cur >= 0 && cur != i;
        if (dim) lv_obj_set_style_bg_opa(dot, LV_OPA_40, 0);
        lv_obj_t *l = label(row, &lv_font_montserrat_16, dim ? UI_COL_TEXT_3 : UI_COL_TEXT, mi->ranges[i]);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_flex_grow(l, 1);
    }

    lv_obj_t *btn = box(c);
    lv_obj_set_size(btn, iw, 56);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_LINE), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_TEXT_3), LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, info_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = label(btn, &lv_font_montserrat_18, UI_COL_TEXT, "Close");
    lv_obj_center(bl);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    int y = 52;
    int hero_h = 180, co2_h = 172, pm_h = 116;
    int tile_h = (SCR_H - MARGIN - y - hero_h - co2_h - pm_h - 4 * GAP) / 2 - GAP / 2;
    int tile_w = (SCR_W - 2 * MARGIN - GAP) / 2;

    create_top_bar(scr);
    create_hero(scr, y, hero_h);            y += hero_h + GAP;
    create_co2(scr, y, co2_h);              y += co2_h + GAP;
    create_pm(scr, y, pm_h);                y += pm_h + GAP;
    create_tile(&t_voc, scr, MARGIN, y, tile_w, tile_h, MI_VOC, "VOC", "");
    create_tile(&t_nox, scr, MARGIN + tile_w + GAP, y, tile_w, tile_h, MI_NOX, "NOx", "");
    y += tile_h + GAP;
    create_tile(&t_temp, scr, MARGIN, y, tile_w, tile_h, MI_TEMP, "Temperature", "\xC2\xB0" "C");
    create_tile(&t_hum, scr, MARGIN + tile_w + GAP, y, tile_w, tile_h, MI_HUM, "Humidity", "%");

    ui_set_sensor_state(UI_SENSOR_STARTING);
}

void ui_set_sensor_state(ui_sensor_state_t st)
{
    sensor_state = st;
    static const char *txt[] = { "Starting sensor", "Live", "Sensor offline" };
    static const uint32_t col[] = { UI_COL_FAIR, UI_COL_GOOD, UI_COL_BAD };
    lv_label_set_text(state_lbl, txt[st]);
    lv_obj_set_style_bg_color(state_dot, lv_color_hex(col[st]), 0);
    state_realign();
    if (st == UI_SENSOR_OFFLINE) {
        hero_set(-1, UI_COL_BAD, "No data", "Can't read the air sensor. Check its cable; readings resume automatically.");
    } else if (st == UI_SENSOR_STARTING) {
        hero_set(-1, UI_COL_TEXT_3, "Starting", "Sensor is warming up. Readings appear in a few seconds.");
    }
}

static void update_hero(const air_sample_t *s)
{
    if (s->fire) {
        hero_set(100, UI_COL_BAD, "Smoke!",
                 "Smoke-like particles and gases detected together. Check for fire.");
        lv_label_set_text(hero_score, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(hero_score, lv_color_hex(UI_COL_BAD), 0);
        lv_obj_add_flag(hero_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(hero_card, 3, 0);
        hero_shown_score = -1;   // re-animate when the score returns
        return;
    }
    lv_obj_set_style_text_color(hero_score, lv_color_hex(UI_COL_TEXT), 0);

    // Worst sub-score among the pollutants that currently have a valid reading
    int score = 101;
    metric_id_t worst = MI_CO2;
    bool dust = false;   // worst is PM10 rather than PM2.5
    struct { bool ok; metric_id_t m; const scale_t *sc; float v; } in[] = {
        { s->co2_ok, MI_CO2, &scales[MI_CO2], s->co2 },
        { s->pm_ok,  MI_PM,  &scales[MI_PM],  s->pm25 },
        { s->pm_ok,  MI_PM,  &pm10_scale,     s->pm10 },
        { s->voc_ok, MI_VOC, &scales[MI_VOC], s->voc },
        { s->nox_ok, MI_NOX, &scales[MI_NOX], s->nox },
    };
    for (size_t i = 0; i < sizeof in / sizeof in[0]; i++) {
        if (!in[i].ok) continue;
        int sc = score_on(in[i].sc, in[i].v);
        if (sc < score) { score = sc; worst = in[i].m; dust = in[i].sc == &pm10_scale; }
    }
    if (score > 100) {
        hero_set(-1, UI_COL_TEXT_3, "Starting", "Sensor is warming up. Readings appear in a few seconds.");
        return;
    }

    const char *status;
    uint32_t color;
    if (score >= 90)      { status = "Excellent"; color = UI_COL_GOOD; }
    else if (score >= 75) { status = "Good";      color = UI_COL_GOOD; }
    else if (score >= 50) { status = "Moderate";  color = UI_COL_FAIR; }
    else if (score >= 25) { status = "Poor";      color = UI_COL_POOR; }
    else                  { status = "Unhealthy"; color = UI_COL_BAD; }

    const char *advice;
    if (score >= 75) {
        advice = "Fresh air. Nothing to do.";
    } else {
        bool urgent = score < 50;
        switch (worst) {
        case MI_CO2: advice = urgent ? "CO2 is high. Open a window now."
                                     : "CO2 is building up. Consider airing the room."; break;
        case MI_PM:
            if (dust) advice = urgent ? "Lots of dust in the air. Air the room after cleaning."
                                      : "Dust in the air. Vacuuming, sweeping or sanding?";
            else      advice = urgent ? "Lots of particles in the air. Cooking, smoke or dust?"
                                      : "Particles are elevated. Cooking or candles nearby?";
            break;
        case MI_VOC: advice = urgent ? "Strong chemical odours. Air the room."
                                     : "Chemical odours detected. Cleaning, paint or perfume?"; break;
        default:     advice = urgent ? "Combustion gases detected. Check stove and candles."
                                     : "Some combustion gases. Gas stove or candles?"; break;
        }
    }
    hero_set(score, color, status, advice);
}

void ui_update(const air_sample_t *s)
{
    char buf[16];
    last = *s;
    have_last = true;
    if (sensor_state != UI_SENSOR_OK) ui_set_sensor_state(UI_SENSOR_OK);

    // CO2
    if (s->co2_ok) {
        uint8_t lv = level_of(MI_CO2, s->co2);
        lv_label_set_text_fmt(co2_val, "%d", s->co2);
        pill_set(co2_pill, level_word(MI_CO2, s->co2), level_colors[lv]);
        co2_ser->color = lv_color_hex(level_colors[lv]);

        uint32_t now = lv_tick_get();
        if (!history_started || lv_tick_elaps(last_history_ms) >= UI_HISTORY_STEP_MS) {
            history_started = true;
            last_history_ms = now;
            ui_history_push(s->co2);
        } else {
            lv_chart_refresh(co2_chart);
        }
        co2_trend_update(s->co2);
    } else {
        lv_label_set_text(co2_val, "--");
        pill_set(co2_pill, "Warming up", UI_COL_TEXT_3);
        lv_label_set_text(co2_trend, "");
    }

    // Particles
    if (s->pm_ok) {
        uint8_t lv = level_of(MI_PM, s->pm25);
        fmt_pm(buf, sizeof buf, s->pm25); lv_label_set_text(pm25_val, buf);
        fmt_pm(buf, sizeof buf, s->pm1);  lv_label_set_text(pm1_val, buf);
        fmt_pm(buf, sizeof buf, s->pm4);  lv_label_set_text(pm4_val, buf);
        fmt_pm(buf, sizeof buf, s->pm10); lv_label_set_text(pm10_val, buf);
        dot_set(pm_dot[PM_1], true, pm_level(PM_1, s->pm1));
        dot_set(pm_dot[PM_4], true, pm_level(PM_4, s->pm4));
        dot_set(pm_dot[PM_10], true, pm_level(PM_10, s->pm10));
        pill_set(pm_pill, level_word(MI_PM, s->pm25), level_colors[lv]);
    } else {
        lv_label_set_text(pm25_val, "--");
        lv_label_set_text(pm1_val, "--");
        lv_label_set_text(pm4_val, "--");
        lv_label_set_text(pm10_val, "--");
        dot_set(pm_dot[PM_1], false, 0);
        dot_set(pm_dot[PM_4], false, 0);
        dot_set(pm_dot[PM_10], false, 0);
        pill_set(pm_pill, "Warming up", UI_COL_TEXT_3);
    }

    // Tiles
    snprintf(buf, sizeof buf, "%d", (int)lroundf(s->voc));
    tile_set(&t_voc, MI_VOC, s->voc_ok, s->voc, buf);
    snprintf(buf, sizeof buf, "%d", (int)lroundf(s->nox));
    tile_set(&t_nox, MI_NOX, s->nox_ok, s->nox, buf);
    fmt_1dp(buf, sizeof buf, s->temp);
    tile_set(&t_temp, MI_TEMP, s->rht_ok, s->temp, buf);
    snprintf(buf, sizeof buf, "%d", (int)lroundf(s->humid));
    tile_set(&t_hum, MI_HUM, s->rht_ok, s->humid, buf);

    update_hero(s);
}
