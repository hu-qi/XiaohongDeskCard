# HarmonyOS 客户端：ArkTS 集成状态

当前包含 **可供接入 DevEco Stage 工程的源文件**，但尚未创建完整 HAP 工程，未进行 ArkTS 编译或手机实测。

```text
src/main/ets/desk/
  DeskModels.ets          # 桌牌数据结构、Wi-Fi/BLE/SLE 传输契约
  WifiHttpTransport.ets   # NetworkKit HTTP 适配
  NearLinkCapability.ets  # API 26+ 星闪能力检测
```

## 当前范围

- `WifiHttpTransport` 为基于官方 `@kit.NetworkKit` HttpRequest API 的源码初稿，可调用模拟器 `GET /api/v1/desk/status` 和 `PUT /api/v1/desk/content`。需要在 DevEco 工程中声明网络权限并进行 HAP 编译。
- `NearLinkCapability` 基于 `@kit.ConnectivityKit`，其中 `manager.isNearLinkSupported()` 从 API 26.0.0 开始提供。**仅当 DevEco SDK 和目标终端支持时才能编译/调用**，不同系统版本需要单独做依赖隔离/编译配置。
- BLE GATT 与 SLE SSAP 目前尚未实现真实手机传输。桌面模拟器的 BLE/SLE 只是二进制分包/重组模拟，切勿称之为真机 BLE/SLE 已联通。
- 由于桌面模拟器只绑定 `127.0.0.1`，实体手机无法直接访问 Mac 上的该地址；后续对接时应使用受保护的局域网测试代理或显式开发模式，不建议默认暴露 Mock 服务。

## 下一步

1. 创建正式 DevEco Stage 应用工程（AppScope、entry、build-profile、模块配置与 ArkUI 编辑页）。
2. 完成 BLE GATT 服务发现、分包、特征写入及通知 ACK。
3. 完成 SLE 扫描、SSAP 服务发现、分包写入和通知 ACK，并使用 NearLink 能力查询做功能降级。
4. 接入桌面模拟器自动化测试及 DevEco 编译检查。
5. 最后一轮才用小鸿 AI 实机测试三模通信与 LVGL 原屏。

## 参考官方文档

- https://developer.huawei.com/consumer/cn/doc/harmonyos-references/js-apis-http/
- https://developer.huawei.com/consumer/cn/doc/harmonyos-references/js-apis-bluetooth-ble/
- https://developer.huawei.com/consumer/cn/doc/harmonyos-references/js-apis-nearlink-ssap/
- https://developer.huawei.com/consumer/cn/doc/harmonyos-references/js-apis-nearlink-manager/
