/**
 * Standalone LVGL v8/v9 host test configuration.
 * NOT the memory/font/peripheral configuration used by WS63 firmware.
 */
#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (128U * 1024U)
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1
#define LV_USE_ASSERT_OBJ 1
#define LV_USE_LABEL 1
#define LV_USE_THEME_DEFAULT 1
#define LV_DPI_DEF 130
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_DEMO_WIDGETS 0
#define LV_BUILD_EXAMPLES 0

#endif
