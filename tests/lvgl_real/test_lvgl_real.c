#include "desk_view_lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Real LVGL v8 and v9 have different display driver APIs. Both run with
 * a headless flush callback so this test needs NO TFT or desktop window. */
#if LVGL_VERSION_MAJOR >= 9
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}
#else
static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors)
{
    (void)area;
    (void)colors;
    lv_disp_flush_ready(drv);
}
#endif

int main(void)
{
    lv_init();
#if LVGL_VERSION_MAJOR >= 9
    static uint8_t draw_pixels[240u * 12u * 2u];
    lv_display_t *display = lv_display_create(240, 240);
    assert(display != NULL);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, draw_pixels, NULL, sizeof(draw_pixels),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_obj_t *screen = lv_screen_active();
#else
    static lv_color_t draw_pixels[240u * 12u];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, draw_pixels, NULL, 240u * 12u);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_t *display = lv_disp_drv_register(&disp_drv);
    assert(display != NULL);
    lv_obj_t *screen = lv_scr_act();
#endif
    assert(LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9);

    desk_lvgl_view_t view = {0};
    assert(desk_lvgl_view_create(&view, screen));
    lv_obj_update_layout(view.root);
    assert(lv_obj_get_width(view.root) == 240);
    assert(lv_obj_get_height(view.root) == 240);
    assert(lv_obj_get_height(view.header) == 42);

    desk_content_t first = {0};
    strcpy(first.name, "张三");
    strcpy(first.title, "技术负责人");
    strcpy(first.organization, "OpenHarmony 社区");
    strcpy(first.theme, "classic");

    assert(desk_lvgl_view_render(&view, &first));
    assert(strcmp(lv_label_get_text(view.name_label), "张三") == 0);
    assert(strcmp(lv_label_get_text(view.title_label), "技术负责人") == 0);
    assert(strcmp(lv_label_get_text(view.org_label), "OpenHarmony 社区") == 0);
    lv_obj_update_layout(view.root);

    strcpy(first.theme, "modern");
    assert(desk_lvgl_view_render(&view, &first));
    strcpy(first.theme, "minimal");
    assert(desk_lvgl_view_render(&view, &first));

    /* A real LVGL invalidation + flush cycle, but not ST7789 SPI transfer. */
    lv_timer_handler();
    desk_lvgl_view_destroy(&view);
    assert(view.root == NULL);

    printf("PASS: compiled actual LVGL %d.%d.%d + 240x240 Desk View, update, render\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return 0;
}
