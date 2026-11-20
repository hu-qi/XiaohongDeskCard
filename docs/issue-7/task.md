# Task：Issue #7 任务拆解

- [x] T1 阅读 Issue/评论/Spec，对齐基线 `feat/desk-core-triradio-baseline`
- [x] T2 基线回归：CTest 23/23、Node 路由 8/8
- [x] T3 输出 `docs/issue-7/spec.md` / `plan.md` / `task.md`
- [x] T4 `build-profile.json5` 共享配置不声明签名引用（`signingConfigs` 空、产品无 `signingConfig`）+ 结构测试
- [x] T5 `docs/HAP_BUILD_HANDOFF.md` 自动签名步骤、Gate A/B 记录表、脱敏清单 + 结构测试
- [x] T6 `NearLinkCapability.ets` 运行期系统能力守卫 + 结构测试
- [x] T7 `tools/ws63_preflight.py --oh-commit` + 单测 + 文档命令更新
- [x] T8 五轴自审查（正确性/安全/性能/可维护性/测试）
- [x] T9 全量回归（CTest + UBSan + Node 路由；rebase 到 `feat/desk-core-triradio-baseline` 最新 tip 后重跑）
- [x] T10 推送分支、更新 PR #9（base=`feat/desk-core-triradio-baseline`）、Issue 评论、A2A QA
