#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t screen = {0};
static unsigned live = 0;

lv_obj_t *lv_scr_act(void) { return &screen; }
unsigned lvgl_stub_objects_alive(void) { return live; }

lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    assert(parent != NULL);
    lv_obj_t *obj = (lv_obj_t *)calloc(1, sizeof(*obj));
    if (obj == NULL) return NULL;
    obj->parent = parent;
    obj->next_sibling = parent->first_child;
    parent->first_child = obj;
    live++;
    return obj;
}
lv_obj_t *lv_label_create(lv_obj_t *parent) { return lv_obj_create(parent); }

void lv_obj_set_width(lv_obj_t *obj, int32_t w) { obj->w = (int16_t)w; }
void lv_obj_set_size(lv_obj_t *obj, int32_t w, int32_t h)
{
    obj->w = (int16_t)w;
    obj->h = (int16_t)h;
}
void lv_obj_set_pos(lv_obj_t *obj, int32_t x, int32_t y)
{
    obj->x = (int16_t)x;
    obj->y = (int16_t)y;
}
void lv_obj_set_style_pad_all(lv_obj_t *obj, int32_t pad, int32_t selector)
{ (void)obj; (void)pad; (void)selector; }
void lv_obj_set_style_border_width(lv_obj_t *obj, int32_t w, int32_t selector)
{ (void)obj; (void)w; (void)selector; }
void lv_obj_set_style_radius(lv_obj_t *obj, int32_t r, int32_t selector)
{ (void)obj; (void)r; (void)selector; }
void lv_obj_set_style_bg_opa(lv_obj_t *obj, uint8_t opa, int32_t selector)
{ (void)obj; (void)opa; (void)selector; }
void lv_obj_set_style_bg_color(lv_obj_t *obj, lv_color_t c, int32_t selector)
{ (void)selector; obj->bg = c; }
void lv_obj_set_style_text_color(lv_obj_t *obj, lv_color_t c, int32_t selector)
{ (void)selector; obj->fg = c; }
void lv_obj_set_style_text_align(lv_obj_t *obj, lv_text_align_t a, int32_t selector)
{ (void)obj; (void)a; (void)selector; }
void lv_label_set_long_mode(lv_obj_t *obj, lv_label_long_mode_t mode)
{ (void)obj; (void)mode; }
void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    assert(text != NULL);
    snprintf(obj->text, sizeof(obj->text), "%s", text);
}
lv_color_t lv_color_hex(uint32_t hex) { return hex; }

void lv_obj_del(lv_obj_t *obj)
{
    if (obj == NULL || obj == &screen) return;
    while (obj->first_child != NULL)
        lv_obj_del(obj->first_child);
    if (obj->parent != NULL) {
        lv_obj_t **node = &obj->parent->first_child;
        while (*node != NULL && *node != obj)
            node = &(*node)->next_sibling;
        if (*node == obj) *node = obj->next_sibling;
    }
    assert(live > 0);
    live--;
    free(obj);
}
