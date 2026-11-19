# XiaohongDeskCard · Wi-Fi HTTP 设备端接口 v1

**状态：C99 JSON 编解码 + Wi-Fi 请求入队 + ACK/状态序列化已通过宿主机测试。实际 WS63 HTTP 服务、网络连接、TLS/设备认领仍待 SDK 实现。暂不需要实机。**

## 1. 为什么不能直接在 HTTP 回调里修改桌牌

三种通信方式共享 **Desk Core、revision、Flash/NV、LVGL owner task**：

```text
WS63 Wi-Fi socket/HTTP worker （尚未接入 SDK）
  │  限制 Content-Length / 认证设备拥有者 / 解析 Content-Type
  ▼
desk_wifi_submit_put()   ← 本仓库已实现的 C99 API
  │  desk_http_decode_set() [1..1024B JSON、严格 UTF-8、无堆分配]
  │  desk_dispatcher_submit() [DESK_CHANNEL_WIFI, peer_token]
  ▼
g_disp_event_qid → 唯一 LVGL 任务
  │  authorize_on_ui() 再核验连接代际/权限
  │  desk_core_apply() → Flash/NV save → LVGL render
  ▼
desk_dispatcher.on_result(channel, peer_token, Desk ACK 16B)
  ▼
HTTP worker ACK 队列（真实 SDK 尚待接入）
  │  peer_token 与 requestId 均必须一致
  │  desk_wifi_match_ack() 验证业务 ACK CRC/请求编号
  │  UI 任务已复制真实 content/display/revision 快照
  ▼
HTTP 200/4xx/5xx JSON 响应（已实现 JSON 编码器，尚无 SDK socket writer）
```

**收到 PUT 请求 ≠ 修改成功；进入 Dispatcher 队列 ≠ 已渲染；只有 UI 执行并发回匹配的业务 ACK，才允许响应业务结果。**

## 2. 固定 API

### GET /api/v1/desk/status

**必须先验证已绑定的有效访问凭据**，再由 UI owner task 提供状态快照。不可在 Wi-Fi worker 同时读取可能正被 LVGL 任务写入的 Desk Core 内存。

返回结构：

```json
{
  "revision": 1,
  "content": {
    "name": "张三", "title": "开发者",
    "organization": "OpenHarmony", "theme": "classic"
  },
  "display": {
    "name": "张三", "title": "开发者",
    "organization": "OpenHarmony", "theme": "classic"
  },
  "device": "XiaohongDesk",
  "hardwareValidated": false
}
```

`content` 为已持久化目标值，`display` 是最后**成功渲染**的内容；渲染失败后可暂时不同。`hardwareValidated` 由受信任的板级固件/验收配置明确设置，主机 Mock 固定为 `false`，不能凭网络连通就设置 `true`。

### PUT /api/v1/desk/content

- `Content-Type: application/json`；
- `Authorization: Bearer <设备绑定后的有效令牌>`，真实令牌由设备安全配对流程产生；**禁止提交、打印或明文保存在仓库里**；
- `Content-Length` 为 **1—1024 字节**；不允许无限流、未声明的分块、超限请求；
- JSON 对象严格包含如下键，字段顺序可以不同，重复键和未知字段拒绝。

```json
{
  "requestId": 55,
  "expectedRevision": 1,
  "content": {
    "name": "李四",
    "title": "开发者",
    "organization": "OpenHarmony",
    "theme": "modern"
  }
}
```

字段约束：`requestId` 为非零 **uint32**；`expectedRevision` 为 uint32；姓名 UTF-8 最多 48 字节，职位最多 64，组织最多 96，主题仅限 `classic`/`modern`/`minimal`。字符串字段必须存在；职位、组织可为空。

UTF-8 多字节原样接受；JSON `\\uXXXX` 转义和合法的代理对会解码为 UTF-8。非法 Unicode/重复字段/JSON 尾随数据/过大的整数/无效控制码全部拒绝，不截断字符串。

返回示意：

```json
{
  "result": "OK",
  "transport": "WIFI_HTTP",
  "state": {
    "revision": 2,
    "content": {
      "name": "李四", "title": "开发者",
      "organization": "OpenHarmony", "theme": "modern"
    },
    "display": {
      "name": "李四", "title": "开发者",
      "organization": "OpenHarmony", "theme": "modern"
    },
    "device": "XiaohongDesk",
    "hardwareValidated": false
  }
}
```

