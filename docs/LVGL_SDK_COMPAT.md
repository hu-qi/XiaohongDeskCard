# XiaohongDeskCard · LVGL / WS63 无实机版本兼容验证

状态：**实际 LVGL v8.3.11 与 v9.3.0 的宿主机编译和 headless 测试通过**；小鸿主固件实际使用的 LVGL SHA、GN 配置及 WS63 `hb build` 仍未校验。

## 为什么同时验证 8 和 9

上游小鸿显示资料采用了 `lv_image_create`、`LV_DISPLAY_RENDER_MODE_PARTIAL` 等 LVGL 9 风格的接口。最初的 Desk View 以 LVGL 8 为基线，这存在移植风险。

因此我们现在针对以下两个**固定源码 commit**构建真实 LVGL 静态库：

| 目标 | LVGL 来源 | Commit SHA | 验证项目 |
|---|---|---|---|
| LVGL 8.3.11 | `lvgl/lvgl` tag `v8.3.11` | `74d0a816a440eea53e030c4f1af842a94f7ce3d3` | 8.x API / Desk View |
| LVGL 9.3.0 | `lvgl/lvgl` tag `v9.3.0` | `c033a98afddd65aaafeebea625382a94020fe4a7` | 9.x API / Desk View |

`firmware/display/desk_view_lvgl.c` 根据 `LVGL_VERSION_MAJOR` 选择 `lv_scr_act/lv_screen_active` 和 `lv_obj_del/lv_obj_delete`，并复用通用标签与样式方法。

## 电脑端自己复现（无需小鸿 AI）

```bash
git clone https://github.com/lvgl/lvgl.git /tmp/desk-lvgl
git -C /tmp/desk-lvgl checkout c033a98afddd65aaafeebea625382a94020fe4a7

cmake -S tests/lvgl_real -B build-lvgl9 \
  -DDESK_LVGL_SOURCE_DIR=/tmp/desk-lvgl -DCMAKE_BUILD_TYPE=Debug
cmake --build build-lvgl9 --target test_lvgl_real --parallel 4
ctest --test-dir build-lvgl9 --output-on-failure
```

GitHub Actions 的 `.github/workflows/core-ci.yml` 已包含以上两个版本的 matrix 自动验证，且仍保留原先 C99 / Python / TypeScript 软件层测试。

本机执行的是**真实 LVGL 库**，而不是轻量测试替身；测试使用无屏幕 flush，验证 240×240 对象、主题、设置姓名/职位/组织字符串及 UI 刷新调用。与真正 TFT 显示之间还有 ST7789 驱动、中文字体和任务并发等差异。

## 在 WS63 上仍需确认的内容

1. **实际固件 LVGL 版本与 commit**：以板卡上游 OpenHarmony 源码树为准，不要因为 host v9.3 测试通过就认定 WS63 使用 v9.3。
2. **LCD 与资源**：核对 LCD ST7789S 240×240、横竖屏方向、LVGL 堆内存、颜色深度、实际刷新缓冲区及 GPU 加速选项。
3. **中文字体**：测试中 UTF-8 文本值正确，**不证明 LVGL 默认 Montserrat 字体包含中文**。需要为姓名/职位/单位选择字体和缺字回退，结合 WS63 Flash/RAM 预算编译验证。
4. **线程边界**：只有 LvglTask 可以调用 `desk_lvgl_view_render`。无线回调仅入队，不能跨任务直接操作 LVGL。
5. **实机性能**：帧刷新时长、屏幕乱码/花屏、背光和功耗等仍属最后的设备验收事项。

## 无实机集成 GN 片段

`python3 tools/ws63_overlay.py --output build/ws63-overlay` 可生成待审查的 `src/deskcard/` 目录、`BUILD.gn.fragment` 和逐文件 SHA-256 manifest。

**该工具不下载上游 SDK、不修改第三方 `BUILD.gn`、不执行 `hb build`、不烧录设备**。准备真实 SDK 编译前，需要先运行 `tools/ws63_preflight.py`，再把片段人工审核加入实际 GN target；详细见 [WS63_INTEGRATION.md](WS63_INTEGRATION.md)。

## 尚未达成的结论

- 没有 DevEco Studio 实际编译 HAP 的记录。
- 没有小鸿 OpenHarmony/WS63 SDK 的真实 `hb build` 成功记录。
- 没有实机 BLE/SLE/Wi-Fi 链路或 ST7789 屏幕显示的验收结果。

因此不能将该项软件验证视为 Issue #7 / #8 全部完成。
