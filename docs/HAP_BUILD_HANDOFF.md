# XiaohongDeskCard · Gate A：HarmonyOS App 首次 DevEco 编译交接

状态：**现在可以进行第一轮「只编译、不运行」的 HAP 编译诊断。** GitHub Actions 已运行源文件结构与 C99/LVGL/CMSIS 测试，但没有华为 DevEco SDK，因此不能替代真正的 ArkTS 编译器。第一次构建的目标是得到明确的编译通过记录，或者可定位的错误日志。

**本流程不需要小鸿 AI、不需要 HarmonyOS 7 手机、不安装 App、不烧录固件、不启用 BLE/SLE/Wi-Fi 设备连接。**

## 前置条件

- macOS/Windows 已装 DevEco Studio，且能安装项目所配置的 **HarmonyOS SDK 6.0.2(22)**。如果你只有新 SDK，先保留版本信息，不要未经验证自行批量改 `targetSdkVersion` 或导入新的 NearLink API。
- 本地 Git 有仓库 `hu-qi/XiaohongDeskCard` 的可检出副本。
- 工程里 `apps/harmony/build-profile.json5` 和 `apps/harmony/hvigor/hvigor-config.json5` 的基线均为 6.0.2，源码采用 `@kit.NearLinkKit`；**接口能否编译仍待此项检查**。

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

- **Gate A（当前）**：DevEco Studio 首次导入、同步、编译 HAP，**无需运行**。
- **Gate B（后续）**：WS63 OpenHarmony 源码树与 `hb build -f`，必须先完成上游 GN、CMSIS、NV/Flash、GATT/SSAP/HTTP 板级适配；**现阶段还不能把生成的 WS63 overlay 视为完整固件**。
- **Gate C（最后）**：拿到经过软件编译的固件和 HAP 后，再通知用户进行烧录、手机安装运行与 BLE/SLE/Wi-Fi 真机联调。

若不能立即进行 Gate A，仍可继续进行 SDK 独立的源码开发；**不需要为了构建诊断提前改变手机和小鸿板卡状态**。
