# XiaohongDeskCard · Flash/NV 双槽持久化 v1

**状态：可移植 C99 模块 + 模拟 Flash 测试已实现。WS63 实际 NV/Flash 驱动尚未接入，未进行设备编译或断电实测。**

源代码：
- [desk_nv_store.h](../firmware/core/include/desk_nv_store.h)
- [desk_nv_store.c](../firmware/core/src/desk_nv_store.c)
- [test_nv_store.c](../tests/test_nv_store.c)
- [test_nv_glue.c](../tests/test_nv_glue.c)

## 1. 目标与策略

桌牌更新采用 **先持久化、后渲染、最后回复业务 ACK**。掉电恢复时选取两个独立 Flash 擦除区域中 **revision 最大、CRC32 与提交标记完整、UTF-8 内容合法** 的记录。

```text
         Slot A（可恢复的旧版本）          Slot B（待提交的新版本）
              revision = 7                       ERASE
                  │                              WRITE 244B body
                  │                              READBACK verify
                  │                              WRITE 4B commit LAST
                  │                              READBACK verify
                  └──────────────────────────────► revision = 8
```

写入过程中如果断电，旧槽不会先被擦除；新的记录没有有效提交标记时不会被选用。重启后若新槽有效则使用新槽，否则回退旧槽。

### 固定字节格式：248 字节

| 偏移 | 长度 | 内容 |
|---|---:|---|
| 0 | 4 | magic `XHDN` |
| 4 | 2 | schema v1（小端） |
| 6 | 2 | payload 大小 228（小端） |
| 8 | 4 | revision uint32（小端） |
| 12 | 228 | 姓名 49B + 职位 65B + 组织 97B + 主题 17B，固定数组，NUL 结束/零填充 |
| 240 | 4 | 对字节 0–239 的 CRC32 |
| 244 | 4 | **最后写入**的提交标记 `0xC01117ED` |

该格式避免了依赖编译器 packed struct 的持久化布局，并拒绝无效 UTF-8、未知主题、缺少尾部 NUL、校验错误或不受支持的 schema。

## 2. 底层 WS63 HAL 必须保证

`desk_nv_io_t` 为设备侧提供三个回调：

```c
int read(void *ctx, unsigned slot, size_t offset, uint8_t *dst, size_t bytes);
int erase(void *ctx, unsigned slot);
int program(void *ctx, unsigned slot, size_t offset,
            const uint8_t *src, size_t bytes);
```

这三个 API **不是已经绑定的 WS63 SDK 接口**。实际适配时必须检查：

1. 两个槽必须属于独立的物理擦除区域（擦除一个不能损坏另一个）。
2. 擦除后填充值是 `0xFF`，program 满足 NOR Flash 1→0 位写语义。
3. `program` 支持 4 字节对齐，244B 正文与 4B 最终提交标记的写入顺序被实际落盘保证（必要时做同步或屏障）。
4. 提交标记写入之前已完成正文读回校验；标记写入后再次完整读回。
5. `read/erase/program` 必须在所在任务上下文可调用，不应在 ISR 中直接进行 Flash 写入；耗时和看门狗行为待评估。
6. 实际擦除块可能是 4KB 或更大，**248B 只是记录大小，不是 WS63 擦除扇区大小**。必须保留真实 SDK 所需的完整擦除区域和分区空间。

**不得**将两个槽映射到同一个 Flash 擦除块，也不得将 Mock 256 字节槽大小直接配置为真实 Flash 擦除大小。

## 3. Desk Core / WS63 Glue 接法

在板级启动阶段：

```c
/* 伪代码：io 由真实 SDK read/erase/program 包装提供。 */
desk_nv_store_t store;
desk_nv_io_t io = {flash_read_slot, flash_erase_slot,
                   flash_program_slot, &flash_context};
if (!desk_nv_init(&store, io)) {
    /* 不要继续开放桌牌写入；报告数据损坏/设备初始化故障。 */
    return;
}

/* desk_ws63_callbacks_t.load/save 可分别委托
 * desk_nv_load(&store, content, revision)
 * desk_nv_save(&store, content, revision)。
 * 回调上下文同时需要携带 UI 事件/授权/ACK 工作队列。 */
```

注意：`desk_ws63_port_init` **现在要求** `authorize_on_ui` 回调；它会在真正执行请求前复核连接代际和当前设备绑定状态，拒绝在排队期间已撤销授权的写入。

## 4. 故障处理

- 两槽均为空白：第一次启动，加载 Desk Core 默认内容。
- 一槽有效、一槽无效：恢复有效记录，下次保存修复失效槽。
- 两槽均有效：选择 revision 较大者。若 revision 相同却内容不同则拒绝启动写入。
- 两槽均损坏：`desk_nv_init` 失败，**不允许静默恢复出厂默认并覆盖证据**；后续设计受用户确认控制的恢复流程。
- Revision 达到 uint32 最大值：停止递增而不是回绕覆盖旧记录。
- 写入被中断或读回失败：保存返回错误；客户端不得仅凭传输写入成功宣称已更新。
- **安全边界**：CRC32 仅检测意外数据损坏，不提供抗篡改能力；设备身份/绑定密钥应单独安全存储。

## 5. 当前测试证据

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- `desk_nv_store_tests`：初始空白槽、递增 revision、重复内容幂等保存、旧槽回退、双槽损坏拒绝、读写故障，以及 **248 个写入中断位置**。
- `desk_nv_glue_host_tests`：模拟 SLE 请求经 Dispatcher → NV save → LVGL view → ACK → 重启并读取 Flash；同时测试请求排队后撤销授权。
- 在 CI 的 UBSan 模式下执行 C99 核心测试，可发现部分未定义行为，**不等于完整固件测试、掉电硬件测试或安全认证**。

## 6. 下一关

仍需 [Issue #7：DevEco / WS63 SDK 无实机编译](https://github.com/hu-qi/XiaohongDeskCard/issues/7)。接入真实 Flash HAL 并通过 `hb build` 后才形成可烧录候选固件。小鸿 AI 和 HarmonyOS 7 手机**仍无需现在操作**；实际烧录与断电测试归属 [Issue #8](https://github.com/hu-qi/XiaohongDeskCard/issues/8)。
