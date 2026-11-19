# XiaohongDeskCard 协议 v1 · BLE / SLE / Wi-Fi 统一桌牌业务

**状态：C99 软件协议、模拟器及 Host CI 已实现。真实 WS63 BLE/SLE/HTTP 服务端与 HarmonyOS HAP 尚未通过 SDK 编译。**

## 1. 同一业务数据模型

所有通道最终交给同一个 `desk_set_command_t` / `Desk Core`：

```json
{
  "requestId": 55,
  "expectedRevision": 1,
  "content": {
    "name": "张三",
    "title": "技术负责人",
    "organization": "OpenHarmony 社区",
    "theme": "classic"
  }
}
```

- `requestId`：1 至 4294967295 的无符号整数，不是字符串；与业务 ACK 关联。
- `expectedRevision`：uint32，乐观并发版本；不同传输共用一份设备 revision。
- `content`：四个**必填**字符串，UTF-8 字节上限依次为姓名 48、职位 64、组织 96、主题 16。职位与组织允许空字符串。
- `theme`：`classic` / `modern` / `minimal`。
- 保存成功前不修改状态；保存、LVGL 渲染完成后再生成业务结果。失败可由业务 ACK 确认。

当前 Desk Core 仅记住 RAM 中最后一笔请求。跨重启防重放、配对/绑定及安全凭据存储还需要 WS63 平台支持。

## 2. 三模各自的传输方式

| 通道 | 客户端 | 设备端 | 共同的最终处理 |
|---|---|---|---|
| BLE GATT | HarmonyOS GATT Central | WS63 GATT Server（SDK 待接入） | C99 分包 → Desk Dispatcher |
| SLE SSAP | HarmonyOS NearLink Client | WS63 SSAP Server（SDK 待接入） | 同一套 C99 分包 → Desk Dispatcher |
| Wi-Fi HTTP | HarmonyOS NetworkKit | WS63 HTTP Server（SDK 待接入） | C99 严格 JSON → Desk Dispatcher |

BLE/SLE 用二进制帧与 BEGIN/CHUNK/COMMIT，并通过特征/属性通知发送 **16 字节业务 ACK**。定义与暂定 UUID 详见 [WIRE_V1.md](WIRE_V1.md)。

Wi-Fi 采用：

- `GET /api/v1/desk/status`：授权后读取 owner task 的内容快照；
- `PUT /api/v1/desk/content`：授权后提交上述 JSON，正文最多 1024 字节；
- 符合 `DeskAck` 的响应格式：`{"result":"OK","transport":"WIFI_HTTP","state":{...}}`。

完整的 Wi-Fi JSON 校验、HTTP 状态/错误映射、令牌要求、UI ACK 时序与测试范围详见 [WIFI_HTTP_INTEGRATION.md](WIFI_HTTP_INTEGRATION.md)。

## 3. 一致性与安全规则

1. **单一版本号**：三模共用 `desk_core_t.revision`。写入前必须提供当前期望 revision，冲突返回 CONFLICT。
2. **同一 owner task**：三条通道只负责收包、授权和入队；保存 Flash、LVGL 更新必须串行处理。
3. **二次授权复核**：请求入队后到实际执行之间，必须再次确认连接代际与设备所有权。权限撤销时返回 UNAUTHORIZED，不更新。
4. **业务 ACK，而非无线确认**：GATT/SSAP 写入或 HTTP 数据读取成功不代表桌牌已显示。只有 UI 任务确认后才能报告结果。
5. **安全回退**：SLE → BLE → Wi-Fi 的自动回退仅限**开始写入之前**。若数据已发送但 ACK 超时，结果未知，先重新读状态，不跨通道盲目重发。
6. **隐私与鉴权**：生产 Wi-Fi GET 和 PUT 均必须鉴权。CRC32 只检测误码，不能替代配对、绑定、TLS 或抗重放机制。
7. **固定内存上限**：BLE/SLE 单帧最大 384B；Wi-Fi JSON 最大 1024B；总入队容量固定。超限返回错误，不执行部分写入。

## 4. 业务结果

| Result | 含义 |
|---|---|
| OK | 已完成当前请求（或完全相同的最近请求重绘） |
| BAD_INPUT | JSON/UTF-8/字段/长度无效 |
| UNAUTHORIZED | 未授权或执行时绑定已撤销 |
| CONFLICT | expectedRevision 不匹配 |
| DUPLICATE_ID | 最近相同 requestId 与内容不一致 |
| STORE_ERROR | 持久化失败 |
| RENDER_ERROR | 保存后显示失败，需读取当前状态 |
| REVISION_EXHAUSTED | Revision 达到 uint32 最大值 |

`GET status` 响应同时携带 `content`（已保存配置）与 `display`（最后成功渲染内容），两者在异常情况下可不同。

## 5. 验证与剩余工作

已在 GitHub Actions 对 C99 Desk Core、BLE/SLE 分包、Wi-Fi JSON、NV 双槽、统一队列、LVGL 真实库（8/9）进行电脑端测试。模拟器的 BLE/SLE/Wi-Fi 都使用同一套 C99 设备端业务逻辑，**没有实际无线通信或可烧录镜像**。

下一阶段 [#7 SDK 无实机编译](https://github.com/hu-qi/XiaohongDeskCard/issues/7)；最后才进入 [#8 小鸿 AI + HarmonyOS 7 真机验收](https://github.com/hu-qi/XiaohongDeskCard/issues/8)。
