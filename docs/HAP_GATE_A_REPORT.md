# Gate A · HarmonyOS 7 首次 App 真机启动验证

**状态：开发者已报告通过（2026-11-19）。**

这个结论来自用户在本地 DevEco Studio 完成 Debug HAP 编译、安装和 HarmonyOS 7 手机启动的实际结果，而不是 GitHub Actions 的结构检查。仓库侧仅记录**脱敏的修复点和验证范围**；本地签名材料未写入公开分支。

## 本次编译问题及已同步的修复

| 项目 | 实际问题 | 本仓库处理 |
|---|---|---|
| Stage 启动窗口资源 | SDK 22 的 Module schema 要求 `startWindowIcon`、`startWindowBackground` | 在 `module.json5` 添加对应资源引用，新增 `color.json` |
| BLE/SLE 发现异常处理 | `arkts-limited-throw` 禁止直接抛出未知类型 | `DeskDeviceDiscovery.ets` 使用 `throw new Error(String(err))` |
| ArkTS Promise.catch 回调 | `arkts-no-any-unknown` 禁止隐式未知参数类型 | `Index.ets` 给异步关闭回调显式标注 `Error` |
| Debug HAP 设备签名 | 本机配置了证书但 product 未绑定对应签名，导致安装拒绝 | **本机**为 `products[].signingConfig` 设置匹配的调试配置；公开仓库不复制任何证书路径或密码 |

这些是根据用户提供的成功编译/运行记录确认的五项本地修复；公开分支只同步**不含敏感数据的四处 Stage/ArkTS/资源修改**。本机签名仍须由 DevEco 管理，详见 [本地签名安全规范](HARMONY_SIGNING_SECURITY.md)。

## 已验证（用户本机）

- [x] DevEco 项目可以完成对应 SDK 22 的 Debug HAP 编译。
- [x] 产品绑定本地 Debug 签名后，HAP 能安装到 HarmonyOS 7 真机。
- [x] App 在手机上启动运行。
- [x] 上述 Stage 配置及 ArkTS 严格检查问题已被修复。

## 尚无证据 / 不应宣称通过

- [ ] 当前 GitHub HEAD 的全部变更重新经过 DevEco 编译（本轮同步之后尚未让用户重新构建）。
- [ ] BLE 真机扫描、GATT 连接、写入和 ACK 到真实 WS63 桌牌。
- [ ] 星闪 SLE 在该手机上的芯片/系统支持与 SSAP 实际连接验证。
- [ ] Wi-Fi HTTP 真机与小鸿 WS63 固件联通、设备授权、请求更新和 ACK。
- [ ] WS63 真实 OpenHarmony SDK `hb build` 与 Flash、LVGL、CMSIS、GATT/SSAP/HTTP 服务适配。
- [ ] 固件烧录、240×240 TFT 中文渲染、双槽 Flash 真实断电恢复。

## 补充元信息（未提供，不能猜测）

DevEco Studio 的精确版本号、本机编译时 Git SHA、HAP 文件哈希、设备系统 Build 号目前未记录。这不否定用户反馈的实际成功运行，但限制了对同一二进制的可复现性声明。

## 下一关

**Gate A 的首次编译/安装/启动目标可视为用户验证通过。** 项目继续进入 Gate B：真实 WS63 SDK 固件编译和 BLE/SLE/Wi-Fi 服务绑定。Gate C 仍是后续带小鸿板卡的真机联调，届时再提供具体烧录和运行步骤。

当前无需重复安装 App，也无需进行小鸿 AI 烧录。若后续 ArkTS 改动较大，再安排有目标的 DevEco 回归验证。
