# 小鸿 AI · CMSIS-RTOS2 显示任务接入（无实机 Gate B 准备）

**现状：适配层 `desk_ws63_cmsis.[ch]` 已完成宿主机测试，并通过 ARM 官方 CMSIS-RTOS2 v2.3.0 头文件的 C99 编译检查。未与小鸿实际 `lvgl_task.c`、`g_disp_event_qid` 或 WS63 SDK 链接。**

## 已实现接口

`firmware/platform/ws63/desk_ws63_cmsis.h`：

- `desk_ws63_cmsis_init(adapter, queue, update_event)`：验证已有 CMSIS 事件队列的消息大小**必须恰好为 4 字节**，否则拒绝启用，以免 `osMessageQueuePut` 按大于临时事件值的大小访问数据。
- `desk_ws63_cmsis_post_update(adapter)`：以 `osMessageQueuePut(queue, &uint32_event, 0, 0)` 非阻塞投递，记录投递失败次数。
- `desk_ws63_cmsis_notify(ctx)`：用于现有 Desk Dispatcher/WS63 Glue 的显示通知回调；只投递消息，不调用 LVGL。
- `desk_ws63_cmsis_ui_poll(adapter, port)`：由**唯一 LVGL 任务**处理已排队内容，调用 Desk Core、Flash/NV 存储、原屏绘制与业务结果回传。

## 待人工审核的真实小鸿接入点

1. 从用户本机固定 SHA 的 OpenHarmony 源码树中核对 `vendor/atomgit/xiaohong/xiaohong/src/display/lvgl_task.c` 与 `src/display/disp_driver.h`。
2. 确认 `g_disp_event_qid` 的实际 `osMessageQueueNew(count, msg_size, attr)` 或等价初始化方式：若消息不是 4 字节事件枚举，就**不能**直接复用此适配层；需改为实际消息结构并重测。
3. 在上游显示事件枚举增加唯一 `eDisp_Desk_Update`（值必须来自上游枚举，不得任意占用现有 ID），在 LvglTask 消息处理分支调用 `desk_ws63_cmsis_ui_poll`。
4. **同一个 LVGL 任务的正常周期**中也必须调用一次 `desk_ws63_cmsis_ui_poll`，用于恢复 CMSIS 消息队列满、唤醒事件丢失时已经成功入 Desk Dispatcher 的命令。不要在第二个通信任务直接调用它。
5. `desk_ws63_cmsis_t` 应与 `desk_ws63_port_t` 一样作为生命周期清晰的静态对象。平台 `desk_ws63_callbacks_t.context` 有多个回调统一复用，需由包装结构间接访问 CMSIS 适配、Flash、连接授权和回复邮箱。
6. 给 `desk_dispatcher` 与 `desk_reply_mailbox` 的 lock/unlock 分别接入 SDK 可用的 CMSIS mutex；不在 ISR 中调用跨模块入队。网络工作线程不得直接操作 LVGL。
7. 上游实际 `BUILD.gn` 中需把 `desk_ws63_cmsis.c` 加入显示编译条件，`tools/ws63_overlay.py` 现在生成带有此模块的**12 C + 12 H** 暂存包。
8. 真实 SDK 的 RTOS mutex 创建、消息队列管理与 ACK worker 线程仍需实现。这份适配层**不能独立形成可烧录镜像**。

## 电脑端验证证据

当前 GitHub Actions 分别运行：

- CTest `desk_ws63_cmsis_host_tests`：在模拟 CMSIS-RTOS2 事件队列下检查 4B 消息长度、非阻塞投递、队列满时的恢复轮询和固件 UI/glue 更新。
- `Official CMSIS-RTOS2 v2.3.0 header compile`：从 ARM `CMSIS_6` v6.2.0 tag 对应 commit `6f0a58d01aa9bd2feba212097f9afe7acd991d52` 检出真实头文件，只编译 `desk_ws63_cmsis.c` 并进行 C99 严格类型/警告检查。**这不是 WS63 的实际 RTOS 库链接或 `hb build`。**
- 原有真实 LVGL 8.3.11 和 9.3.0 的 Headless 编译与测试。

权威 CMSIS-RTOS2 参考：
https://arm-software.github.io/CMSIS_6/main/RTOS2/group__CMSIS__RTOS__Message.html

## 与用户协作的时间点

- **现在可以先执行 HarmonyOS App 的 DevEco Gate A 只编译检查**，步骤见 [HAP_BUILD_HANDOFF.md](HAP_BUILD_HANDOFF.md)，不用连手机。
- **固件编译 Gate B** 仍需上游原始源码的实际 GN/Flash/BLE/SLE/HTTP 适配；完成后再告知用户所需 Ubuntu 工具链和日志要求。
- **手机运行和真机联调 Gate C** 暂不进行。
