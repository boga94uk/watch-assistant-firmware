#include "ui_galaxy.h"

#include <math.h>
#include <string.h>

#ifndef GALAXY_CORE
#define GALAXY_CORE 180
#endif
#ifndef GALAXY_DUST
#define GALAXY_DUST 100
#endif
#define GALAXY_N (GALAXY_CORE + GALAXY_DUST)

typedef struct {
    float orbit_a, orbit_b, cos_tilt, sin_tilt, angle, speed;
    uint32_t rgb;
    float brightness, brightness_speed;
    uint8_t dust; /* 1 = extra 1px white/gray star */
    int16_t x, y;
} star_t;

typedef struct {
    star_t s[GALAXY_N];
    int w, h, cx, cy;
    float radius_px;
    lv_obj_t *obj;
    lv_timer_t *timer;
    bool paused;
} galaxy_t;

static galaxy_t s_g;

static uint32_t star_rgb(int t)
{
    if (t < 40) {
        return 0xF4F1EA;
    }
    if (t < 65) {
        return 0xB8D7FF;
    }
    if (t < 78) {
        return 0xFFE9A8;
    }
    if (t < 86) {
        return 0xFFC266;
    }
    if (t < 96) {
        return 0xC8C4BC;
    }
    if (t < 99) {
        return 0xE8D4FF;
    }
    return 0xFFB8A0;
}

static void galaxy_step(galaxy_t *g)
{
    for (int i = 0; i < GALAXY_N; i++) {
        star_t *s = &g->s[i];
        s->angle += s->speed;
        if (s->angle > 6.2832f) {
            s->angle -= 6.2832f;
        }
        if (s->brightness_speed > 0) {
            s->brightness += s->brightness_speed;
            if (s->brightness > 6.2832f) {
                s->brightness -= 6.2832f;
            }
        }
        float ca = cosf(s->angle);
        float sa = sinf(s->angle);
        float ox = s->orbit_a * ca;
        float oy = s->orbit_b * sa;
        float x3 = ox * s->cos_tilt - oy * s->sin_tilt;
        float y3 = ox * s->sin_tilt + oy * s->cos_tilt;
        s->x = (int16_t)(g->cx + x3 * g->radius_px);
        s->y = (int16_t)(g->cy + y3 * g->radius_px);
    }
}

static void plot_px(lv_draw_ctx_t *ctx, int x, int y, lv_color_t col, lv_opa_t opa)
{
    const lv_area_t *clip = ctx->clip_area;
    if (x < clip->x1 || x > clip->x2 || y < clip->y1 || y > clip->y2) {
        return;
    }
    const lv_area_t *ba = ctx->buf_area;
    if (!ba || x < ba->x1 || x > ba->x2 || y < ba->y1 || y > ba->y2) {
        return;
    }
    lv_color_t *buf = ctx->buf;
    if (!buf) {
        return;
    }
    uint32_t w = (uint32_t)(ba->x2 - ba->x1 + 1);
    lv_color_t *px = buf + (uint32_t)(y - ba->y1) * w + (uint32_t)(x - ba->x1);
    if (opa >= LV_OPA_COVER) {
        *px = col;
    } else if (opa > LV_OPA_MIN) {
        *px = lv_color_mix(col, *px, opa);
    }
}

