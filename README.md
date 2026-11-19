# XiaohongDeskCard · 小鸿智能桌牌

基于 **小鸿 AI（HiSilicon WS63 / OpenHarmony LiteOS）** 原装 **240×240 TFT** 的开源智能电子桌牌项目。

> 当前状态：**架构与验证基线（尚未形成可烧录固件）**。首个目标是无需外接电子纸，在小鸿原装屏幕上验证 **BLE GATT / 星闪 SLE / Wi‑Fi HTTP 三种链路均能更新同一个桌牌页面**。

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
| HarmonyOS Stage App 工程及 ArkUI 编辑页 | 已提交源码；未在 DevEco 编译 |
| BLE GATT / SLE SSAP 原生客户端 | SDK 适配原型已提交；未接入实际 WS63 服务 |

**硬件实测放最后，但 SDK 编译检查应提前。** 详细说明见 [验证矩阵](docs/VALIDATION.md) 与 [桌面协议](docs/WIRE_V1.md)。

## HarmonyOS 原生 App（源码基线）

已在 [apps/harmony](apps/harmony) 中创建 Stage 模型工程结构、EntryAbility、ArkUI 桌牌编辑界面：

- BLE Mock / SLE Mock 按钮用于 ArkTS 业务预览；
- Wi-Fi HTTP 按钮使用 NetworkKit 对明确指定的测试设备发送真实 HTTP 请求；
- `BleGattLink.ets` 和 `SleSsapLink.ets` 是基于官方 SDK 的无线客户端适配原型，尚无 WS63 服务 UUID / 业务 ACK，未在 DevEco 编译；
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

**尚未完成 OpenHarmony WS63 `hb build` 与 DevEco HAP 编译；用户目前不需要进行真机操作。**


## 设备端 Wi-Fi HTTP 统一业务链路（软件基线）

已新增 `desk_http_json` 与 `desk_wifi_ingress` C99 模块：实现限制 1024B 的严格 JSON 解码（Unicode/整数/字段白名单）、Wi-Fi 更新入同一个 UI Dispatcher、业务 ACK 校验，以及 GET 状态/PUT 结果的 JSON 编码。

桌面模拟器的 Wi-Fi 更新也已经改用真实 C99 JSON 与统一队列；BLE/SLE/Wi-Fi 三条模拟通道都经过同一 Desk Core。额外加入 **30,000 组异常 Wi-Fi JSON 输入测试**。

[Wi-Fi HTTP 接口与安全要求](docs/WIFI_HTTP_INTEGRATION.md) · [协议 v1](docs/PROTOCOL.md)。

**依然没有 WS63 实际 HTTP Server、DevEco HAP 编译或实机结果。现在不需要操作小鸿 AI / HarmonyOS 7 手机。**
