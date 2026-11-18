#include "desk_view_lvgl.h"
#include <stddef.h>
#include <string.h>

/* Dimensions are fixed for the ORIGINAL Xiaohong AI 240x240 TFT. */
#define DESK_SCREEN 240

static void position_label(lv_obj_t *label, int16_t x, int16_t y, int16_t width)
{
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static int failed(desk_lvgl_view_t *view)
{
    desk_lvgl_view_destroy(view);
    return 0;
}

int desk_lvgl_view_create(desk_lvgl_view_t *view, lv_obj_t *parent)
{
    if (view == NULL) return 0;
    memset(view, 0, sizeof(*view));
    if (parent == NULL) parent = lv_scr_act();
    if (parent == NULL) return 0;

    view->root = lv_obj_create(parent);
    if (view->root == NULL) return 0;
    lv_obj_set_size(view->root, DESK_SCREEN, DESK_SCREEN);
    lv_obj_set_pos(view->root, 0, 0);
    lv_obj_set_style_pad_all(view->root, 0, 0);
    lv_obj_set_style_border_width(view->root, 0, 0);
    lv_obj_set_style_radius(view->root, 0, 0);
    lv_obj_set_style_bg_opa(view->root, LV_OPA_COVER, 0);

    view->header = lv_obj_create(view->root);
    if (view->header == NULL) return failed(view);
    lv_obj_set_size(view->header, DESK_SCREEN, 42);
    lv_obj_set_pos(view->header, 0, 0);
    lv_obj_set_style_pad_all(view->header, 0, 0);
    lv_obj_set_style_border_width(view->header, 0, 0);
    lv_obj_set_style_radius(view->header, 0, 0);

    view->org_label = lv_label_create(view->header);
    if (view->org_label == NULL) return failed(view);
    position_label(view->org_label, 8, 12, 224);

    view->name_label = lv_label_create(view->root);
    if (view->name_label == NULL) return failed(view);
    position_label(view->name_label, 10, 91, 220);

    view->title_label = lv_label_create(view->root);
    if (view->title_label == NULL) return failed(view);
    position_label(view->title_label, 10, 153, 220);

    view->footer_label = lv_label_create(view->root);
    if (view->footer_label == NULL) return failed(view);
    position_label(view->footer_label, 8, 218, 224);
    lv_label_set_text(view->footer_label, "XIAOHONG DESK");
    return 1;
}

int desk_lvgl_view_render(void *user, const desk_content_t *content)
{
    desk_lvgl_view_t *view = (desk_lvgl_view_t *)user;
    if (view == NULL || content == NULL || view->root == NULL ||
        !desk_content_valid(content)) return 0;

    /* Names and titles are UTF-8. The configured LVGL font MUST contain the
     * required Chinese glyphs; fallback/selection is a separate task. */
    const uint32_t accent =
        strcmp(content->theme, "modern") == 0 ? 0x173A60u :
        strcmp(content->theme, "minimal") == 0 ? 0xFFFFFFu : 0xB32942u;
    const uint32_t header_text =
        strcmp(content->theme, "minimal") == 0 ? 0xB32942u : 0xFFFFFFu;

    lv_obj_set_style_bg_color(view->root, lv_color_hex(0xFAFAF8u), 0);
    lv_obj_set_style_bg_color(view->header, lv_color_hex(accent), 0);
    lv_obj_set_style_text_color(view->org_label, lv_color_hex(header_text), 0);
    lv_obj_set_style_text_color(view->name_label, lv_color_hex(0x172B45u), 0);
    lv_obj_set_style_text_color(view->title_label, lv_color_hex(0x5D6876u), 0);
    lv_obj_set_style_text_color(view->footer_label, lv_color_hex(0xB32942u), 0);

    lv_label_set_text(view->org_label, content->organization);
    lv_label_set_text(view->name_label, content->name);
    lv_label_set_text(view->title_label, content->title);
    return 1;
}

void desk_lvgl_view_destroy(desk_lvgl_view_t *view)
{
    if (view == NULL) return;
    if (view->root != NULL) lv_obj_del(view->root);
    memset(view, 0, sizeof(*view));
}
