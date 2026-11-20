# XiaohongDeskCard · UI 与 BLE/SLE/Wi-Fi 响应队列（无实机）

**软件状态：已实现并进行 C99 宿主机验证；真实 WS63 CMSIS 消息队列、无线通知与 HTTP socket 尚未绑定。**

## 为什么必须两条队列

小鸿桌牌的 LVGL 对象和 Desk Core 存储状态由一个 UI owner task 维护。BLE/SLE/Wi-Fi 的多个网络任务不能并发读取 Core，更不能从无线中断/回调直接更新 LVGL。当前实现使用两条固定容量的消息队列：

```text
BLE GATT Worker  ─┐
SLE SSAP Worker  ─┼─→ desk_dispatcher （4 条共享 FIFO）─→ LvglTask
Wi-Fi HTTP Worker─┘                                      │
    │                         ┌─────────────────────────┤
    │                         │                         │
    │                  SET content                 GET status
    │                         │                         │
    │              复核连接代际与绑定权限        复核权限后读取当前状态
    │                         │                         │
    │              Flash save → LVGL render       Core + last rendered
    │                         │                         │
    │                      Desk ACK                  snapshot
    │                         └──────────┬──────────────┘
    │                                    ▼
    └────── desk_reply_mailbox（4 条有界 FIFO，非阻塞）
                               │
                    Communications worker
                               │
          检查 channel + peer_token + requestId
                               │
                   BLE notify / SLE notify /
                      HTTP 200/4xx response
```

生产固件的真实发送工作由 SDK HTTP/BLE/SLE 任务实现。本模块**不会自行连接网络**。

## 已实现的模块

- `desk_dispatcher_submit_status()`：把 GET 状态查询放到和 SET 更新**同一个 FIFO**，因此能保证先读/先写的执行顺序，不会在另一个线程同时查看半更新状态。
- `desk_dispatcher_process_one()`：对 GET/SET 都执行 `authorize_on_ui`，防止用户被解绑后仍看到姓名/单位信息。
- `desk_ws63_glue`：记录上次**成功渲染**的桌牌内容，GET 返回的 `content` 是保存配置，`display` 是显示内容；显示失败时二者允许不同。
- `desk_reply_mailbox`：拷贝并保存 16B Desk ACK 或状态快照，含通道枚举、短期会话 `peer_token` 和业务状态；工作线程 `pop` 后还必须核对仍存活的连接与 requestId。
- 两条队列都有固定上限，不通过网络流量无限分配堆内存；回传队列满时明确计数，不覆盖此前尚未处理的响应。

## 重要线程与安全边界

**入队前鉴权、UI 执行时再次鉴权、发送前再次核对连接。** 仅接收到可写 GATT 属性、已建立 SLE 链路或收到 HTTP GET，并不等于授权。

- GET 必须像 PUT 一样检查访问凭据；GET 响应中含有个人姓名、职位与组织信息。
- `on_status` 的 `desk_http_state_t *` 是 UI 回调临时数据，SDK 适配层必须同步复制到有界回传队列，不得保存悬垂指针。
- 回传队列满时 `desk_reply_push_*` 返回失败并增加 `rejected_full`。写入可能已完成，但 ACK 未送达，所以客户端结果必须为 **UNKNOWN**，并通过 GET 状态对账；**不可立即跨无线通道重发**。
- 当前 `desk_ws63_callbacks_t.enqueue_ack/enqueue_status` 是 `void` 回调：真实 SDK 适配层应检查邮箱入队结果并记录错误、限流或拒绝新请求；这个可移植源码**不保证网络通知一定发出**。
- `desk_reply_pop` 会清空已取出的邮箱槽，防止姓名/单位长期驻留在空闲消息槽中；工作任务的本地拷贝也须在发送后妥善清理。
- `desk_dispatcher` 的 `notify_ui` 只负责投递唤醒事件，不返回是否送达。真实 CMSIS 适配必须保证可靠唤醒或周期性重查队列，避免“入队成功但 UI 永不处理”。
- mailbox 的 `lock/unlock` 在生产中必须由 SDK mutex/临界区提供。测试夹具可用轻量锁计数。禁止从 ISR 直接调用本接口。
- 回传 mailbox 是短暂 RAM 队列，不是持久化防重放日志；安全设备绑定、nonce/session 及重启后的请求重放保护仍待 WS63 平台实现。

