# Plan：Issue #7 首轮清障实施方案

## 1. 总体策略

只做"编译前可代码化"的四项清障，C99 核心与既有测试零改动。三项 App/工具链改动均以"结构测试可断言"的方式落地，避免依赖本机 DevEco（宿主机无 DevEco/hvigor/hb，真实构建由用户执行）。

## 2. 模块改动

| 模块 | 改动 | 验证方式 |
|---|---|---|
| `apps/harmony/build-profile.json5` | 不新增签名引用：`signingConfigs` 保持 `[]`，产品不声明 `signingConfig`（签名由 DevEco Automatic signing 在本机写入，不入库） | `tests/test_harmony_structure.py` 断言空签名槽且产品无该键；`tools/check_signing_safety.py --tracked` |
| `entry/src/main/ets/desk/NearLinkCapability.ets` | 新增 `canIUse` 系统能力守卫 + 导出常量；`try/catch` 包裹 SDK 调用，异常返回 `false` | 结构测试断言 `canIUse`/能力串/SDK 导入 |
| `docs/HAP_BUILD_HANDOFF.md` | 新增自动签名步骤、Gate A/Gate B 固定记录表、脱敏禁止清单、失败回传字段 | 结构测试断言表格标题与脱敏关键词 |
| `tools/ws63_preflight.py` | 新增 `--oh-commit`，报告含 `openHarmonyCommit`/`openHarmonyCommitPinned`；SHA 校验（7–40 位小写十六进制，允许大写自动归一） | `tests/test_ws63_preflight.py` 新增 4 个用例 |
| `docs/WS63_INTEGRATION.md`、`README.md` | 预检查命令补 `--oh-commit`，说明版本固定要求 | 无需测试 |

## 3. 关键决策

| 决策 | 选项 | 选择 | 理由 |
|---|---|---|---|
| 共享配置签名形态 | 产品引用 `default` 占位 / 完全不声明签名引用 | 后者 | 现行策略见 `docs/HARMONY_SIGNING_SECURITY.md` 与 `tools/check_signing_safety.py`：签名只在本机生效；产品引用仓库里不存在的签名配置只是伪占位，反而让配置报错与真实 ArkTS 错误混淆 |
| NearLink 守卫方式 | 动态 import / 静态 import + 运行期守卫 | 后者 | ArkTS 动态 import 仍受模块解析约束；`canIUse` 为全局能力查询，改动最小且可测 |
| OH commit 来源 | 从 git 自动取 / 显式 `--oh-commit` | 显式入参（保留自动取 vendor commit） | 用户树可能非 git 或为 repo 多仓，显式固定才可复现 |

## 4. 提交拆分（原子）

1. `docs(issue-7): add Gate A/B clearing spec, plan and task breakdown`
2. `docs(app): document local-only automatic signing without committing signing material`（含 HANDOFF 记录表与结构测试）
3. `fix(app): guard NearLink probe behind runtime system capability`（含结构测试断言）
4. `feat(ws63): pin OpenHarmony commit in preflight report`（含单测与文档命令）

## 5. 验证矩阵

| 检查 | 命令 | 期望 |
|---|---|---|
| CTest | `cmake -S . -B build && cmake --build build --parallel && ctest --test-dir build --output-on-failure` | 23/23 通过 |
| UBSan | `cmake -S . -B build-ubsan -DCMAKE_C_FLAGS="-fsanitize=undefined -fno-sanitize-recover=all" ... && ctest -R desk_` | 通过 |
| Node 路由 | `node --experimental-strip-types --test tests/router.test.mjs` | 8/8 通过 |
| 覆盖层 | `python3 tools/ws63_overlay.py --output build/ws63-overlay` | 退出码 0 |

真实 DevEco Sync/Build 与 `hb build -f` 属用户协助步骤，本方案只保证首条错误落在 ArkTS/Kit 原型而非配置。
