/* Copyright (c) 2026 DankBuild - RadarAura. RadarAura Build Licence 1.0 - see LICENSE.md */
/* Render the dashboard (main/ui.c) on a PC into PNG screenshots. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "ui.h"

#define W 480
#define H 800
static uint8_t fb[W * H * 3];
static lv_color_t buf[W * H];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px)
{
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) {
            uint32_t c = lv_color_to32(*px++);
            uint8_t *o = &fb[(y * W + x) * 3];
            o[0] = (c >> 16) & 0xFF; o[1] = (c >> 8) & 0xFF; o[2] = c & 0xFF;
        }
    lv_disp_flush_ready(d);
}

static void run_ms(int ms)
{
    for (int t = 0; t < ms; t += 10) { lv_tick_inc(10); lv_timer_handler(); }
}

static void shot(const char *name)
{
    run_ms(1000);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    char path[256];
    snprintf(path, sizeof path, "%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
    printf("wrote %s\n", path);
}

static air_sample_t sample(int co2, float pm25, float voc, float nox, float t, float h)
{
    air_sample_t s = { true, true, true, true, true, (uint16_t)co2,
                       pm25 * 0.7f, pm25, pm25 * 1.1f, pm25 * 1.2f, t, h, voc, nox, false };
    return s;
}

static void click_first_card(int x, int y);

int main(void)
{
    lv_init();
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, buf, NULL, W * H);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = W; drv.ver_res = H; drv.flush_cb = flush; drv.draw_buf = &db;
    drv.full_refresh = 0;
    lv_disp_drv_register(&drv);

    ui_create();
    shot("01_starting");

    /* warm-up: PM/CO2 valid, VOC/NOx not yet */
    air_sample_t s = sample(612, 4.2f, 0, 0, 21.4f, 44);
    s.voc_ok = s.nox_ok = false;
    ui_update(&s);
    shot("02_warmup");

    /* good air with an hour of history: slow drift */
    for (int i = 0; i < 120; i++) ui_history_push(560 + (i * 7) % 60 + i / 3);
    s = sample(640, 3.8f, 96, 1, 21.6f, 43);
    ui_update(&s);
    shot("03_good");

    /* stale room: CO2 climbing over the last hour, slightly dry */
    for (int i = 0; i < 120; i++) ui_history_push(700 + i * 7 + (i % 5) * 6);
    s = sample(1540, 9.5f, 140, 3, 23.9f, 27);
    s.pm1 = 6.1f; s.pm4 = 10.2f; s.pm10 = 11.0f;
    ui_update(&s);
    shot("04_stale");

    /* smoke */
    s = sample(980, 182.0f, 410, 160, 24.8f, 48);
    s.fire = true;
    ui_update(&s);
    shot("05_smoke");

    /* info sheet for CO2 (tap the CO2 card) */
    s = sample(1540, 9.5f, 140, 3, 23.9f, 27);
    ui_update(&s);
    click_first_card(240, 340);
    shot("06_info_co2");

    /* close the sheet (tap the dimmed area), then mixed particle levels + PM sheet */
    click_first_card(240, 40);
    run_ms(100);
    s = sample(820, 28.0f, 110, 2, 22.4f, 45);
    s.pm1 = 14.2f; s.pm4 = 31.5f; s.pm10 = 64.0f;
    ui_update(&s);
    shot("07_pm_card");
    click_first_card(240, 470);
    shot("08_info_pm");

    /* dusty room: PM2.5 fine, coarse dust high -> PM10 lowers the score */
    click_first_card(240, 40);
    run_ms(100);
    s = sample(620, 8.0f, 90, 2, 21.8f, 42);
    s.pm1 = 4.0f; s.pm4 = 30.0f; s.pm10 = 120.0f;
    ui_update(&s);
    run_ms(1500);
    shot("09_dust");
    return 0;
}

/* Fake a tap by sending CLICKED to the object under the point */
static void click_first_card(int x, int y)
{
    lv_point_t p = { x, y };
    lv_obj_t *o = lv_indev_search_obj(lv_scr_act(), &p);
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) o = lv_obj_get_parent(o);
    if (o) lv_event_send(o, LV_EVENT_CLICKED, NULL);
}
