# 实机验收关卡 · XiaohongDeskCard

> 状态：NOT READY。现在不需要用户接线、烧录或提供手机日志。
>
> 已知可用设备：小鸿 AI WS63 原装屏 1 台、HarmonyOS 7 真机 1 台。
> 具体手机型号、NearLink 硬件支持、手机开发者选项等尚未核对。系统是 HarmonyOS 7 **不等于** 手机一定具备 NearLink 硬件。

## Gate A：App 首次编译/安装启动 — 用户已验证通过（后续功能验证仍在进行）

- [x] C99 Desk Core / Desk Wire 帧编解码
- [x] BLE/SLE Mock 分片与真实 C99 Core HTTP 模拟器
- [x] 16-byte business ACK、CRC32 与请求 ID 关联，宿主机自动化测试
- [x] HarmonyOS Stage 工程、编辑 UI、BLE/SLE 扫描和 SDK 写入源码初稿
- [x] 三模入口统一有界命令队列、LVGL 240×240 界面与 SDK 无关 glue，在宿主机模拟编译/测试通过
- [x] BLE/SLE 原始分包共用 Radio Ingress，绑定授权状态、断连清理、业务 ACK 与连接 token 关联均通过主机测试
- [x] 浏览器 BLE/SLE 模拟入口经过真实 C99 Radio Ingress、Dispatcher 和 ACK 完整路径
- [x] 原始无线数据加入确定性错误输入压力测试，并纳入 C UBSan 自动检查（非安全认证）
- [x] 小鸿 OpenHarmony 源码布局离线预检查器及测试（不等于执行真实 SDK 构建）
- [x] **2026-11-19 用户本地 DevEco Studio / SDK 6.0.2(22) Debug HAP 编译成功，并在 HarmonyOS 7 手机安装启动**；共享 CI 仍不能替代 DevEco 编译。
- [x] 软件侧校验分包恢复、CRC、超时及预写入安全回退策略（Node 22 + C99 测试；非无线实测）
- [ ] 指定安全的受控局域网/模拟器互联测试方案

## Gate B：SDK 编译验证（无需设备）— 未开始

- [ ] 固定并记录小鸿 WS63 原仓库及 SDK 的实际 commit、构建说明和工具链
- [ ] 编译原始官方样例，保存成功日志与产物校验和
- [ ] 将 `desk_core`、`desk_wire`、`desk_packet`、`desk_dispatcher`、双槽 NV 和 LVGL/WS63 CMSIS 适配层真正植入上游 SDK GN 构建；双槽 NV 的 C99 逻辑已完成，但真实 Flash HAL 仍待绑定
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

### 适配依据

详细工程接入路径、事件流与离线源码预检查见 [WS63_INTEGRATION.md](WS63_INTEGRATION.md)。当前 CI 已运行 C99、LVGL 测试替身、真实 LVGL 8/9 头文件/库及 CMSIS 官方头文件编译；仍没有执行上游 WS63 `hb build` 或烧录固件。


### 软件侧最新完成项（不需要实机）

- [x] 双槽 Flash/NV 日志、CRC32、最终提交标记与 248 个写入中断位置故障注入
- [x] SLE 队列 → NV → LVGL → ACK → 冷启动恢复主机联动测试
- [x] UI 执行前实时核验授权，撤销绑定后排队中的请求不能修改内容
- [ ] 将 NV 接入真实 WS63 SDK Flash HAL；SDK 构建和实机断电测试仍未开始


### 本轮额外通过的软件验证

- [x] 官方真实 LVGL 8.3.11 与 LVGL 9.3.0：均完成宿主机编译、240×240 对象更新与无屏幕刷新测试
- [x] 离线生成 WS63 移植源码包（12 C + 12 H）、`BUILD.gn` 审查片段与 SHA-256 清单
- [x] 生成脚本具有拒绝覆盖及缺文件失败的自动化测试
- [ ] 确认小鸿实际 LVGL commit/版本、中文字体及 GN 条件编译，并完成真正的 WS63 SDK 构建
- [x] 开发者已完成 DevEco Debug HAP 编译、手机安装及启动；真正 WS63 无线链路与板端屏幕未验证


