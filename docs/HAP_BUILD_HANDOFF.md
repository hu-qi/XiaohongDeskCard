# XiaohongDeskCard · Gate A：HarmonyOS App 首次 DevEco 编译交接

状态：**2026-11-19 开发者已在 DevEco 成功构建 Debug HAP，并安装启动 HarmonyOS 7 真机**。本文保留为日后复现或排查的只编译操作指南；本次成功依据见 [Gate A 实际验证记录](HAP_GATE_A_REPORT.md)。GitHub Actions 仍不包含华为 DevEco 编译器。

**只复现 HAP 编译时不需要小鸿 AI 或手机。** 本次用户已经额外完成手机安装和启动，但尚未验证真实 WS63 三模联通。

## 前置条件

- macOS/Windows 已装 DevEco Studio，且能安装项目所配置的 **HarmonyOS SDK 6.0.2(22)**。如果你只有新 SDK，先保留版本信息，不要未经验证自行批量改 `targetSdkVersion` 或导入新的 NearLink API。
- 本地 Git 有仓库 `hu-qi/XiaohongDeskCard` 的可检出副本。
- 工程里 `apps/harmony/build-profile.json5` 和 `apps/harmony/hvigor/hvigor-config.json5` 的基线均为 6.0.2，源码采用 `@kit.NearLinkKit`；用户本机的 Stage 入口编译已通过，仍需针对未接入页面的 BLE/SLE 链路与 SDK 实际数据路径进行编译/运行验证。
- 签名只需在本机完成：DevEco **Automatic signing**（自动签名）代表你在本机生成调试证书与 Profile，仓库里**不保存**任何证书、Profile 或口令。

## 自动签名（Automatic signing）：仅在本机生效

共享仓库的 `apps/harmony/build-profile.json5` **刻意保留空的 `signingConfigs`，产品 `default` 也不声明 `signingConfig`**——它只承载代码检查/编译所需的共享结构；签名一律留在本机。这与 [docs/HARMONY_SIGNING_SECURITY.md](HARMONY_SIGNING_SECURITY.md) 的现行策略一致：签名材料仅属于开发者本地环境，公开仓库不提交任何签名信息。

1. DevEco Studio 登录华为账号（`File → Settings/Preferences → 华为账号` 或欢迎页右上角登录）。
2. 打开 `File → Project Structure… → Project → Signing Configs`。
3. 勾选 **Automatically generate signature**（自动生成签名），等待 DevEco 生成调试证书与 Profile；DevEco 会**在你本机**填写 `signingConfigs` 与产品对签名配置的引用，字段值由 IDE 自动填充，不要手写证书路径。
4. 这些本机写入的内容**只留在本机**：证书、Profile、`*.p12`、`storePassword`/`keyPassword` 一律不入库。提交代码前先自检：

   ```bash
   python3 tools/check_signing_safety.py --staged
   ```

   若提示 `BLOCKED`，用 `git restore --staged apps/harmony/build-profile.json5` 撤销该文件的**暂存**（不会抹掉本机证书配置），只提交其余安全文件。
5. 若自动签名失败（账号无权限、网络受限等），先按下方记录表回传**错误原文**，不要自行把证书内容粘贴到 Issue。
6. 若 Sync/Build 报找不到签名配置，说明本机自动签名尚未完成，回到第 2–3 步；这是本机配置问题，不是 ArkTS/Kit 原型问题。

## 操作步骤（首次只进行 HAP 构建）

1. 克隆或更新分支：

   ```bash
   git clone https://github.com/hu-qi/XiaohongDeskCard.git
   cd XiaohongDeskCard
   git switch feat/issue-7
   git rev-parse HEAD
   ```

   如果此前已克隆，请先 `git fetch origin`，再 `git switch feat/issue-7` 和 `git pull --ff-only`。本流程要求检出**包含本轮改动**的分支：`feat/issue-7`（`NearLinkCapability.ets` 运行期能力守卫、Gate A/B 回传记录表均在该分支）；若该分支已合入，改为检出合入后的基线分支，并确认本文档含 Gate A/B 回传记录表、`apps/harmony/build-profile.json5` 的 `signingConfigs` 仍为空且产品未声明 `signingConfig`，否则说明检出的仍是旧版，不得据此记录门禁结论。**不要**只检出尚未合入本轮改动的 `feat/desk-core-triradio-baseline`：其上同名文档为旧版，按本文档执行将无法验证本轮改动。签名配置由 DevEco 在**本机**写入，不随分支同步，因此该文件出现本机签名内容时不得提交。无论采用哪种检出方式，都必须记录 40 位完整 HEAD SHA（不得只写短 SHA）以便归因。

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
| 签名/证书配置报错 | 按「自动签名步骤」完成 Automatic signing 后重试；仍失败只回传错误原文，不提交证书 |
| 资源或 Module 配置报错 | 修复应用资源/Stage 工程配置，再进行同一 API 基线的构建 |
| Debug HAP 编译成功 | 记录源码 SHA、IDE/SDK 版本、HAP 文件 SHA256；然后才讨论受控模拟器或手机运行 |

