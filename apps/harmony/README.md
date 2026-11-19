# XiaohongDeskCard · HarmonyOS Stage App

这个目录现在包含 Stage 工程骨架、入口 Ability、ArkUI 桌牌页面，以及三模传输的适配代码基线。

> 重要：本仓库当前的 GitHub Actions 只运行 C99、HTTP 模拟器和 Stage **结构检查**；尚未执行 DevEco / ArkTS 编译，尚未生成 HAP，也没有在 HarmonyOS 模拟器或手机上安装运行。

## 快速入口

- [ArkTS 与 Stage 集成指南](../../docs/HARMONY_APP.md)
- [桌牌 App 主页](entry/src/main/ets/pages/Index.ets)
- [BLE GATT 真实 SDK 写入原型](entry/src/main/ets/desk/BleGattLink.ets)
- [SLE SSAP 真实 SDK 写入原型](entry/src/main/ets/desk/SleSsapLink.ets)
- [Wi-Fi HTTP 客户端](entry/src/main/ets/desk/WifiHttpTransport.ets)
- [统一 C 帧对应的 ArkTS 编码](entry/src/main/ets/desk/DeskWireFrame.ets)

## 在 DevEco 打开

使用 DevEco Studio 打开本目录 `apps/harmony`（不要打开仓库顶层作为 HarmonyOS 工程）。当前 SDK 基线配置为 `6.0.2(22)`，沿用本仓库主人已有的 HarmonyOS 工程风格；最终构建可用性需要本地 DevEco 检查。

页面上的 **BLE Mock / SLE Mock** 是明确标注的纯 ArkTS 模拟按钮；真正的 GATT / SSAP 客户端还需要服务 UUID、设备端广播和配对授权等条件，尚未接入 UI。

特别注意：首版用 `@kit.NearLinkKit`，它的相关接口自 API 13 起即有提供，**不要求为第一版直接切换到 API 26**。请参阅 [官方 V13 SSAP](https://developer.huawei.com/consumer/cn/doc/harmonyos-references-V13/nearlink-ssap-V13/)。


## 首次真正构建：Gate A（现在可执行）

请按 [HAP 编译交接指南](../../docs/HAP_BUILD_HANDOFF.md) 在 DevEco Studio 中仅 **Sync → Build Hap(s)**。目标是获取 DevEco/SDK 版本、源码 SHA 以及真实 ArkTS 编译日志。**暂不运行、不安装到手机、不访问真实桌牌，也不需要固件编译**。

本机 GitHub CI 的 Stage 检查只是文件结构与资源检查，不能代替这一 Gate。
