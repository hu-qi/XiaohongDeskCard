# XiaohongDeskCard · HarmonyOS Stage App

这个目录现在包含 Stage 工程骨架、入口 Ability、ArkUI 桌牌页面，以及三模传输的适配代码基线。

> **Gate A 已由开发者在 2026-11-19 实机验证**：使用 DevEco Studio / SDK 6.0.2(22) 成功构建 Debug HAP，并在 HarmonyOS 7 手机上安装启动。仓库 CI 仍仅进行软件、资源与源码检查，无法重现 DevEco 原生编译；WS63 固件及三模通信尚未实测。

## 快速入口

- [ArkTS 与 Stage 集成指南](../../docs/HARMONY_APP.md)
- [桌牌 App 主页](entry/src/main/ets/pages/Index.ets)
- [BLE GATT 真实 SDK 写入原型](entry/src/main/ets/desk/BleGattLink.ets)
- [SLE SSAP 真实 SDK 写入原型](entry/src/main/ets/desk/SleSsapLink.ets)
- [Wi-Fi HTTP 客户端](entry/src/main/ets/desk/WifiHttpTransport.ets)
- [统一 C 帧对应的 ArkTS 编码](entry/src/main/ets/desk/DeskWireFrame.ets)

## 在 DevEco 打开

使用 DevEco Studio 打开本目录 `apps/harmony`（不要打开仓库顶层作为 HarmonyOS 工程）。当前 SDK 基线配置为 `6.0.2(22)`，沿用本仓库主人已有的 HarmonyOS 工程风格；本地 Debug HAP 的首次编译/安装/启动已经验证，但后续分支代码的每次迭代仍需回归构建。

页面上的 **BLE Mock / SLE Mock** 是明确标注的纯 ArkTS 模拟按钮；真正的 GATT / SSAP 客户端还需要服务 UUID、设备端广播和配对授权等条件，尚未接入 UI。

特别注意：首版用 `@kit.NearLinkKit`，它的相关接口自 API 13 起即有提供，**不要求为第一版直接切换到 API 26**。请参阅 [官方 V13 SSAP](https://developer.huawei.com/consumer/cn/doc/harmonyos-references-V13/nearlink-ssap-V13/)。


## Gate A 首次真正构建（2026-11-19 已通过）

开发者已完成 Debug HAP 构建并成功在 HarmonyOS 7 手机上安装启动，详见 [Gate A 记录](../../docs/HAP_GATE_A_REPORT.md)。今后复现只编译时仍可参考 [HAP 操作指南](../../docs/HAP_BUILD_HANDOFF.md)，但暂不需要再次运行 App。

本地调试签名只保留在个人机器中，**禁止把 build-profile.json5 内的证书路径、签名密码推送至 GitHub**。参见 [签名安全规范](../../docs/HARMONY_SIGNING_SECURITY.md)。

本机 GitHub CI 的 Stage 检查只是文件结构与资源检查，不能代替这一 Gate。