static void galaxy_draw(lv_event_t *e)
{
    galaxy_t *g = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    if (!g || !ctx || !obj) {
        return;
    }

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;

    const lv_coord_t ox = obj->coords.x1;
    const lv_coord_t oy = obj->coords.y1;
    const lv_color_t white = lv_color_hex(0xF6F4EE);
    const lv_color_t gray = lv_color_hex(0x9A9A9A);

    for (int i = 0; i < GALAXY_N; i++) {
        const star_t *s = &g->s[i];
        if (s->x < 1 || s->y < 36 || s->x >= g->w - 2 || s->y >= g->h - 90) {
            continue;
        }
        float br = 0.35f;
        if (s->brightness_speed > 0) {
            br = 0.22f + 0.78f * (1.0f + cosf(s->brightness)) * 0.5f;
        }
        if (s->dust) {
            int opa = (int)(40 + br * 215.0f);
            if (opa > 255) {
                opa = 255;
            }
            plot_px(ctx, ox + s->x, oy + s->y, s->rgb ? gray : white, (lv_opa_t)opa);
            continue;
        }
        uint32_t rgb = s->rgb;
        uint8_t r = (uint8_t)(((rgb >> 16) & 0xFF) * br);
        uint8_t gg = (uint8_t)(((rgb >> 8) & 0xFF) * br);
        uint8_t b = (uint8_t)((rgb & 0xFF) * br);
        dsc.bg_color = lv_color_make(r, gg, b);
        int sz = (s->orbit_a > 0.85f) ? 2 : 1;
        lv_area_t a = {
            .x1 = ox + s->x,
            .y1 = oy + s->y,
            .x2 = ox + s->x + sz,
            .y2 = oy + s->y + sz,
        };
        lv_draw_rect(ctx, &dsc, &a);
    }
}

static void galaxy_timer(lv_timer_t *t)
{
    galaxy_t *g = t->user_data;
    if (!g || g->paused || !g->obj) {
        return;
    }
    galaxy_step(g);
    lv_obj_invalidate(g->obj);
}

static void init_star(star_t *s, bool dust)
{
    float dist = 0.12f + (lv_rand(0, 1200) / 1000.0f);
    float ecc = (lv_rand(0, 100) < 70) ? (0.4f + lv_rand(0, 500) / 1000.0f) : (lv_rand(0, 400) / 1000.0f);
    s->orbit_a = dist;
    s->orbit_b = dist * (1.0f - ecc * 0.85f);
    float tilt = lv_rand(0, 3600) / 572.96f;
    s->cos_tilt = cosf(tilt);
    s->sin_tilt = sinf(tilt);
    s->angle = lv_rand(0, 3600) / 572.96f;
    s->speed = 0.010f / (dist + 0.2f) + lv_rand(0, 80) / 60000.0f;
    s->brightness = lv_rand(0, 628) / 100.0f;
    s->dust = dust ? 1 : 0;
    if (dust) {
        s->rgb = (lv_rand(0, 100) < 42) ? 1 : 0; /* 1 = gray, 0 = white */
        s->brightness_speed = (lv_rand(0, 100) < 70) ? (lv_rand(8, 28) / 10000.0f) : 0;
        s->speed *= 0.85f;
    } else {
        s->rgb = star_rgb(lv_rand(0, 100));
        s->brightness_speed = (lv_rand(0, 100) < 55) ? (lv_rand(6, 22) / 10000.0f) : 0;
    }
}

lv_obj_t *ui_galaxy_create(lv_obj_t *parent, int w, int h)
{
    memset(&s_g, 0, sizeof(s_g));
    s_g.w = w;
    s_g.h = h;
    s_g.cx = w / 2;
    s_g.cy = h / 2 - 10;
    s_g.radius_px = (w < h ? w : h) * 0.55f;

    for (int i = 0; i < GALAXY_CORE; i++) {
        init_star(&s_g.s[i], false);
    }
    for (int i = GALAXY_CORE; i < GALAXY_N; i++) {
        init_star(&s_g.s[i], true);
    }
    galaxy_step(&s_g);

    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x050508), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(obj, galaxy_draw, LV_EVENT_DRAW_MAIN, &s_g);
    s_g.obj = obj;
    s_g.timer = lv_timer_create(galaxy_timer, 50, &s_g);
    return obj;
}

void ui_galaxy_set_visible(bool on)
{
    if (!s_g.obj) {
        return;
    }
    if (on) {
        lv_obj_clear_flag(s_g.obj, LV_OBJ_FLAG_HIDDEN);
        s_g.paused = false;
    } else {
        lv_obj_add_flag(s_g.obj, LV_OBJ_FLAG_HIDDEN);
        s_g.paused = true;
    }
}

void ui_galaxy_pause(bool pause)
{
    s_g.paused = pause;
}
