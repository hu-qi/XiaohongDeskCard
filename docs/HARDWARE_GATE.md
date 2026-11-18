# 实机验收关卡 · XiaohongDeskCard

> 状态：NOT READY。现在不需要用户接线、烧录或提供手机日志。
>
> 已知可用设备：小鸿 AI WS63 原装屏 1 台、HarmonyOS 7 真机 1 台。
> 具体手机型号、NearLink 硬件支持、手机开发者选项等尚未核对。系统是 HarmonyOS 7 **不等于** 手机一定具备 NearLink 硬件。

## Gate A：软件验证（无需设备）— 进行中

- [x] C99 Desk Core / Desk Wire 帧编解码
- [x] BLE/SLE Mock 分片与真实 C99 Core HTTP 模拟器
- [x] 16-byte business ACK、CRC32 与请求 ID 关联，宿主机自动化测试
- [x] HarmonyOS Stage 工程、编辑 UI、BLE/SLE 扫描和 SDK 写入源码初稿
- [ ] DevEco Studio / Hvigor **真实构建通过 HAP**（GitHub 结构检查不等于 HAP 编译）
- [x] 软件侧校验分包恢复、CRC、超时及预写入安全回退策略（Node 22 + C99 测试；非无线实测）
- [ ] 指定安全的受控局域网/模拟器互联测试方案

## Gate B：SDK 编译验证（无需设备）— 未开始

- [ ] 固定并记录小鸿 WS63 原仓库及 SDK 的实际 commit、构建说明和工具链
- [ ] 编译原始官方样例，保存成功日志与产物校验和
- [ ] 在源码层接入 `desk_core`、`desk_wire`、`desk_packet`、LVGL 桌牌控件和 NV 持久化
- [ ] 编译 BLE GATT Peripheral/Server 与 SLE SSAP Server，并加入相同 UUID
- [ ] 编译 Wi-Fi HTTP 控制接口与业务 ACK，确认三者任务边界
- [ ] 检查 RAM/Flash 预算、字符集与 UI 任务单线程约束
- [ ] 安全审查：设备绑定、权限、防重放、CRC 与鉴权分离
- [ ] 形成**可烧录的候选固件**，产出清晰的构建/烧录/回滚说明

## Gate C：实机验证（**届时需要用户参与**）— 未开始

在 Gate A / B 达标后明确告知用户开始真机测试，并提供候选固件的 commit SHA / 构建产物 / 烧录说明。具体使用现有小鸿 AI 原装屏及 HarmonyOS 7 手机。

### Step 1：小鸿 AI 设备基线

1. 记录小鸿 AI 固件版本、开发板版本、串口端口。
2. 先备份/记录当前固件及恢复方式，不直接覆盖重要数据。
3. 烧录候选固件，检查原装 240×240 TFT 显示中文姓名和职位。
4. 测试断电恢复与串口日志。

### Step 2：手机 BLE

1. 在 HarmonyOS 7 手机安装已构建签名的 HAP，授权蓝牙。
2. 扫描并连接设备；识别 Desk GATT Service、写入与 ACK Characteristic。
3. 发送中文桌牌内容，确认返回的 requestId/revision/resultCode、实际屏幕显示。
4. 验证断连重连、无授权拒绝、校验失败、通知超时。

### Step 3：手机 SLE

1. 调用 `manager.isNearLinkSupported()`，记录结果、手机型号和 HarmonyOS 7 具体系统版本。
2. 授予 ACCESS_NEARLINK 权限，扫描桌牌广播。
3. 发现并连接 SSAP Server，订阅 ACK Property。
4. 发送 Desk Wire 帧，确认通知与原装屏更新。
5. 如设备不支持星闪，记录 `UNSUPPORTED`，不谎报 SDK 失效；可用另一兼容手机再测。

### Step 4：Wi-Fi

1. 将小鸿与控制端置于受控网络并完成设备授权。
2. 用 HTTP API 修改桌牌；与 BLE/SLE 的结果一致。
3. 测试不同通道同时下发的 revision 冲突、断网恢复。

### Step 5：共存与功耗

1. 记录 BLE+SLE、BLE+Wi-Fi、SLE+Wi-Fi 及三模并存的真实表现。
2. 如 WS63 无法三模同时开启，应明确改为时分调度或互斥，不宣称三模并发。
3. 记录 TFT 更新时延、失联率、RAM 峰值、重启恢复、长时间运行稳定性。

## 用户回传资料（**仅 Gate C 开始时再需要**）

- 小鸿板卡/原屏照片和烧录串口日志
- HarmonyOS 7 手机型号、系统版本与星闪能力检测结果
- BLE/SLE 扫描及连接日志，Desk ACK 和桌牌变化前后照片
- 每项测试的实际通过/失败结果

不需要提前提供私人设备标识、真实 MAC、登录令牌或用户账户信息。分享日志前请遮盖敏感信息。

## 判定

只有真实 WS63 与 HarmonyOS 手机完成上述闭环，才可以把项目状态从 `SOFTWARE VERIFIED` 升级到 `HARDWARE VERIFIED`。对于双小鸿官方 SLE UART 示例，第二块板卡是可选的独立参考验证，并不是手机直连验证的必需前置条件。
