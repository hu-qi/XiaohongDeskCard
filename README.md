# XiaohongDeskCard · 小鸿智能桌牌

基于 **小鸿 AI（HiSilicon WS63 / OpenHarmony LiteOS）** 原装 **240×240 TFT** 的开源智能电子桌牌项目。

> **最新状态（2026-11-19）**：开发者已在 DevEco Studio 以 HarmonyOS SDK 6.0.2(22) 完成 Debug HAP 构建、签名、安装并在 HarmonyOS 7 真机启动；**WS63 固件尚未完成实际 SDK 构建，BLE/SLE/Wi-Fi 与板卡通信尚未实测**。调试签名仅保留本地，不提交密码或证书路径。详见 [Gate A 实测记录](docs/HAP_GATE_A_REPORT.md) 与 [签名安全说明](docs/HARMONY_SIGNING_SECURITY.md)。

## 首版目标

- 显示：姓名、职位、组织、模板；支持中文、断电恢复。
- 通信：**BLE / SLE / Wi‑Fi 同优先级分别验证**，不要求首版三链路同时连接。
- 设备：共用 Desk Core、Desk Protocol、LVGL 页面，运输层可插拔。
- 客户端：优先 HarmonyOS ArkTS App；后续接入元服务、微信小程序、Web。
- 硬件：首先完全使用小鸿 AI 原机。电子纸与定制 PCB 属于后续产品化方向。

## 设计原则

1. **一套数据模型，多种无线入口**：BLE GATT、SLE SSAP、Wi‑Fi HTTP 统一转换为 Desk Command。
2. **显示和通信解耦**：无线回调不直接操作 LVGL；通过事件队列交给 UI 任务更新。
3. **能力检测与降级**：HarmonyOS 客户端先检测 NearLink API 和硬件能力；不支持则用 BLE / Wi‑Fi。
4. **安全优先**：未绑定设备不可写入；鉴权、输入校验、分片完整性校验、重放防护必须进入验收。
5. **分阶段验证共存**：WS63 具备三模硬件不等于固件可无约束同时运行；单链路通后再测共存与切换。
6. **不冒充已验证**：官方样例、项目计划和真实小鸿硬件实测结果严格分开记录。

## 文档

- [产品范围与 MVP](docs/PRD.md)
- [技术架构](docs/ARCHITECTURE.md)
- [统一桌牌协议草案](docs/PROTOCOL.md)
- [首轮硬件验证矩阵](docs/VALIDATION.md)

## 官方参考

