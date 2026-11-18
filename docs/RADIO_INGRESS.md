# BLE / SLE 无实机接入规范：Desk Radio Ingress v1

状态：**C99 软件路径已编写并通过宿主机测试**；尚未连接 WS63 SDK GATT Server / SSAP Server，也尚未经过 DevEco HAP 编译或真实射频测试。

## 一、职责边界

```text
WS63 BLE GATT / SLE SSAP connection worker
  ├─ SDK 配对 / 应用级绑定校验（尚待实现）
  ├─ 每连接唯一 peer_token（新连接必须递增）
  ├─ desk_radio_link_t（一个连接独立实例）
  │     ├─ desk_radio_link_open()
  │     ├─ desk_radio_link_set_authorized()
  │     └─ desk_radio_link_receive(raw notification/write bytes)
  │             └─ desk_packet_feed(BEGIN/CHUNK/COMMIT)
  │                    └─ desk_dispatcher_submit()
  │                           └─ UI task 事件队列
  │                                  └─ Desk Core save/render
  │                                         └─ 16-byte Desk ACK
  └─ SDK transport worker ACK 队列 <────────┘
       ├─ 再次检查 peer_token / 连接存活状态
       ├─ 发 GATT notify 或 SSAP property notify
       └─ desk_radio_link_ack_complete()（同一个连接 worker）
```

核心组件：

- `firmware/core/include/desk_radio_ingress.h`：单连接对象、授权状态、输入状态、业务 ACK 关联。
- `firmware/core/src/desk_radio_ingress.c`：调用现有 `desk_packet` 与 `desk_dispatcher`；不会触碰 LVGL。
- `firmware/core/src/desk_dispatcher.c`：跨 BLE/SLE/Wi-Fi 有界 FIFO；仅 UI task 会处理请求。
- `firmware/platform/ws63/desk_ws63_glue.c`：将 UI 事件 / NV / ACK 回调接到小鸿现有任务体系。

## 二、WS63 连接工作任务接入流程（伪代码，不是板级 SDK 实现）

```c
/* 每个连接单独持有对象，必须先初始化为 0。 */
desk_radio_link_t link = {0};
uint32_t token = allocate_unique_connection_generation();

if (!desk_radio_link_open(&link, &port.dispatcher,
                          DESK_CHANNEL_SLE, token)) {
    /* 拒绝连接或记录系统错误 */
}

/* 仅在 OS 配对 + 设备所有权绑定都通过后授权。 */
desk_radio_link_set_authorized(&link, verified_bond_and_owner);

/* 连接工作任务接收到 GATT/SSAP 写入包 */
desk_radio_result_t result = desk_radio_link_receive(
    &link, value, length, monotonic_ms(), 2000u);

switch (result) {
case DESK_RADIO_PROGRESS:     /* 接受分片，还未提交业务命令 */ break;
case DESK_RADIO_QUEUED:       /* 已成功入 UI 队列，但还未显示 */ break;
case DESK_RADIO_NOT_AUTHORIZED: /* 拒绝更改并提示授权 */ break;
case DESK_RADIO_QUEUE_FULL:   /* 忙：不可宣称已更新 */ break;
case DESK_RADIO_AWAITING_ACK: /* 仅允许单笔未确认命令 */ break;
default:                      /* 无效分包 / 断线 / 参数错误 */ break;
}

/* UI 线程 on_result 只能入队 ACK；由本 worker 取出后发送。 */
if (ack.channel == DESK_CHANNEL_SLE && ack.peer_token == token) {
    /* SDK 调用: notify_property(ack.bytes) */
    /* 处理完成 ACK 后，在此工作任务上更新链路状态。 */
    (void)desk_radio_link_ack_complete(
        &link, token, ack.bytes, DESK_ACK_SIZE);
}

/* 断连：销毁未完成分包，不能把旧连接的 ACK 发给下一位用户。 */
desk_radio_link_close(&link);
```

完整的 WS63 Server API、MTU 和特征/属性 UUID 仍依赖实际 SDK 代码，不允许将以上伪代码粘贴成可烧录驱动并声称已集成。

## 三、安全与生命周期

1. **认证不可省略**：`authorized` 只能由 SDK 配对安全 + 应用设备绑定结果授予。广播名称、MAC、已连接均不是身份凭据。CRC32 不是安全签名。
2. **所有连接独立会话**：每次连接必须有新的非零 `peer_token`；一个连接有一个由零初始化的 `desk_radio_link_t`。BLE/SLE 链路不能共享组包缓冲。
3. **严格串行**：同一个连接实例的所有调用都在它自己的通信 worker 任务执行；UI task 只负责把 ACK 入队，不得跨线程直接调用 `desk_radio_link_ack_complete`，否则存在数据竞争。
4. **结果未知不是失败**：GATT/SSAP 写成功 ≠ 桌牌更新；只有收到 requestId 匹配的业务 ACK 才能确认。通知丢失时手机端不得跨通道盲目重发。
5. **队列满**：`DESK_RADIO_QUEUE_FULL` 表示命令没有进入 UI 队列；设备端应反馈明确的 BUSY/传输错误。当前协议尚无独立的业务 BUSY ACK，需在 WS63 SDK 集成前固定负载与错误映射。
6. **断开后的已提交请求**：已成功进入 UI 队列的命令可能在无线断开后继续执行。设备所有权撤销时需要在 owner task 处理前进行二次授权检查；当前 `authorized` 是提交时的快照，还不等于可撤销权限管理。
7. **ACK 分发**：`desk_dispatcher` 的 `on_result` 不得阻塞 LVGL 任务；必须投递到有容量与失败策略的通信队列，发送前检查原连接 token。发送队列满/通知失败时要记录日志并允许客户端通过状态接口进行恢复。
8. **超时与恢复**：分包接收有 32 位单调时钟超时；提交后的 ACK 可能超时，不得在不检查设备 revision/content 的情况下清除业务结果未知状态。

## 四、电脑端可运行验证

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`desk_radio_ingress_tests` 实测的是**真实 C99 模块间调用**：未授权拒绝、BLE/SLE 独立分片、队列入列但不提前渲染、错误 peer ACK、CRC 破坏、断连重入、背压和恢复。

浏览器模拟器的 BLE/SLE Mock 也已升级为：

```text
HTTP test endpoint
  → Desk Wire C binary frame
  → desk_radio_link_receive
  → desk_packet_feed
  → desk_dispatcher_submit
  → desk_core_apply
  → C business ACK
  → desk_radio_link_ack_complete
```

这比单独模拟分片更接近最终固件链路，**但仍没有产生 BLE/SLE 无线信号**。

## 五、实机前必须满足

先完成 [Issue #7：WS63 SDK + HarmonyOS HAP 编译](https://github.com/hu-qi/XiaohongDeskCard/issues/7)，固定实际 SDK 的权限、线程队列、存储、GATT/SSAP 服务与通知接口。之后才进入 [Issue #8：真机验收](https://github.com/hu-qi/XiaohongDeskCard/issues/8)。

截至本文件发布，**不需要用户烧录小鸿 AI 或操作 HarmonyOS 7 手机**。
