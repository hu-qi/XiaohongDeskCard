# HarmonyOS Stage App · 软件先行版本

## 目标与当前边界

本仓库的 `apps/harmony` 已包含 Stage 工程、入口 Ability、ArkUI 编辑页及三通道源文件。**开发者已在 2026-11-19 使用 DevEco Studio 和 SDK 6.0.2(22) 成功构建 Debug HAP，在 HarmonyOS 7 真机安装并启动**。实际修复了 SDK 22 启动窗口资源、ArkTS 严格类型及本地签名绑定；详见 [Gate A 验证记录](HAP_GATE_A_REPORT.md)。这不代表 BLE/SLE/Wi-Fi 与小鸿 AI 固件已联通。

工程配置以项目现有 [Goodtime 鸿蒙化工程](https://github.com/hu-qi/goodtime/tree/harmonyos) 的 `6.0.2(22)` 构建模型为参考。

### 开发模式

页面不再使用 Mock：BLE / 星闪按钮走真实无线链路。

- BLE / SLE：扫描广播名称为 `XiaohongDesk` 的设备，连接后按 `DeskServiceIds` 发现 Desk Card 服务，写入 `DeskWireFrame` 生成的 BEGIN/CHUNK/COMMIT 包，并等待设备 16 字节业务 ACK。**写入成功不等于桌牌已更新**，只有 ACK 才算完成。
- `RadioDeskTransport.ets`：`BleGattLink` / `SleSsapLink` 的统一 `DeskTransport` 实现。由于 Desk Wire v1 没有无线读取操作，`getStatus()` 用一次必然被拒的 SET（`expectedRevision = UINT32_MAX`）取得设备真实 revision；Desk Core 在持久化之前就拒绝，因此不会改动桌牌。
- 自动选择链路：复用 `DeskRouter.ts`，顺序 SLE → BLE → Wi-Fi。只有连接/读状态失败才回退；一旦写入过，ACK 丢失属于未知结果，不会换通道重发。
- Wi-Fi HTTP：NetworkKit 的真实网络请求，需要用户输入端点和令牌。
- Mac/Linux 浏览器模拟器：本地 loopback HTTP + 真实 C99 Desk Core。
- `MockDeskTransport.ets` 仍保留（`tests/router.test.mjs` 与离线预览使用），但页面已不引用。

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
        RadioDeskTransport.ets
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

- 页面已改为真实链路，`hvigorw assembleHap` 在本机通过（含 `RadioDeskTransport`、`DeskRouter` 接入）；仍需在 DevEco 里做一次回归确认。
- BLE/SLE 扫描、连接、写入与业务 ACK 已全部接上页面；**真机端到端仍未验证**：需要烧录含最新 UUID/SLE 修复的固件后实测。
- C99 设备侧 BEGIN/CHUNK/COMMIT **解析器已实现并通过宿主机测试**，并已接到 WS63 的 GATT/SSAP 服务端与 Desk Core UI 任务；整链路仍需真机验证。
- 设备端 ownership/认领（配对之外的身份校验）仍未实现：当前固件在 GATT/SSAP 连接建立后即视为已授权。

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
