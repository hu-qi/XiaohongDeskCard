#ifndef XIAOHONG_DESK_VIEW_LVGL_H
#define XIAOHONG_DESK_VIEW_LVGL_H

#include "desk_core.h"
#include "lvgl.h"

/*
 * LVGL v8/v9 UI adapter for Xiaohong AI's original 240x240 ST7789 TFT.
 * MUST only be called from LvglTask after LVGL/display initialization.
 * This module does not initialize SPI, LVGL, the screen driver or RTOS.
 */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *header;
    lv_obj_t *org_label;
    lv_obj_t *name_label;
    lv_obj_t *title_label;
    lv_obj_t *footer_label;
} desk_lvgl_view_t;

/* Create an overlay on parent; NULL parent selects LVGL active screen. */
int desk_lvgl_view_create(desk_lvgl_view_t *view, lv_obj_t *parent);

/* Desk Core render hook (user points to desk_lvgl_view_t). */
int desk_lvgl_view_render(void *user, const desk_content_t *content);

/* Delete the overlay; caller must be on LvglTask. */
void desk_lvgl_view_destroy(desk_lvgl_view_t *view);

#endif