### Wi-Fi HTTP 软件侧已完成（无需设备）

- [x] Wi-Fi 严格 JSON v1 解析、UTF-8 Unicode、整数边界、状态与业务 ACK JSON 编码
- [x] Wi-Fi 授权入队、UI 实际执行前所有权复核、同 BLE/SLE 共用 Dispatcher 的测试
- [x] 30,000 组正常/畸形 JSON 输入压力测试，纳入 CTest/UBSan
- [x] 桌面 HTTP 模拟器的 Wi-Fi 更新已走真正 C99 JSON → Wi-Fi Ingress → UI Dispatcher → ACK
- [ ] 真实 WS63 Wi-Fi HTTP socket/server、GET 状态快照、认证/TLS、ACK 任务队列及 SDK 编译，未执行


### 本轮完成：UI 状态读取与通信响应桥接

- [x] Wi-Fi GET 与所有 SET 指令共用同一个 FIFO；快照只由 UI owner task 读取
- [x] GET/PUT 执行前再次核验连接授权；权限撤销后的 GET 不返回任何桌牌内容
- [x] 已保存内容与最后成功显示内容分离，渲染失败时可通过状态接口发现差异
- [x] C99 `desk_reply_mailbox`：BLE/SLE/Wi-Fi 响应固定容量、连接 token 对应、消息满丢弃诊断、不覆盖旧回复
- [x] 桌面模拟器 GET/PUT 和 BLE/SLE Mock 均通过同一 UI 队列和回应邮箱
- [ ] 真实 SDK 接入 CMSIS 事件通知、socket/无线发送工作队列与 backpressure；**WS63 固件编译未完成**，但 App Debug HAP 首次编译与安装已验证


### 回传队列并发消费修复

- [x] BLE、SLE、Wi-Fi 工作线程按 `channel + peer_token` 定向领取业务 ACK / 状态响应
- [x] 断连后定向清除该连接剩余回复，不误删其他连接或其他无线模式使用的同号 token
- [x] 多任务乱序、容量上限、环形队列回绕与空闲消息清零测试
- [ ] 真实 WS63 SDK 的 BLE/SLE/HTTP worker 调用 `desk_reply_take_for`，并在实际发送前复核当前 generation（需要 SDK 编译；尚未开始）


### 最新 Gate A/B 进度：CMSIS 接口核验与 HAP 首次编译

- [x] 真实 CMSIS-RTOS2 v2.3.0 头文件下编译 `desk_ws63_cmsis.c` 成功（API 语法检查，不是 WS63 SDK 链接）
- [x] 4 字节消息大小检查、非阻塞队列投递、队列满时 LVGL 轮询恢复：宿主机模拟测试通过
- [x] SDK 源码暂存包包含 12 C + 12 H 及 SHA-256 清单
- [x] DevEco Studio 首次 **只编译不运行** 操作及回报日志清单已编写，见 [HAP_BUILD_HANDOFF.md](HAP_BUILD_HANDOFF.md)
- [x] **Gate A 首次 DevEco 构建与安装启动已完成**（开发者 2026-11-19 报告），见 [HAP_GATE_A_REPORT.md](HAP_GATE_A_REPORT.md)
- [ ] **Gate B 待执行**：真实小鸿 SDK 的 GN、CMSIS mutex/队列/事件、Flash 与 GATT/SSAP/HTTP 集成后才能运行 `hb build`
- [ ] **Gate C 待执行**：HarmonyOS 7 App 已能独立启动，但尚无可编译的 WS63 桌牌固件；板卡烧录与 BLE/SLE/Wi-Fi 真机联调均未开始


### 2026-11-19 Gate A 实际验证节点

用户反馈 App 已正常完成 Debug HAP 构建、安装及 HarmonyOS 7 真机启动；对应 Stage 启动窗口、ArkTS 严格模式问题已同步到 PR。**本地调试签名文件和密码不进入 GitHub 仓库**，见 [HARMONY_SIGNING_SECURITY.md](HARMONY_SIGNING_SECURITY.md)。

这只是**手机端 App 启动验证**，不能勾选 WS63 固件编译、原屏显示、BLE GATT、SLE SSAP 或 Wi-Fi 三模设备联调。