## Gate A 回传记录表

按此表逐项填写后回传（可复制表格到 Issue 评论），未采集项写 `N/A`，不要留空导致口径不一。

| # | 记录项 | 采集位置 / 命令 | 必填 |
|---|---|---|---|
| 1 | DevEco Studio 版本 | `Help → About`（含 build 号） | 是 |
| 2 | HarmonyOS SDK 版本（含 build） | `File → Project Structure → SDK` | 是 |
| 3 | hvigor 版本 | `apps/harmony/hvigor/hvigor-config.json5` 的 `modelVersion` 或构建日志首行 | 是 |
| 4 | ohpm 版本 | `ohpm -v` | 是 |
| 5 | Git HEAD 完整 SHA | `git rev-parse HEAD`（40 位，不得只写短 SHA） | 是 |
| 6 | Sync 结果 | 成功 / 失败 + 首条消息 | 是 |
| 7 | Build Hap(s) 结果 | 成功 / 失败 | 是 |
| 8 | 首条实质错误及上下文 | Build Output 第一条 `ArkTS`/`@kit.*`/`hvigor`/`resources` 错误 + 周边 15–30 行 | 失败必填 |
| 9 | 失败任务摘要 | Build Output 末尾 `BUILD FAILED`/任务名 | 失败必填 |
| 10 | HAP 产物路径 | `apps/harmony/entry/build/default/outputs/default/*.hap` | 成功必填 |
| 11 | HAP SHA-256 | `sha256sum *.hap` / `certutil -hashfile *.hap SHA256` | 成功必填 |
| 12 | 构建模式 | `debug`（本流程只允许 Debug HAP 编译） | 是 |

## Gate B 回传记录表

| # | 记录项 | 采集位置 / 命令 | 必填 |
|---|---|---|---|
| 1 | OpenHarmony 分支与版本 | `repo info` 或 manifest 的 `OpenHarmony-7.0.0.38-Release` | 是 |
| 2 | 固定的 OpenHarmony commit | `repo forall -c 'git rev-parse HEAD'` 汇总，或 `python3 tools/ws63_preflight.py --root <OH_ROOT> --oh-commit <sha> --json` | 是 |
| 3 | preflight 报告 | 上述命令的 JSON 输出（含 `openHarmonyCommit`、`upstreamVendorCommit`、`missingRequired`） | 是 |
| 4 | `hb set` 选择 | `mini → atomgit → xiaohong` | 是 |
| 5 | `hb build -f` 结果 | 成功 / 失败 + 首条实质错误 ±15–30 行 | 是 |
| 6 | 产物路径与 SHA-256 | 构建输出目录中的固件镜像 | 成功必填 |
| 7 | 恢复/回滚说明 | 原厂固件备份方式、回滚命令 | 是 |
| 8 | 工具链版本 | 编译器/`gn`/`ninja`/SDK prebuilts 版本 | 是 |

## 脱敏禁止清单

回传前逐项确认，以下内容**一律不得**出现在 Issue、PR、评论、截图或附件中：

- `keystore`、`*.p12`、`*.cer`、`*.p7b`/profile、签名私钥及其 `storePassword`/`keyPassword`/ciphertext；
- 设备号、序列号、IMEI、UDID、真实 MAC 地址、手机号；
- 真实 Token、AppSecret、华为账号、企业凭据、API Key；
- 完整 home 目录路径、本机用户名、内网 IP/内网域名。

允许回传：DevEco/SDK/hvigor/ohpm 版本号、完整 Git SHA、错误文本（删除上述敏感片段后）、HAP 或固件产物的 SHA-256。

## 与固件/真机阶段的区别

- **Gate A（首次已通过）**：DevEco Studio 编译、签名、安装并启动 HarmonyOS 7 真机；日后改动仍需回归验证。
- **Gate B（后续）**：WS63 OpenHarmony 源码树与 `hb build -f`，必须先完成上游 GN、CMSIS、NV/Flash、GATT/SSAP/HTTP 板级适配；**现阶段还不能把生成的 WS63 overlay 视为完整固件**。
- **Gate C（最后）**：拿到经过软件编译的固件和 HAP 后，再通知用户进行烧录、手机安装运行与 BLE/SLE/Wi-Fi 真机联调。

Gate A 的首次编译与安装启动已经完成。**下一关是 Gate B 的 WS63 实际 SDK 集成与编译；目前不需要再次安装手机 App 或烧录小鸿板卡。**