## 测试方法

```bash
cmake -S . -B build && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- `desk_status_fifo_tests`：先 GET → PUT → 后 GET 的 revision 顺序；排队后撤销授权时不泄露状态；Flash 保存成功但渲染失败时内容与显示状态不同；GET/PUT 共用背压。
- `desk_reply_mailbox_tests`：BLE/SLE ACK 与 Wi-Fi 状态消息按连接 token 路由，模拟满队列不丢旧包、拒绝非法 ACK/未授权响应内容。
- `desk_ws63_glue_host_tests`：真实 `desk_ws63_glue` 可提供原屏显示的最近成功结果，回复 GET，撤销授权后返回无数据错误。
- `desk_sim_http_tests`：桌面 Wi-Fi GET/PUT 与 BLE/SLE 模拟写入经同一队列和 mailbox 完成业务处理。只监听 localhost，无实际电波信号。

## 实际 WS63 绑定待办

1. 固定官方 SDK/commit，并在真实 OpenHarmony 7 源码树运行 `tools/ws63_preflight.py`。
2. 用 CMSIS 或上游实际 RTOS 锁/显示事件实现队列安全、唤醒和超时。
3. 为 BLE/SLE/HTTP worker 配置有界回传队列并接通 `desk_ws63_callbacks_t.enqueue_ack/enqueue_status`。
4. BLE/SLE 发送通知前核对连接 generation；HTTP GET/PUT 使用实际 token 与状态快照，考虑断线、过期和背压。
5. 完成 DevEco HAP 与 WS63 的真正编译和板卡适配。

**这仍不是可烧录固件**。需要编译固件或运行手机 App 时，另向用户明确告知工具链、源码 SHA、构建命令和需要提供的日志；目前暂时不需要实机操作。


## 多通信任务消费修正：定向领取与断连清理

早期 `desk_reply_pop()` 仅能用于**单一响应分发任务**。若 BLE、SLE、HTTP 三个 worker 分别直接 pop 同一队列，可能由错误 worker 先取走别的连接的响应，导致 ACK 丢失和 HTTP 超时。

现已提供：

```c
/* 某个 BLE worker：只领取匹配本连接 generation 的回复。 */
desk_reply_item_t item = {0};
if (desk_reply_take_for(&mailbox, DESK_CHANNEL_BLE, my_peer_token, &item)) {
    /* 发送前仍需检查本连接是否存活、绑定未撤销，
     * ACK 的 requestId 是否匹配正在等待的业务请求。 */
}

/* 连接关闭：先让连接 generation 失效，后清空剩余响应。 */
disconnect_generation_in_platform_state();
(void)desk_reply_discard_peer(&mailbox, DESK_CHANNEL_BLE, my_peer_token);
```

`desk_reply_take_for` 在持锁时扫描最多 4 项，取出**同通道/同连接 token 的最早响应**，保留其他连接的队列顺序；中间位置删除时压紧环形队列并清零空闲存储位。BLE 和 SLE 即便碰巧使用相同 token，也不能互相消费。

`desk_reply_discard_peer` 只删除指定通道和 token 的未领取回复，用于断连释放容量。**它不能撤销已经提交至 Desk Core 的命令，更不能撤回另一任务已经领取的响应**；接收线程在发送前仍必须进行当前连接代际和所有权校验。

新增 `desk_reply_demux_tests` 覆盖：SLE worker 在 BLE 前运行、错误 token 读取、邮箱满载、中间元素删除、环形回绕、同 token 跨通道、一个连接两条响应断连同时清理、空闲槽敏感字段清零。桌面模拟器现在使用 `take_for`，不再用无目标的全局 `pop`。

**仍待 SDK 绑定：** 真实连接 generation 来源、各通信 worker 唤醒与等待模型、CMSIS mutex，以及发送前再次校验 session。主机测试并未模拟多核真正同时运行的所有时序。
