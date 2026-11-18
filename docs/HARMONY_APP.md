# HarmonyOS Stage App · 软件先行版本

## 目标与当前边界

本仓库的 `apps/harmony` 已放入**完整的 Stage 工程配置结构、入口 Ability、ArkUI 编辑页及三通道源文件**；但未执行 DevEco Studio / Hvigor / ArkTS 编译，也**不保证首次导入就能直接产出 HAP**。SDK 与图标资源等仍需在实际 DevEco 环境检查。

工程配置以项目现有 [Goodtime 鸿蒙化工程](https://github.com/hu-qi/goodtime/tree/harmonyos) 的 `6.0.2(22)` 构建模型为参考。

### 开发模式

- BLE Mock、SLE Mock：**纯 ArkTS 本地内存模拟**，不触发蓝牙或星闪。
- Wi-Fi HTTP：NetworkKit 的真实网络请求，需要用户输入端点和令牌。后端可选择真实设备或测试服务。
- Mac/Linux 浏览器模拟器：本地 loopback HTTP + 真实 C99 Desk Core；它比 ArkTS 内存 Mock 更接近固件业务行为。
- BLE/SLE 原生适配源码 `BleGattLink.ets` / `SleSsapLink.ets`：调用扫描/连接/写入和通知 SDK，并已补充业务 ACK 解码/超时。现有 UUID **只是拟定值**，WS63 端尚未实现对应服务。真实无线写入按钮尚未启用。

## 工程结构

```text
apps/harmony/
  AppScope/app.json5
  build-profile.json5
  hvigorfile.ts
  hvigor/hvigor-config.json5
  oh-package.json5
  entry/
    build-profile.json5
    hvigorfile.ts
    oh-package.json5
    src/main/
      module.json5
      ets/entryability/EntryAbility.ets
      ets/pages/Index.ets
      ets/desk/
        DeskModels.ets
        MockDeskTransport.ets
        WifiHttpTransport.ets
        NearLinkCapability.ets
        DeskWireFrame.ets
        DeskAckFrame.ets
        DeskAckWaiter.ets
        DeskServiceIds.ets
        DeskDeviceDiscovery.ets
        DeskPermissions.ets
        BleGattLink.ets
        SleSsapLink.ets
      resources/base/profile/main_pages.json
```

## 导入方式

1. 安装与 `6.0.2(22)` 工程兼容的 DevEco Studio 和 HarmonyOS SDK。
2. 在 DevEco Studio 选择 Open Project，打开 **`apps/harmony`**，不是仓库根目录。
3. 首次同步 Hvigor / ohpm，检查 API、运行时权限、资源图标处理以及 ArkTS 严格模式。
4. 配置本机调试签名后，尝试生成 HAP。若工具提示需要升级 SDK，不要直接更新依赖并声称已兼容，应留下错误记录。
5. 在模拟器/手机上运行编辑页，先验证 ArkTS Mock；随后单独验证 Wi-Fi HTTP 到**受控测试端点**。
6. 最后阶段用小鸿实机广播、服务发现、认证、ACK 和显示同步完成 BLE/SLE 真正闭环。

## 星闪 API 版本说明

官方有两套文档：

- `@kit.NearLinkKit` 的 SSAP/manager 从 HarmonyOS **5.0.1(13)** 开始提供。
- 新版 `@kit.ConnectivityKit` 中的 `@ohos.nearlink.ssap` 等接口标注从 **26.0.0** 开始提供。

本分支选择 `@kit.NearLinkKit`，匹配项目 SDK 22 目标，并非星闪一定要求 API 26。更新 SDK 前需要对类型、事件签名和目标硬件支持情况做逐项回归。

参考：
- https://developer.huawei.com/consumer/cn/doc/harmonyos-references-V13/nearlink-ssap-V13/
- https://developer.huawei.com/consumer/cn/doc/harmonyos-references/nearlink-manager/
- https://developer.huawei.com/consumer/cn/doc/harmonyos-references/js-apis-nearlink-ssap/

## 安全说明

`module.json5` 已声明 INTERNET、ACCESS_BLUETOOTH、ACCESS_NEARLINK，但**声明不等于用户授权**。扫描界面源码已通过 `DeskPermissions.request` 接入运行时授权，但实际授权弹窗、拒绝处理和手机 API 行为**仍待 DevEco 编译及真机测试**。

模拟 HTTP 令牌只适用于本机开发。不要在不可信局域网明文发送生产令牌或把 Mock HTTP 服务公开到 Internet。

## 还缺什么？

- DevEco 真正构建验证与图标等资源兼容修复。
- BLE/SLE 扫描、权限及 ACK 已有 SDK 调用原型；仍缺 **DevEco HAP 编译验证**、完整配对/认领、真实广播、真实 ACK 和 GATT/SSAP UUID 固化。
- 设备侧 BEGIN/CHUNK/COMMIT 接收解码并映射 C99 Desk Core。
- 真实 WS63/LVGL 固件编译、烧录、三模共存与功耗实测。

## 当前实际测试证据

- **Host CI**：C99 Core / Binary ACK / HTTP 模拟器 / Stage 结构检查，可在 GitHub Actions 查询。结构检查不会调用 ArkTS 编译器。
- **App SDK 源码**：`DeskDeviceDiscovery`、`DeskPermissions`、`DeskAckWaiter`、`BleGattLink`、`SleSsapLink` 已提交但仍属待编译原型。
- **用户设备**：已知有小鸿 AI 与 HarmonyOS 7 手机，但现在不需要真机操作。进入实测关卡前先完成 [#7 无实机编译验收](https://github.com/hu-qi/XiaohongDeskCard/issues/7)，再开展 [#8 真机验收](https://github.com/hu-qi/XiaohongDeskCard/issues/8)。

注意：设备扫描可以发现某个广播名称，但不能凭名称证明它是可信的 XiaohongDeskCard 设备。**设备所有权、身份验证和安全写入**必须单独实现。
