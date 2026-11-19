# Gate B · WS63 真实 SDK 接口证据采集（只读，不编译、不接设备）

## 目的

项目已经完成可移植的 12 C + 12 H 模块、GN 源码注入保护和 LVGL/CMSIS 官方库宿主机编译。**现在缺少的是第一代小鸿 AI（WS63）实际 SDK 的源码版本和板级函数签名**，不能仅凭官网示例推断真实 GN、事件队列与 GATT/SSAP API。

`tools/ws63_sdk_probe.py` 是为此新增的离线、只读信息采集脚本。它只读取已有的 OpenHarmony SDK 源码树，不下载 SDK、不执行 `hb build`、不修改文件、不需要开发板和手机。

## 使用场景

**电脑里已有完整小鸿 OpenHarmony SDK 时**，在 XiaohongDeskCard 工程根目录执行：

```bash
git switch feat/desk-core-triradio-baseline
git pull --ff-only

# 将这里改成真实的 OpenHarmony SDK 根目录
OHOS="/path/to/openharmony"

# 必须能找到 "$OHOS/vendor/atomgit/xiaohong/xiaohong/BUILD.gn"
test -f "$OHOS/vendor/atomgit/xiaohong/xiaohong/BUILD.gn" || exit 2

mkdir -p build
python3 tools/ws63_sdk_probe.py \
  --root "$OHOS" --output "build/ws63-sdk-probe.json"

python3 tools/ws63_preflight.py --root "$OHOS" --json \
  > "build/ws63-preflight.json"
```

结果为两个小 JSON 文件：

- `ws63-sdk-probe.json`：上游 vendor Git commit、关键源码 SHA-256、已知显示/网络标识是否存在、候选源码文件名、LVGL version。
- `ws63-preflight.json`：原目录结构和关键锚点检查、SDK 编译标识固定为 `false`。

**先检查 JSON 是否含不愿共享的信息，再提供文件内容。** 工具不会输出源代码正文、Wi-Fi 密码、Debug 签名文件内容，但上游 Git SHA/文件哈希仍属工程版本信息。如果使用的是企业内网 SDK，遵守对应代码保密要求。

**如果电脑尚无 OpenHarmony WS63 源码树，无需现在自行下载数十 GB 或尝试烧录。** 可先回复“尚无 SDK 源码”，之后再按可用主机/磁盘/工具链确定源码获取方式。当前 HarmonyOS 7 App 已经成功启动，不需要重复构建。

## 为什么现在不能直接发给你 `hb build` 指令

还没有查到能匹配你本机小鸿第一代 WS63 的固定上游 commit/实际 `BUILD.gn`、`lvgl_task.c`、`disp_driver.h`、Flash 分区及 BLE GATT/SSAP Server 绑定。当前 C99 SDK-independent 程序和主机 CI 全部正常，但缺少实际 RTOS/驱动的签名和运行时约束。直接改 SDK 或刷未验证镜像有覆盖原厂固件与存储数据的风险。

因此当前 Gate B 的严格顺序：

1. 收到只读 SDK fingerprint/preflight，确认上游源码版本与相关目标；
2. 对实际源码评审 GN、LVGL、CMSIS 事件模型和板级无线 HAL；
3. 在分支中提交**具体针对该 SDK commit 的**绑定代码；
4. 用户或有 SDK 工具链的 CI 执行首次 `hb build`；将脱敏编译错误反馈用于修复；
5. 产物可追溯且具备恢复方案后，再通知进行烧录和手机三模联调。

## 当前验收范围

| 项目 | 状态 |
|---|---|
| 12 C 源码、LVGL 8/9 与官方 CMSIS 头文件联合编译 | 已通过宿主机 CI |
| 版本锁定 GN 注入工具和临时 vendor Git 仓库测试 | 已通过 |
| DevEco Debug HAP 在 HarmonyOS 7 真机运行 | 用户已确认 |
| 真实 WS63 OpenHarmony SDK 源码快照 | **待获取** |
| WS63 实际 `hb build`、固件镜像 | 未执行 |
| 小鸿 AI 烧录、BLE/SLE/Wi-Fi 真机联调 | 未执行 |

**现在如果需要用户协助，只需提供已有 SDK 的探测报告；不需要编译固件、重新运行 App 或接入板卡。**
