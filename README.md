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
