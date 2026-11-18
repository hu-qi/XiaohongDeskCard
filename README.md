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
| HarmonyOS ArkTS 原生 App | 待开发 |

**硬件实测放最后，但 SDK 编译检查应提前。** 详细说明见 [验证矩阵](docs/VALIDATION.md) 与 [桌面协议](docs/WIRE_V1.md)。
