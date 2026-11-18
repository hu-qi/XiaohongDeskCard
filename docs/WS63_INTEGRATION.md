# Xiaohong AI WS63 / OpenHarmony 7 适配指南（无真机阶段）

状态：**Desk Core + Dispatcher + LVGL v8 源码与宿主机测试已完成**；OpenHarmony WS63 SDK/hb 编译、板端 GATT/SSAP、中文字体和 Flash 存储适配 **尚未完成**。

## 一、上游基线

目标：**第一代小鸿 AI（xiaohong），不是 xiaohong-se / xiaohong-kb**；搭载 WS63、OpenHarmony 轻量系统、板载 ST7789 系列 240×240 TFT。

官方上游项目：https://gitcode.com/xiaohong-ai/vendor_atomgit

其 README 提供一条 OpenHarmony 7.0.0.38-Release 的源码同步基线：

```bash
repo init -u https://atomgit.com/xiaohong-ai/manifest.git \
  -b OpenHarmony-7.0.0.38-Release \
  -m default_xiaohong.xml -g ohos:mini --no-repo-verify -v
repo sync -c -j8
repo forall -c 'git lfs pull'
# 按上游 README 准备 mini WS63 prebuilts，再按提示 hb set
hb set     # mini -> atomgit -> xiaohong
hb build -f
```

推荐固定上游实际 commit SHA，而不只记录版本分支名。以上命令是官方说明的引用，**本项目尚未执行该 SDK 编译**，不能据此声明兼容或产出固件。

离线源文件预检查（无需真机）：

```bash
python3 tools/ws63_preflight.py --root /path/to/openharmony --json
```

该脚本只检查源码路径/锚点和 vendor commit；不会下载 SDK、编译、修改文件或操作硬件。上游版本变化可能导致路径失效，应人工审核后修复，**不要强制打补丁**。

## 二、最小植入边界

已经编写的可移植组件：

| 源文件 | 责任 |
|---|---|
| `firmware/core/src/desk_core.c` | 状态、授权、版本、持久化、渲染 |
| `firmware/core/src/desk_wire.c` | BLE/SLE 二进制命令与业务 ACK |
| `firmware/core/src/desk_packet.c` | BEGIN/CHUNK/COMMIT、CRC、超时 |
| `firmware/core/src/desk_dispatcher.c` | 三模接入统一 FIFO、有界队列、UI 任务串行执行 |
| `firmware/display/desk_view_lvgl.c` | 原屏 240×240 姓名、职位、组织与主题 |
| `firmware/platform/ws63/desk_ws63_glue.c` | 把 LVGL、Core、Queue、NV、ACK 连接成可调用接口 |

建议先将这六个源文件与对应头文件放入小鸿工程独立子目录，并经审核后添加到 `vendor/atomgit/xiaohong/xiaohong/BUILD.gn` 的 `static_library("xiaohong")` source/include 清单。

**本仓库暂不自动修改上游 BUILD.gn**：上游多版本的路径与条件编译不同，目前还未完成真实源码树/GN target 的固定验证。

## 三、接入官方显示事件队列

官方 VibeCoding 信号灯示例已展示 `g_disp_event_qid`、`lvgl_task.c` 与 `disp_driver.h` 的任务通信模式。桌牌遵循同一范式，不新建第二条 LVGL 任务：

```text
BLE GATT callback ─┐
SLE SSAP callback ─┼─> 解包/授权 -> desk_ws63_port_submit()
Wi-Fi HTTP task ───┘                   │
                                 post_display_event()
                                         │
                               g_disp_event_qid
                                         │
                            LvglTask/eDisp_Desk_Update
                                         │
                              desk_ws63_port_ui_drain()
                                         │
                           desk_core_apply() -> NV save
                                         │
                           desk_lvgl_view_render()
                                         │
                                on_result()
                                         │
                          ACK 任务 -> BLE/SLE notify
                                     HTTP response
```

### 接入点（概念对应，非可直接复制的上游补丁）

1. 在 `src/display/disp_driver.h` 的显示事件枚举中增加 `eDisp_Desk_Update`。
2. `src/display/lvgl_task.c` 初始化已有 LVGL 之后，提供与 `desk_ws63_callbacks_t` 对应的真实 NV、队列、锁、ACK 回调，并 **只初始化一次** `desk_ws63_port_init`。
3. 由 `post_display_event` 将 `eDisp_Desk_Update` 投递到官方 `g_disp_event_qid`；UI 任务处理该事件时调用 `desk_ws63_port_ui_drain`。
4. BLE/SLE Server 回调只负责解包、检查绑定权限并提交请求；不直接调用 LVGL，不能阻塞事件队列。
5. on_result **仅入队** ACK，不能在 UI 任务内执行长时间的 GATT/SSAP 写入。ACK 由各自通信工作任务发送。

注意 `desk_ws63_port_t` 内含多个固定容量的命令与分包缓冲对象，建议作为静态/全局实例放在合适的 RAM 区域，不要在只有几 KB 栈空间的任务里临时分配它。

## 四、仍需验证的四个板级接口

**1. NV 存储**：`load/save` 需实现掉电安全双槽或等效机制、内容校验和版本存储。现有宿主机 `snapshot.bin` 不是 Flash/NV 适配。

**2. UI 事件/互斥**：`post_display_event` 需基于小鸿现有 CMSIS RTOS 消息队列实现；`lock/unlock` 需适配 SDK 的同步原语。该模块不允许从 ISR 中调用 `desk_dispatcher_submit`。

**3. 无线**：BLE Peripheral/GATT Server 和 SLE SSAP Server 的服务/属性表、认证、通知与 MTU 均待 WS63 SDK 实际编译；只拥有手机端连接原型不代表两端协议已通。

**4. 中文字库**：`lv_label_set_text` 已接受 UTF-8，仍需确认 LVGL 字库覆盖姓名/组织名；不存在的中文字形不能靠 UTF-8 校验解决。需结合上游字库文档处理缺字和字体大小。

## 五、电脑端已可运行的验证

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
node --experimental-strip-types --test tests/router.test.mjs
```

其中以下三个新增测试重要：

- `desk_dispatcher_tests`：FIFO 排序、队列满返回 BUSY、多入口 ACK、无权限不刷新
- `desk_lvgl_view_host_tests`：LVGL v8 API 形式、240×240 标签布局、三色主题、Unicode 字符串、队列到显示更新
- `desk_ws63_glue_host_tests`：用替代 NV/消息队列/ACK 回调验证初始化、显示、ACK、断电恢复逻辑

它们使用的是 **主机端 LVGL 测试替身**，不是 OpenHarmony SDK 编译或 ST7789 显示测试。

## 六、真正的验收仍然放在最后

依次完成：

1. [Issue #7：DevEco HAP 与 WS63 SDK 无实机编译](https://github.com/hu-qi/XiaohongDeskCard/issues/7)
2. [Issue #8：小鸿 AI + HarmonyOS 7 真机验收](https://github.com/hu-qi/XiaohongDeskCard/issues/8)

到 Issue #8 才需要用户配合烧录和手机操作。HarmonyOS 7 手机是否支持 SLE，要通过实际设备能力检查，不能仅凭系统版本作出判断。