- [小鸿 AI 文档与原机规格](https://xiaohong.atomgit.com/docs)
- [小鸿 AI SLE UART 样例](https://xiaohong.atomgit.com/docs-en.html)（samples/13_sle_uart；原样例为两块小鸿板间的 Server/Client 通信）
- [HarmonyOS NearLink Kit](https://developer.huawei.com/consumer/cn/sdk/nearlink-kit/)
- [HarmonyOS 星闪开发准备](https://developer.huawei.com/consumer/cn/doc/HarmonyOS-Guides/nearlink-preparations-guide)

## 开发顺序

P0-A：原装屏静态桌牌 → P0-B：统一命令与配置保存 → P0-C：BLE → P0-D：SLE → P0-E：Wi‑Fi → P0-F：HarmonyOS App 三链路联动与能力降级 → P0-G：共存、断网、重启与安全测试。

> 各通信链路平行优先，以上顺序仅为降低集成风险的建议执行路径；不以 Wi‑Fi 跑通作为 BLE/SLE 开发前提。

## 许可证

待确认并在导入第三方 SDK/上游代码前审查其许可证。当前仓库不声明继承任何上游源码授权。

## 电脑端先行验证（无需小鸿实机）

本项目现已提供**真实 C99 Desk Core + 桌面 Web 模拟器**。桌面模拟器不是 WS63 固件，也不是物理 BLE / 星闪模拟器；它在电脑本机用不同的传输分片策略验证相同的桌牌协议。

macOS / Linux（预装 CMake、C 编译器、Python 3）：

```bash
git clone https://github.com/hu-qi/XiaohongDeskCard.git
cd XiaohongDeskCard
git switch feat/desk-core-triradio-baseline

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure

python3 tools/simulator/desk_sim_server.py
# 浏览器打开 http://127.0.0.1:8765
```

模拟界面可以直接输入姓名、职位、组织，选择 Wi-Fi HTTP、BLE MOCK、SLE MOCK，并预览 240×240 屏幕。配置保存在 `build/desk-sim-state.bin`，重启模拟器可恢复。

开发接口示例（仅限**本机 127.0.0.1**）：

```bash
curl -X PUT http://127.0.0.1:8765/api/v1/desk/content \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer dev-only-token' \
  -d '{"requestId":123,"expectedRevision":0,"content":{"name":"张三","title":"开发者","organization":"OpenHarmony 社区","theme":"classic"}}'
```

注意：如已在模拟器中修改过内容，`expectedRevision` 应从 `GET /api/v1/desk/status` 的响应读取；同一 `requestId` 重试具有有限的幂等语义。

开发令牌仅用于本机 Mock 环境，可以通过 `DESK_SIM_TOKEN` 覆盖；**不要将它部署在公网或作为真实 WS63 设备的安全实现**。

### 模拟与硬件的边界

| 项目 | 当前 |
|---|---|
| Desk Core 与协议 C99 单测 | 已实现；GitHub Actions 编译测试 |
| 本机 HTTP 控制 / 240×240 Web 预览 | 已实现 |
| BLE/SLE 分片与完整帧交付到 Desk Core | 已实现模拟路径 |
| 真正的 BLE GATT / SLE SSAP 无线通讯 | 尚未实现 |
| LVGL / ST7789 实机绘制、WS63 SDK 构建 | 尚未实现 |
| HarmonyOS Stage App 工程及 ArkUI 编辑页 | **用户本机 DevEco Debug HAP 编译、真机安装启动已通过**；仍需专项链路功能测试 |
| BLE GATT / SLE SSAP 原生客户端 | SDK 适配原型已提交；未接入实际 WS63 服务 |

**硬件实测放最后，但 SDK 编译检查应提前。** 详细说明见 [验证矩阵](docs/VALIDATION.md) 与 [桌面协议](docs/WIRE_V1.md)。

## HarmonyOS 原生 App（源码基线）

已在 [apps/harmony](apps/harmony) 中创建 Stage 模型工程结构、EntryAbility、ArkUI 桌牌编辑界面：

- 扫描到的 BLE / 星闪设备以列表呈现，“连接并发送”按钮走真实无线链路：连接 → 发现 Desk Card 服务 → 写入 Desk Wire 帧 → 等待设备业务 ACK；
- Wi-Fi HTTP 按钮使用 NetworkKit 对明确指定的设备发送真实 HTTP 请求；
- `RadioDeskTransport.ets` 把 `BleGattLink` / `SleSsapLink` 接到统一 `DeskTransport` 接口；`DeskRouter` 负责 SLE → BLE → Wi-Fi 的回退，且只在写入前失败时回退；
- `DeskWireFrame.ets` 提供与 C99 Desk Core 匹配的帧布局及应用层 BEGIN/CHUNK/COMMIT 分包设计；
- 工程配置对齐 HarmonyOS `6.0.2(22)`，使用从 API 13 起可用的 `@kit.NearLinkKit`；后续可迁移到新版 `@kit.ConnectivityKit`。

详见 [HarmonyOS 集成与边界说明](docs/HARMONY_APP.md)。

**当前 GitHub CI 检查的是宿主机 C99/HTTP 逻辑与 Stage 文件结构，并非 HAP 编译成功，也不代表 BLE/SLE 物理链路已联通。**

## 2026-11-19 · 三模 ACK、发现和实机验收关卡

本分支继续推进了以下**源代码实现（尚未进行 DevEco/WS63 编译）**：

1. **Desk Wire ACK v1**：C99 编解码 16 字节业务 ACK（requestId、revision、resultCode、CRC32）；BLE/SLE 软件模拟器已生成并校验真实 C 编码的 ACK。
2. **HarmonyOS ACK 处理**：`DeskAckFrame.ets`、`DeskAckWaiter.ets`；BLE GATT 与 SLE SSAP 原型订阅通知，在接收到对应 ACK 之后才认为业务执行完成。无 ACK 不得标记成功。
3. **发现与权限**：`DeskDeviceDiscovery.ets` 基于官方 BLE 和 NearLink Kit 的扫描接口；`DeskPermissions.ets` 按通道申请用户授权；ArkUI 界面新增设备扫描入口。需在目标 SDK/真机编译验证。
4. **协议服务表**：声明了项目专用的**暂定 UUID**、广播名称；设备端必须实现对应 GATT/SSAP 服务才能连接。
5. **C99 重试修复**：防止同一 requestId 但不同 expectedRevision 被误当成幂等重试；如果落盘成功而界面渲染失败，重试可以再次尝试渲染。

详细材料：[Desk Wire v1](docs/WIRE_V1.md) · [HarmonyOS App](docs/HARMONY_APP.md) · [实机验收关卡](docs/HARDWARE_GATE.md)。

研发闸口：[#7 无实机 DevEco/WS63 SDK 编译验收](https://github.com/hu-qi/XiaohongDeskCard/issues/7) → [#8 小鸿 AI + HarmonyOS 7 真机验收](https://github.com/hu-qi/XiaohongDeskCard/issues/8)。

用户已有小鸿 AI 和 HarmonyOS 7 手机，**当前无需接线、烧录或提供手机日志**；只有 Gate A/B 通过后，才要求操作真机。系统为 HarmonyOS 7 不等于手机一定具备 NearLink 硬件能力，须实测。

⚠️ GitHub Actions 的成功仅证明宿主机 C99、HTTP 模拟器与 Stage 文件结构检查通过，**不等价于 ArkTS/HAP 编译或无线连接成功**。

## 2026-11-19 · WS63 原屏集成基线（无需实机）

本轮新增**可以在普通电脑端编译测试的设备侧模块**：

- `firmware/core/desk_dispatcher`：BLE/SLE/Wi-Fi 公用有界 FIFO；只有 LVGL owner task 执行 Core / 渲染 / ACK
- `firmware/display/desk_view_lvgl`：原装 240×240 TFT 的 LVGL v8 桌牌界面，姓名/职位/组织/主题
- `firmware/platform/ws63/desk_ws63_glue`：SDK 无关的 LVGL、NV 存储、UI 事件、ACK 任务回调适配接口
- `tools/ws63_preflight.py`：检查 OpenHarmony 7 上游源码目录/关键文件并打印 vendor commit，不编译或烧录
- 主机 LVGL/NV 测试替身及测试文件：不依赖开发板，也不伪称实际屏幕已显示

完整集成步骤见 [WS63 适配指南](docs/WS63_INTEGRATION.md)。当前 GitHub Actions 运行 C99、模拟器、HarmonyOS 结构及 Node 22 路由策略测试。这些宿主机测试通过不代表 `hb build` 或 DevEco HAP 编译成功；硬件仅在 [Issue #8](https://github.com/hu-qi/XiaohongDeskCard/issues/8) 开始时需要用户参与。

## 2026-11-19 · BLE/SLE 设备端接收闭环（无实机）

新增 `firmware/core/desk_radio_ingress.[ch]`：一个 BLE/SLE 连接一份独立会话，在绑定授权后接收 BEGIN/CHUNK/COMMIT 分片，经同一队列交给 Desk Core；只认可对应连接和请求 ID 的业务 ACK。收到旧连接 ACK、未经授权写入、分片破损、队列满时均不会误报成功。

桌面模拟器已改为复用这一真实 C99 接收链路，而不是单独拼包后直接调用核心。对应 `tests/test_radio_ingress.c` 与 `tests/test_simulator.py` 覆盖软件闭环。

[无线服务端接入说明](docs/RADIO_INGRESS.md) · [WS63 集成](docs/WS63_INTEGRATION.md)。

GitHub Actions 在宿主机运行 CTest、Node 22 路由测试及 UBSan 编译检查；这些结果**不是 DevEco/HAP/WS63 SDK 编译，不代表 BLE/SLE 射频实测**。真机仍仅在 [Issue #8](https://github.com/hu-qi/XiaohongDeskCard/issues/8) 阶段才需要用户参与。


## Flash/NV 与撤销授权的新增验证

C99 `firmware/core/desk_nv_store.[ch]` 已实现双槽版本记录、CRC32 和最后提交标记，配套 248 个写入断电位置的故障模拟。`desk_ws63_glue` 现在强制要求设备提供 `authorize_on_ui`，避免已经排队的无线命令在权限撤销后继续落盘。

`test_nv_glue.c` 额外测试了模拟 SLE 写入 → NV → LVGL → ACK → 冷启动恢复，以及撤销绑定后拒绝更新。

[存储设计与 HAL 对接](docs/NV_STORAGE.md) · [Issue #7 SDK 编译待办](https://github.com/hu-qi/XiaohongDeskCard/issues/7)。

**注意**：这不等于真实小鸿 Flash/HarmonyOS HAP 构建或掉电实测，仍不需要用户操作实机。


## 真实 LVGL 编译与 WS63 构建暂存目录

已新增 GitHub Actions 双版本验证：**真实 LVGL 8.3.11 和 LVGL 9.3.0** 分别与 Desk View 源码链接，运行 240×240 无屏幕绘制测试。此前仅有 LVGL stub 的兼容风险已有所降低；小鸿原固件 LVGL 版本仍待实际源码检查。

无需 SDK 也可以生成待审查的 WS63 C 源码暂存目录：

```bash
python3 tools/ws63_overlay.py --output build/ws63-overlay
```

生成 8 个 C 源文件、8 个头文件、`BUILD.gn.fragment` 和 SHA-256 manifest；不会自动修改任何上游工程，更不会烧录硬件。

[真实 LVGL 8/9 验证说明](docs/LVGL_SDK_COMPAT.md) · [SDK 集成与 Overlay 指南](docs/WS63_INTEGRATION.md)。

**仍未完成 WS63 SDK `hb build`；HarmonyOS App 已成功完成首次 DevEco 编译并启动。无需现在烧录 WS63 或进行三模联调。**


## 设备端 Wi-Fi HTTP 统一业务链路（软件基线）

已新增 `desk_http_json` 与 `desk_wifi_ingress` C99 模块：实现限制 1024B 的严格 JSON 解码（Unicode/整数/字段白名单）、Wi-Fi 更新入同一个 UI Dispatcher、业务 ACK 校验，以及 GET 状态/PUT 结果的 JSON 编码。

桌面模拟器的 Wi-Fi 更新也已经改用真实 C99 JSON 与统一队列；BLE/SLE/Wi-Fi 三条模拟通道都经过同一 Desk Core。额外加入 **30,000 组异常 Wi-Fi JSON 输入测试**。

[Wi-Fi HTTP 接口与安全要求](docs/WIFI_HTTP_INTEGRATION.md) · [协议 v1](docs/PROTOCOL.md)。

**仍没有 WS63 实际 HTTP Server 或板卡联调结果；App 已在 HarmonyOS 7 真机启动，目前不需要再次运行或烧录。**


## 2026-11-19 · UI 状态快照与三模响应 mailbox

`GET status`、`PUT content` 现共享唯一 Desk Dispatcher 顺序执行；`desk_reply_mailbox` 将 UI 任务产生的 BLE/SLE 业务 ACK 和 Wi-Fi 状态快照转交通信工作线程。已覆盖请求顺序、权限撤销、显示失败后持久化与实际显示的差异、响应队列满时的背压/未知结果。

桌面模拟器的 Wi-Fi GET/PUT 和 BLE/SLE 业务 ACK 都已经经过同一 C99 UI 队列及 mailbox。新模块纳入 WS63 待审查源码暂存包（**11 C + 11 H**）。

[UI GET/SET 与回传队列规范](docs/UI_MESSAGE_BRIDGE.md) · [SDK 编译 Gate](https://github.com/hu-qi/XiaohongDeskCard/issues/7)。

**仍未进行真实 WS63 SDK 固件构建；HarmonyOS HAP 首次编译及运行已由用户验证。**


## 2026-11-19 · 三模回传连接隔离

`desk_reply_mailbox` 新增 `desk_reply_take_for(channel, peer_token)` 与 `desk_reply_discard_peer(channel, peer_token)`。在 BLE、SLE 和 Wi-Fi 各自拥有工作线程的情况下，任何线程都不会取走别的连接的业务 ACK；断连后只清理自身的待发结果。

`tests/test_reply_demux.c` 新增错误连接、乱序消费、环形队列回绕、跨通道相同 token、断连多消息清理及敏感字段清空验证。桌面模拟器已改为定向领取 ACK / GET 状态。

[三模 UI 与通信任务模型](docs/UI_MESSAGE_BRIDGE.md) · [SDK 实际编译门槛](https://github.com/hu-qi/XiaohongDeskCard/issues/7)。

**这是 C99 软件隔离验证，不代表 WS63 BLE/SLE/HTTP 工作线程已编译或 HAP 已运行。**


## 2026-11-19 · CMSIS-RTOS2 无实机编译与首轮 HAP Gate A

已新增 `firmware/platform/ws63/desk_ws63_cmsis.[ch]`：用于给原小鸿 LvglTask 投递 4 字节显示事件，检测上游消息队列大小，并在队列满/唤醒丢失时由已有 UI 任务的周期轮询恢复。该模块已通过 C99 Host 模拟和 ARM **官方 CMSIS-RTOS2 v2.3.0 头文件**编译检查；没有链接实际 WS63 SDK。

WS63 待审查源码包更新为 **12 C + 12 H**，包含 CMSIS 事件适配层；仍需绑定真正的 `g_disp_event_qid`、Flash、无线服务和 HTTP 服务。

- [CMSIS 与小鸿 LvglTask 接入规范](docs/WS63_CMSIS_BRIDGE.md)
- [首次 DevEco HAP 仅编译 Gate A 指南](docs/HAP_BUILD_HANDOFF.md)

**2026-11-19 开发者已完成首次 DevEco Debug HAP 编译和手机运行。下一关键工作是 WS63 SDK 固件构建与三模板端服务端集成。**


## Gate A 实际完成：HarmonyOS 7 手机运行

2026-11-19 开发者反馈 Debug HAP 已在 HarmonyOS 7 手机上完成安装与正常启动。根据真实 DevEco 诊断修复了 Module 启动窗口字段/颜色资源、ArkTS `throw` 类型以及 `Promise.catch` 回调参数类型。产品与 Debug 签名绑定通过**仅存在于开发者本地的配置**完成，**私有密码/证书路径没有进入公开分支**。

- [Gate A 真实构建与启动记录](docs/HAP_GATE_A_REPORT.md)
- [签名凭据安全及 Git 提交检查](docs/HARMONY_SIGNING_SECURITY.md)
- [后续 WS63 SDK 编译 Gate](https://github.com/hu-qi/XiaohongDeskCard/issues/7)

**下一阶段仍需在小鸿 SDK 中实现并编译真实 BLE GATT、SLE SSAP、Wi-Fi HTTP、Flash/NV 与原屏 LVGL 服务。手机启动成功不能替代板端联调。**


## Gate B 最新进度：GN 安全集成与全量实库编译

已完成 [GN 上游集成指南](docs/WS63_GN_INSTALL.md) 与 `tools/ws63_gn_integrate.py`：

- 首次真正有 WS63 SDK 时，可根据完整 vendor commit SHA 检查 `static_library("xiaohong")` 和 `config_lvgl_enable`，默认**只读展示 diff**。
- 手动审核后显式使用 `--apply`，才会将完整 12 C + 12 H 复制到上游 `src/deskcard` 并原子修改 `BUILD.gn`；有未提交的上游改动、源码包 SHA-256 不匹配或 GN 结构不符合预期都会拒绝。
- CI 额外以实际 **LVGL 8.3.11 和 LVGL 9.3.0 + ARM CMSIS 官方头文件**分别编译整个 12 C 软件包并执行 GUI/NV/GET/ACK 全栈宿主机集成测试。

**尚无 WS63 真实 SDK `hb build`、GATT/SSAP/HTTP 板级服务或可烧录产物。** App 已由用户在 HarmonyOS 7 真机独立启动过，但无需现在再次运行、烧录或联调。



## Gate B · 真实 WS63 SDK 版本探测（只读）

现有可移植 12 C + 12 H 已在真实 LVGL 8.3/9.3 与 ARM CMSIS 头文件下完成宿主机编译，但**当前缺少真实第一代小鸿 OpenHarmony SDK 源码树，尚未执行 hb build**。

新增 `tools/ws63_sdk_probe.py` 用于核实实际 vendor commit、GN/LVGL/显示事件、HTTP/Flash/无线候选源码，不修改 SDK、不会采集密码或复制源码正文：

```bash
# 若电脑已有 OpenHarmony 源码树
python3 tools/ws63_sdk_probe.py \
  --root /path/to/openharmony \
  --output build/ws63-sdk-probe.json
```

提供探测报告前请自行检查隐私信息。完整命令和用途见 [Gate B SDK 证据采集指南](docs/WS63_SDK_EVIDENCE.md)。目前**不需要编译固件、重新运行手机 App 或真机联调**。
