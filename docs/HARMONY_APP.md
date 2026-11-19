# HarmonyOS Stage App · 软件先行版本

## 目标与当前边界

本仓库的 `apps/harmony` 已包含 Stage 工程、入口 Ability、ArkUI 编辑页及三通道源文件。**开发者已在 2026-11-19 使用 DevEco Studio 和 SDK 6.0.2(22) 成功构建 Debug HAP，在 HarmonyOS 7 真机安装并启动**。实际修复了 SDK 22 启动窗口资源、ArkTS 严格类型及本地签名绑定；详见 [Gate A 验证记录](HAP_GATE_A_REPORT.md)。这不代表 BLE/SLE/Wi-Fi 与小鸿 AI 固件已联通。

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
        DeskRouter.ts
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

`module.json5` 已声明 INTERNET、ACCESS_BLUETOOTH、ACCESS_NEARLINK，但**声明不等于用户授权**。扫描界面源码已通过 `DeskPermissions.request` 接入运行时授权，但实际授权弹窗、拒绝处理和手机 API 行为**仍待专门的运行时功能测试**。

模拟 HTTP 令牌只适用于本机开发。不要在不可信局域网明文发送生产令牌或把 Mock HTTP 服务公开到 Internet。

## 还缺什么？

- DevEco 首次 Debug HAP 构建及启动资源问题已通过；后续源码改动的 DevEco 回归仍需执行。
- BLE/SLE 扫描、权限及 ACK 已有 SDK 调用原型；**App 启动已验证**，但还缺真实运行中的完整配对/认领、设备端广播、业务 ACK 和 GATT/SSAP UUID 固化。
- C99 设备侧 BEGIN/CHUNK/COMMIT **解析器已实现并通过宿主机测试**，但还未集成到 WS63 的 BLE/SLE 服务端和 Desk Core UI 任务。
- 真实 WS63/LVGL 固件编译、烧录、三模共存与功耗实测。

## 当前实际测试证据

- **Host CI**：C99 Core / Binary ACK / HTTP 模拟器 / Stage 结构检查，可在 GitHub Actions 查询。结构检查不会调用 ArkTS 编译器。
- **App SDK 源码**：Stage 入口在开发者本机 DevEco 成功编译和启动；尚未接到页面入口或板端设备的 BLE/SLE Server 适配依然属于待功能验证原型。
- **用户设备**：HarmonyOS 7 手机已完成 App 首次安装启动；当前无需再次操作手机或小鸿板卡。先推进 [#7 SDK 编译验收](https://github.com/hu-qi/XiaohongDeskCard/issues/7)，再开展 [#8 真机验收](https://github.com/hu-qi/XiaohongDeskCard/issues/8)。

注意：设备扫描可以发现某个广播名称，但不能凭名称证明它是可信的 XiaohongDeskCard 设备。**设备所有权、身份验证和安全写入**必须单独实现。


## 三模自动选择的安全边界（已实现并测试）

`DeskRouter.ts`：只有连接或读取状态失败（尚未发送写入）时，才能按顺序切换 SLE → BLE → Wi-Fi。若调用 `setContent()` 后出现超时、断连、ACK 丢失，则抛出 `DeskOutcomeUnknownError`，**不会在另一无线通道盲目重发**；需先读取设备的最新 revision/content 进行人工或显式的状态核对。明确的业务拒绝（如 CONFLICT）同样不能被当成无线失败重试。

该路由器本身由 Node 22 对真实 TypeScript 源文件执行单测（不用复制一份模拟逻辑）。ArkUI 页面新增“自动选择链路 · Mock”按钮，仅用于验证能力选择和回退，并不是实际手机三模直连。真实连接、鉴权、STATUS/ACK 和固件通信仍在 Issue #7 验收范围内；App 的首次 HAP 编译已完成。


## Gate A 首次用户协助（已完成 DevEco 编译与真机启动）

开发者已完成首次真实 DevEco 编译和手机启动；完整问题与修复见 [HAP_GATE_A_REPORT.md](HAP_GATE_A_REPORT.md)。GitHub Actions 无法重现 DevEco 原生编译。

今后复现请在 DevEco Studio 打开 `apps/harmony`、Sync 并选择 Build Hap(s)。本地调试签名不应提交 GitHub，参见 [HARMONY_SIGNING_SECURITY.md](HARMONY_SIGNING_SECURITY.md)。

**目前无需重复运行手机 App，仍无需烧录小鸿 AI。** 下一阶段是 WS63 真实 SDK 编译，随后才验证三模设备联通。
