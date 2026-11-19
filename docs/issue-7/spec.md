# Spec：Issue #7 Gate A / Gate B 无实机编译验收（首轮清障）

## 1. 背景

Gate A（DevEco Debug HAP 只编译）与 Gate B（WS63 OpenHarmony 源码树 `hb build -f`）都需要真实工具链，宿主机 CI 无法代替。当前仓库已具备 C99 Desk Core、LVGL 视图、HarmonyOS Stage 工程初稿与 WS63 overlay/预检查脚本，但存在几处"非 ArkTS/Kit 问题"会让用户首轮构建先失败：

1. `apps/harmony/build-profile.json5` 未声明产品与签名配置的关联，首轮构建会终止在签名配置缺失；
2. `NearLinkCapability.ets` 直接调用 `manager.isNearLinkSupported()`，未做系统能力守卫，在 `targetSdkVersion 6.0.2(22)` 下可能成为首个 ArkTS 错误；
3. `tools/ws63_preflight.py` 未固定 OpenHarmony 实际 commit，Gate B 的"工具链版本可复现"缺少强制入参；
4. `docs/HAP_BUILD_HANDOFF.md` 未固化回传记录表与脱敏清单，用户回传口径不一致。

## 2. 目标

- 把"SDK 独立的宿主机准备"推进到"真实工具链下首条错误可归因"的状态。
- 不改动已通过测试的 C99 核心（`firmware/**` 与 `CMakeLists.txt` 编译核心），不引入新依赖。

## 3. 范围

### In Scope

| # | 需求（EARS） | 位置 |
|---|---|---|
| R1 | 当使用 DevEco Automatic signing 时，工程应已具备可被自动签名覆盖的产品/签名关联结构，使首轮构建不因 `signingConfigs` 为空而直接终止 | `apps/harmony/build-profile.json5` |
| R2 | 当运行环境不支持 NearLink 系统能力时，NearLink 能力探测应返回 `false` 且不抛出异常 | `apps/harmony/entry/src/main/ets/desk/NearLinkCapability.ets` |
| R3 | 当用户按 HANDOFF 执行 Gate A 时，文档应提供固定回传记录表、脱敏禁止项清单与自动签名操作步骤 | `docs/HAP_BUILD_HANDOFF.md` |
| R4 | 当以 `--oh-commit <sha>` 运行预检查时，报告应包含可追溯的 OpenHarmony commit 字段；非法 SHA 应以非零退出码拒绝 | `tools/ws63_preflight.py` |
| R5 | 当上述改动落地后，现有 20 项 CTest、UBSan 检查与 Node 22 路由测试应保持通过 | `tests/**`、CI |

### Out of Scope

- DevEco Studio GUI 内实际 Sync / Build Hap(s) 执行（用户协助，回传脱敏日志）。
- 真机安装/烧录/联调（#8）。
- WS63 内 BLE GATT / SLE SSAP / HTTP Server 的真实板级绑定实现。
- #6 与 #7 的收敛关闭（维护者决策）。

## 4. 验收判据

- `build-profile.json5` 保持严格 JSON（现有结构测试用 `json.load` 解析），产品引用签名配置名，`signingConfigs` 仍为空数组，可由 Automatic signing 直接填充。
- `NearLinkCapability.ets` 保留 `@kit.NearLinkKit` 导入，调用前使用 `canIUse('SystemCapability.Communication.NearLink.Base')`，调用包裹 `try/catch`，异常路径返回 `false`。
- HANDOFF 含 Gate A 记录表（DevEco 版本、SDK 版本与 build、hvigor/ohpm 版本、完整 Git SHA、首条实质错误 ±15–30 行、HAP 路径与 SHA-256）、Gate B 记录表（OH 分支与 commit、preflight 报告、`hb build` 日志、产物 SHA、回滚说明）及脱敏禁止项。
- `python3 tools/ws63_preflight.py --root <OH_ROOT> --oh-commit <sha> --json` 输出含 `openHarmonyCommit`，单测覆盖合法/非法输入。
- `ctest` 20/20、`node --experimental-strip-types --test tests/router.test.mjs` 全通过。

## 5. 风险

| 风险 | 缓解 |
|---|---|
| SDK 6.0.2(22) 仍未声明 `manager.isNearLinkSupported` | 守卫保证运行期安全，但静态声明缺失仍会报 ArkTS 错误；HANDOFF 记录表要求回传该行，下一轮按实际 SDK 声明裁剪 |
| Automatic signing 需登录华为账号 | HANDOFF 明确失败回传字段，签名前置失败不阻塞诊断 |
| 签名资产泄露 | 脱敏清单禁止提交 keystore/私钥/profile/口令，只提交结构与文档 |