HTTP 状态建议：

| 条件 | HTTP | 业务 result |
|---|---:|---|
| Desk Core 保存、渲染成功 | 200 | OK |
| 无效 JSON、字段或超限 | 400 | BAD_INPUT |
| 未绑定或令牌过期 | 401/403 | UNAUTHORIZED |
| revision 冲突、重复 ID 不一致 | 409 | CONFLICT / DUPLICATE_ID |
| 设备队列已满 | 503 | 不应伪造已执行的 Desk ACK |
| Flash/NV 错误、显示失败 | 500 | STORE_ERROR / RENDER_ERROR |
| ACK 等待超时或断线 | 504/503 | 结果**未知**，必须读取状态确认 |

## 3. 已实现的 C99 API

- `desk_http_decode_set`：无动态内存、严格 schema 的 JSON → `desk_set_command_t`，命令只在全部校验通过后写入输出。
- `desk_wifi_submit_put`：授权门禁、JSON 解析、分配来源为 Wi-Fi 的 Dispatcher 条目，队列满返回 BUSY。
- `desk_wifi_match_ack`：验证 CRC 和 requestId；外层 HTTP 任务还必须核验 `channel == WIFI` 和 `peer_token`。
- `desk_http_encode_status` / `desk_http_encode_reply`：将 UI 任务复制的配置/显示状态写入固定上限的缓冲区；任何编码失败不得发送截断 JSON。

这套代码可编进 WS63 `BUILD.gn`，但没有依赖某个尚未核对的 socket/HTTP 框架的符号名。实际 HTTP Server 请从小鸿上游网络样例与对应 SDK 源码确认；部分上游工程存在 `net_http_server.c`，应通过 `tools/ws63_preflight.py` 核实，而不是直接假定存在。

## 4. 安全与生命周期

- Wi-Fi GET **也需要鉴权**，否则会向同网段其他设备泄露姓名、职位、组织信息。
- 生产 HTTP 服务不能向公网开放；真实令牌比较应为常量时间或使用 SDK 安全验证接口。无 TLS 的 HTTP 不应传送生产绑定令牌。
- GET/PUT 的认证材料、连接代际、超时/取消、请求 ID 匹配均由 WS63 HTTP 工作任务维护。
- 请求已经进入 UI 队列后，即使 HTTP 断线，命令仍可能写入 Flash；**超时绝不可跨无线通道盲目重发**。
- 拒绝在 ISR 中调用本接口。网络工作任务不直接操作 LVGL/NV，UI 任务不阻塞等待 HTTP socket。
- 设备端必须配置有限大小的 ACK/状态响应队列，队列满时不可冒充已成功响应。

## 5. 无实机验证

```bash
cmake -S . -B build && cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 tools/ws63_overlay.py --output build/ws63-overlay
```

测试范围：

- `desk_http_json_tests`：汉字与 Unicode、JSON 字段长度、顺序、重复键、乱码、转义、整数边界、GET 和 PUT JSON 编码；
- `desk_http_json_fuzz_tests`：3 万组确定性的正常/异常 Wi-Fi JSON 输入；
- `desk_wifi_ingress_tests`：HTTP → 同一 Dispatcher → UI 执行前授权复核 → Flash/显示回调 → ACK、队列背压与状态序列化；
- `desk_sim_http_tests`：Mac/Linux 本机 HTTP 模拟器的 Wi-Fi 路径现已调用真正的 C99 JSON 解析和 Dispatcher，不再直接跳过队列修改 Desk Core。

**重要区别**：桌面模拟器 `GET /api/v1/desk/status` 为了本机网页预览允许未鉴权读取；它只能监听 localhost，不能当作真实设备服务器部署。正式 WS63 HTTP GET 必须鉴权。

进入 [Issue #7：实际 SDK 编译](https://github.com/hu-qi/XiaohongDeskCard/issues/7) 时才需要绑定真正的 HTTP Server、Wi-Fi 初始化、TLS/认证、LVGL 状态快照和 ACK 队列。烧录与手机实测仍保留在 [Issue #8](https://github.com/hu-qi/XiaohongDeskCard/issues/8)。
