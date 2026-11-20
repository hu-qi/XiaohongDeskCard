# HarmonyOS DevEco 本地签名管理（不要提交签名材料）

## 当前实际情况

2026-11-19，项目已由开发者在 HarmonyOS 7 真机完成首次 Debug HAP 编译、安装及启动。过程中确认了 DevEco 的调试签名设置：需要在本机为 `signingConfigs` 配置完整的调试证书/密钥材料，并在 `products[].signingConfig` 中引用正确的名称。

**签名材料仅属于开发者的本地环境。公开仓库不会提交以下任何信息：**

- `keyPassword`、`storePassword` 等密码；
- `certpath`、`storeFile`、`profile` 及个人目录；
- 调试证书、密钥、签名文件、真实设备绑定令牌。

因此版本库的 `apps/harmony/build-profile.json5` **刻意保留空的 `signingConfigs`**，也不在 product 中声明不存在的签名配置。这个共享版本可供代码检查/编译，但直接安装到真机前必须由 DevEco 在**本机**正确配置签名。

## 以后本机开发的操作约束

1. 在 DevEco Studio 的签名配置界面建立或选择自己的本地 Debug 签名，并确认产品引用该签名配置；由 IDE 保存本地 `build-profile.json5` 修改。
2. 本地签名文件及密码配置**不要**复制到 Pull Request、Issue、聊天群、CI 日志或 README 中。Git 忽略证书二进制文件，但 `build-profile.json5` 本身是已跟踪文件，**.gitignore 无法保护它的内容**。
3. 提交代码时逐个指定安全文件，例如 `git add apps/harmony/entry/src/main/ets/desk/DeskDeviceDiscovery.ets`，**不要把带有本机签名的 build-profile 一并提交**。
4. 提交前从仓库根目录执行：

   ```bash
   python3 tools/check_signing_safety.py --staged
   git diff --cached --name-only
   ```

   若提示 BLOCKED，请撤销该文件的**暂存**：`git restore --staged apps/harmony/build-profile.json5`。这条命令只撤销暂存，不会抹掉你的本地证书配置。
5. 如果将来需要同步分支，**先检查 `git status` 并备份本地签名配置到受保护的个人目录**，再决定是否暂存/合并。不要盲目使用 `git restore` 或强制覆盖本地文件。

CI 中的 `tools/check_signing_safety.py --tracked` 和 `harmony_signing_safety` 测试将拒绝带真实签名配置的共享文件。这只是窄范围防护，不能代替全面的凭据审计。

如果签名密码曾发布到公开仓库、Issue 或其他不受控渠道，应**重新生成调试签名材料并停止复用旧密码**。不要把密钥或密码再次发给协作者进行排查；只提供不含凭据的 Build Output。

## 真实构建成功的证据界限

已由开发者报告：
- SDK 6.0.2(22) 下遇到的 Stage 启动窗口字段、颜色资源及两处 ArkTS 严格模式问题得到修复；
- 签名设置正确后，Debug HAP 可以在 HarmonyOS 7 真机上安装、启动。

仍需区分：
- 共享仓库的构建配置不会复制本机凭据；克隆到另一台机器后必须重新选择签名。
- **真机成功启动 App 不代表 BLE/SLE 发现、配对、ACK、Wi-Fi 更新或 WS63 固件已通过验证**。
- 本分支后续源码更新仍需要适当的 DevEco 回归编译；GitHub Actions 不具备本机 DevEco 编译器。
