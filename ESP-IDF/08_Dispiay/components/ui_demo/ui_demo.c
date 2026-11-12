#include "ui_demo.h"

#include <inttypes.h>
#include "lvgl.h"
#include "esp_timer.h"

#define DEMO_RING_COUNT 3

typedef struct {
    lv_obj_t *arc;
    lv_obj_t *rings[DEMO_RING_COUNT];
    lv_obj_t *value;
    lv_obj_t *uptime;
    lv_obj_t *status_dot;
    lv_timer_t *timer;
    int64_t start_us;
} demo_state_t;

static demo_state_t s_demo;

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                               const lv_font_t *font, uint32_t color,
                               lv_align_t align, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, align, 0, y);
    return label;
}

/* 动画回调同样在 LVGL 任务中执行，改变直径后保持圆环中心不变。 */
static void set_ring_diameter(void *object, int32_t diameter)
{
    lv_obj_t *ring = object;
    lv_obj_set_size(ring, diameter, diameter);
    lv_obj_center(ring);
}

static esp_err_t create_color_rings(lv_obj_t *screen)
{
    /*
     * 红、绿、蓝三圈的直径范围互不交叠，始终位于外侧仪表盘之内。
     * 最小圆环也为中心文字留出空间；透明背景仅绘制 4 像素宽的圆边框。
     */
    static const uint32_t colors[DEMO_RING_COUNT] = {0xFF5A67, 0x31D989, 0x4285FF};
    static const int32_t min_diameters[DEMO_RING_COUNT] = {120, 156, 192};
    static const uint32_t durations[DEMO_RING_COUNT] = {1100, 1400, 1700};

    for (int i = 0; i < DEMO_RING_COUNT; ++i) {
        lv_obj_t *ring = lv_obj_create(screen);
        s_demo.rings[i] = ring;
        lv_obj_remove_style_all(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 4, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        set_ring_diameter(ring, min_diameters[i]);

        /* 平滑放大再缩小，错开启动时间及周期，形成持续的彩色呼吸效果。 */
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, ring);
        lv_anim_set_exec_cb(&animation, set_ring_diameter);
        lv_anim_set_values(&animation, min_diameters[i], min_diameters[i] + 20);
        lv_anim_set_time(&animation, durations[i]);
        lv_anim_set_delay(&animation, i * 180);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_set_playback_time(&animation, durations[i]);
        lv_anim_set_playback_delay(&animation, 100);
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_repeat_delay(&animation, 100);
        if (lv_anim_start(&animation) == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

/* LVGL 定时器在 lv_timer_handler 内执行，与刷新共享同一任务，无并发访问。 */
static void update_cb(lv_timer_t *timer)
{
    demo_state_t *state = timer->user_data;
    const uint32_t seconds = (uint32_t)((esp_timer_get_time() - state->start_us) / 1000000LL);
    const uint32_t phase = seconds % 200;
    const uint32_t value = phase <= 100 ? phase : 200 - phase;
    lv_arc_set_value(state->arc, value);
    lv_label_set_text_fmt(state->value, "%" PRIu32 "%%", value);
    lv_label_set_text_fmt(state->uptime, "UP %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
                          seconds / 3600, seconds / 60 % 60, seconds % 60);
    lv_obj_set_style_bg_color(state->status_dot,
                              lv_color_hex(seconds & 1 ? 0x36D9A0 : 0x157552), 0);
}

esp_err_t ui_demo_create(void)
{
    if (s_demo.timer != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lv_obj_t *screen = lv_scr_act();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x050B14), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 圆心约 (180,180)，仪表盘外径 320，距圆形玻璃边缘保留约 20 像素。 */
    s_demo.arc = lv_arc_create(screen);
    lv_obj_remove_style_all(s_demo.arc);
    lv_obj_set_size(s_demo.arc, 320, 320);
    lv_obj_center(s_demo.arc);
    lv_arc_set_rotation(s_demo.arc, 135);
    lv_arc_set_bg_angles(s_demo.arc, 0, 270);
    lv_arc_set_range(s_demo.arc, 0, 100);
    lv_arc_set_value(s_demo.arc, 0);
    lv_obj_set_style_arc_width(s_demo.arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_demo.arc, lv_color_hex(0x183044), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_demo.arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_demo.arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_demo.arc, lv_color_hex(0x36D9A0), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_demo.arc, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(s_demo.arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 先创建圆环，再创建文字，使文字始终显示在动画之上。 */
    esp_err_t err = create_color_rings(screen);
    if (err != ESP_OK) {
        return err;
    }

    /* 标题和底部信息移到彩色圆环之外，中心字号缩小以留出伸缩空间。 */
    create_label(screen, "HXESP3636", &lv_font_montserrat_20, 0xE9F4FF, LV_ALIGN_TOP_MID, 34);
    create_label(screen, "LVGL 8.3", &lv_font_montserrat_14, 0x7994AC, LV_ALIGN_TOP_MID, 56);
    s_demo.value = create_label(screen, "0%", &lv_font_montserrat_32, 0xFFFFFF,
                                 LV_ALIGN_CENTER, -10);
    create_label(screen, "PROGRESS", &lv_font_montserrat_14, 0x7994AC,
                  LV_ALIGN_CENTER, 23);
    s_demo.uptime = create_label(screen, "UP 00:00:00", &lv_font_montserrat_16, 0xC4D8E9,
                                  LV_ALIGN_CENTER, 122);

    s_demo.status_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(s_demo.status_dot);
    lv_obj_set_size(s_demo.status_dot, 10, 10);
    lv_obj_set_style_radius(s_demo.status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_demo.status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_demo.status_dot, lv_color_hex(0x36D9A0), 0);
    lv_obj_align(s_demo.status_dot, LV_ALIGN_CENTER, -50, 143);
    create_label(screen, "RUNNING", &lv_font_montserrat_14, 0x36D9A0,
                  LV_ALIGN_CENTER, 143);

    s_demo.start_us = esp_timer_get_time();
    s_demo.timer = lv_timer_create(update_cb, 1000, &s_demo);
    return s_demo.timer != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}
