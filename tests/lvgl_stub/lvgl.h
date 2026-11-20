#ifndef XIAOHONG_LVGL_TEST_STUB_H
#define XIAOHONG_LVGL_TEST_STUB_H

/* Host-only compile/test stand-in for the small subset of LVGL v8 API used.
 * It does NOT exercise the ST7789 renderer or embedded font support. */
#include <stddef.h>
#include <stdint.h>

typedef uint32_t lv_color_t;
typedef struct _lv_obj_t {
    struct _lv_obj_t *parent;
    struct _lv_obj_t *first_child;
    struct _lv_obj_t *next_sibling;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    lv_color_t bg;
    lv_color_t fg;
    char text[256];
} lv_obj_t;

/* Minimal font type stub for host tests. */
typedef struct {
    void *dsc;
} lv_font_t;

typedef enum { LV_LABEL_LONG_WRAP = 0 } lv_label_long_mode_t;
typedef enum { LV_TEXT_ALIGN_CENTER = 1 } lv_text_align_t;

#define LV_OPA_COVER 255u

lv_obj_t *lv_scr_act(void);
lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);
void lv_obj_set_width(lv_obj_t *obj, int32_t w);
void lv_obj_set_size(lv_obj_t *obj, int32_t w, int32_t h);
void lv_obj_set_pos(lv_obj_t *obj, int32_t x, int32_t y);
void lv_obj_set_style_pad_all(lv_obj_t *obj, int32_t pad, int32_t selector);
void lv_obj_set_style_border_width(lv_obj_t *obj, int32_t w, int32_t selector);
void lv_obj_set_style_radius(lv_obj_t *obj, int32_t r, int32_t selector);
void lv_obj_set_style_bg_opa(lv_obj_t *obj, uint8_t opa, int32_t selector);
void lv_obj_set_style_bg_color(lv_obj_t *obj, lv_color_t c, int32_t selector);
void lv_obj_set_style_text_color(lv_obj_t *obj, lv_color_t c, int32_t selector);
void lv_obj_set_style_text_font(lv_obj_t *obj, const lv_font_t *font, int32_t selector);
void lv_obj_set_style_text_align(lv_obj_t *obj, lv_text_align_t a, int32_t selector);
void lv_label_set_long_mode(lv_obj_t *obj, lv_label_long_mode_t mode);
void lv_label_set_text(lv_obj_t *obj, const char *text);
void lv_obj_del(lv_obj_t *obj);
lv_color_t lv_color_hex(uint32_t hex);
unsigned lvgl_stub_objects_alive(void);

#endif
