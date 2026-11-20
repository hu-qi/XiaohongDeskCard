# Gate B · 小鸿 WS63 上游 GN 实际源码集成（仅工程，不涉及烧录）

状态：**完整可移植 12 C + 12 H 已通过实库 LVGL/CMSIS 联合编译，GN 版本锁定安装工具已通过临时 Git 仓库测试。** 但截至 2026-11-19，未访问或构建真实小鸿 OpenHarmony vendor SDK、未形成可刷机固件。本指南中的后续命令只是可复现操作，不表示已执行。

## 依据与适用范围

小鸿官方开发文档展示了 `vendor/atomgit/xiaohong/xiaohong/BUILD.gn` 中 `static_library("xiaohong")` 及 `if (config_lvgl_enable)`，显示相关源文件在 LVGL 条件块下编译。资料：https://xiaohong.atomgit.com/docs-en.html 。

注意：这里使用的是第一代 WS63 / 原装 240×240 TFT，不是 SE、KB 或其它 WS63 分支。不同源码版本的 GN/RTOS/屏幕驱动可能变化，安装工具严格拒绝不符合预期的文件结构。

## 无需 SDK 的验证（现有 CI 已通过）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GitHub Actions 另外将全部 **12 个 C 源文件**分别与真实 LVGL 8.3.11 和 LVGL 9.3.0 以及 ARM 官方 CMSIS-RTOS2 头文件一起构建。每个 LVGL 版本各执行 2 项测试；其中完整集成测试实际调用：

- 初始化 240×240 无窗口 LVGL 显示对象；
- WS63 glue 初始化、CMSIS 事件入队及 UI 任务轮询；
- Wi-Fi 命令经统一 Dispatcher 更新 LVGL；
- 模拟 NV 保存、ACK 进入固定邮箱并按会话取走；
- GET 状态读取及所有权撤销后的拒绝。

**这些回调仍是宿主机替身**：不包含 WS63 Flash 驱动、SPI、真实 GATT/SSAP 服务端或 HTTP/TLS 的编译与运行。

## 真正有 SDK 后的操作顺序（当前不要求用户执行）

假设 `OHOS` 为已经通过官方 manifest 同步的完整源码树，`DESK` 为本项目仓库。应分别固定它们的 SHA；不要依赖浮动的 `main` 分支。

```bash
cd "$DESK"
# 工具只复制当前已跟踪的 C/H 文件到新的临时目录。
python3 tools/ws63_overlay.py --output build/ws63-overlay

# 在原始 SDK 上先只检查布局和关键标识，不修改上游任何文件。
python3 tools/ws63_preflight.py --root "$OHOS" --json

# 记录 vendor 根目录的真实 Git HEAD。
UPSTREAM_SHA="$(git -C "$OHOS/vendor/atomgit/xiaohong" rev-parse HEAD)"

# 必须先查看拟议 GN diff、文件清单及上游 version；默认是只读！
python3 tools/ws63_gn_integrate.py \
  --root "$OHOS" --overlay "$DESK/build/ws63-overlay" \
  --expected-upstream-commit "$UPSTREAM_SHA"
```

如果以上只读预览拒绝执行，说明源码布局、上游 commit 或 24 个 staged 文件不符合当前实现。**不要通过移除 SHA 检查或硬改文件掩盖差异**。

只有完成上游人工审核，且真实板级绑定工作已经就绪，才执行：

```bash
python3 tools/ws63_gn_integrate.py \
  --root "$OHOS" --overlay "$DESK/build/ws63-overlay" \
  --expected-upstream-commit "$UPSTREAM_SHA" --apply
```

工具行为：

- 上游 Git SHA 与传入 40 位 SHA 不一致直接拒绝；
- 源码包逐文件 SHA-256 与 manifest 对不上直接拒绝；
- 检查唯一的 `static_library("xiaohong")` 和唯一的 `if (config_lvgl_enable)`，拒绝无匹配/多匹配/不完整注入；
- 默认只显示建议 diff，不修改 SDK；
- `--apply` 时若相关上游文件有本地修改或 `src/deskcard` 已存在但不匹配，直接拒绝；
- 先原子放入 `src/deskcard`，最后原子替换 `BUILD.gn`；重复运行相同版本不再添加重复的 `sources`；
- 不会修改 SDK 的任何其余文件（Flash 分区、业务入口、显示枚举、网络 server、ROM image packager）。

## 至少还缺的实际板级绑定

**安装源码包并不意味着固件可以运行。** GN 只是将 SDK 无关的 C99 逻辑编入小鸿主库。仍须由上游相应版本的实际头文件与回调实现：

1. `LvglTask` 注册桌牌更新事件，连接现有 `g_disp_event_qid`；主屏支持中文 glyph，显存/字体资源符合 WS63 RAM/Flash。
2. CMSIS mutex 与唤醒机制正确管理 Dispatcher 和回复队列；唯一 UI 任务负责 LVGL。
3. 真实 NV/Flash HAL，按板载 W25Q128 的擦写对齐/扇区大小审查双槽区域，保护原有文件系统、资源与 OTA 分区。
4. BLE GATT Server 与 SLE SSAP Server 的完整服务注册、认证与 ACK 特征/属性通知。
5. Wi-Fi HTTP Server 实现 GET/PUT 与可信鉴权、TLS/受控网络、会话/异步 ACK 和限流。
6. 实际 `hb set`、`hb build -f`、产物 SHA 和 RAM/Flash 占用记录。即使编译成功，也**不能**据此宣称广播、屏幕或掉电恢复功能已在实机通过。

## 是否需要你操作设备？

- **当前不需要**小鸿 AI 烧录，也不需要重新运行 HarmonyOS App；手机端首次 HAP 编译和启动已验证。
- 当上游 SDK 真正准备好可进行交叉编译时，需要用户在受支持的 OpenHarmony 编译环境执行 `hb build` 并回传**脱敏**构建日志；不要分享签名私钥、设备令牌、Wi-Fi 密码。
- 只有出现可追溯且具备回滚方案的固件镜像后，才安排 BLE/SLE/Wi-Fi 真机联调。
