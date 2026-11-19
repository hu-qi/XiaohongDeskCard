# XiaohongDeskCard · Gate A：HarmonyOS App 首次 DevEco 编译交接

状态：**2026-11-19 开发者已在 DevEco 成功构建 Debug HAP，并安装启动 HarmonyOS 7 真机**。本文保留为日后复现或排查的只编译操作指南；本次成功依据见 [Gate A 实际验证记录](HAP_GATE_A_REPORT.md)。GitHub Actions 仍不包含华为 DevEco 编译器。

**只复现 HAP 编译时不需要小鸿 AI 或手机。** 本次用户已经额外完成手机安装和启动，但尚未验证真实 WS63 三模联通。

## 前置条件

- macOS/Windows 已装 DevEco Studio，且能安装项目所配置的 **HarmonyOS SDK 6.0.2(22)**。如果你只有新 SDK，先保留版本信息，不要未经验证自行批量改 `targetSdkVersion` 或导入新的 NearLink API。
- 本地 Git 有仓库 `hu-qi/XiaohongDeskCard` 的可检出副本。
- 工程里 `apps/harmony/build-profile.json5` 和 `apps/harmony/hvigor/hvigor-config.json5` 的基线均为 6.0.2，源码采用 `@kit.NearLinkKit`；用户本机的 Stage 入口编译已通过，仍需针对未接入页面的 BLE/SLE 链路与 SDK 实际数据路径进行编译/运行验证。

## 操作步骤（首次只进行 HAP 构建）

1. 克隆或更新分支：

   ```bash
   git clone https://github.com/hu-qi/XiaohongDeskCard.git
   cd XiaohongDeskCard
   git switch feat/desk-core-triradio-baseline
   git rev-parse HEAD
   ```

   如果此前已克隆，请先 `git fetch origin`，再 `git switch feat/desk-core-triradio-baseline` 和 `git pull --ff-only`。记录完整 HEAD SHA 以便归因。

2. **DevEco Studio → Open Project**，选择 `XiaohongDeskCard/apps/harmony`（不要选择仓库顶层）。不要在桌牌 App 中输入任何真实 BLE 绑定凭据或 Wi-Fi Token。

3. DevEco Studio 执行 **Sync / Sync and Refresh Project**。选择能满足 SDK 6.0.2(22) 的组件，不自动升级全部构建配置。若提示缺少插件/SDK，记录原始消息和 DevEco 版本。

4. 选择 **Build → Build Hap(s)/APP(s) → Build Hap(s)**（实际菜单以当前 DevEco UI 为准）。只做 Debug HAP 编译检查，暂不点击 Run/Debug，不连接/授权/安装任何手机，不需要烧录小鸿固件。

5. 在 **Build / Build Output** 保存构建结果；如失败，收集以下**可脱敏**信息：
   - DevEco Studio 版本与已安装的 HarmonyOS SDK 版本；
   - Git HEAD 完整 SHA；
   - 构建输出的第一条实质错误（尤其 `ArkTS`、`@kit.NearLinkKit`、`@kit.ConnectivityKit`、`hvigor`、`resources`），以及周边 15–30 行；
   - 末尾任务失败摘要。**不要上传 keystore、签名私钥、设备号、真实 Token、完整 home 目录或企业凭据。**

   如果成功，只需要报告“Sync 成功 + Debug HAP 编译成功”，保存 Build Output 与产物路径；**暂时不需要安装或运行**。

## 结果判断

| 结果 | 下一步 |
|---|---|
| Sync/依赖失败 | 先定位 DevEco/Hvigor/ohpm 项目依赖，不修改 Desk Core |
| ArkTS 类型/API 报错 | 按本机实际 SDK 声明逐项修复 `BLE`/`SLE`/`权限` 原型 |
| 资源或 Module 配置报错 | 修复应用资源/Stage 工程配置，再进行同一 API 基线的构建 |
| Debug HAP 编译成功 | 记录源码 SHA、IDE/SDK 版本、HAP 文件 SHA256；然后才讨论受控模拟器或手机运行 |

## 与固件/真机阶段的区别

- **Gate A（首次已通过）**：DevEco Studio 编译、签名、安装并启动 HarmonyOS 7 真机；日后改动仍需回归验证。
- **Gate B（后续）**：WS63 OpenHarmony 源码树与 `hb build -f`，必须先完成上游 GN、CMSIS、NV/Flash、GATT/SSAP/HTTP 板级适配；**现阶段还不能把生成的 WS63 overlay 视为完整固件**。
- **Gate C（最后）**：拿到经过软件编译的固件和 HAP 后，再通知用户进行烧录、手机安装运行与 BLE/SLE/Wi-Fi 真机联调。

Gate A 的首次编译与安装启动已经完成。**下一关是 Gate B 的 WS63 实际 SDK 集成与编译；目前不需要再次安装手机 App 或烧录小鸿板卡。**
