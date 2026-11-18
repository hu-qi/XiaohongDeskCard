# Desk Protocol v0.1 · 三模统一业务协议（草案）

设计目标：**同一条桌牌业务命令**可以通过 BLE GATT、SLE SSAP 或 Wi-Fi HTTP 到达同一个 Desk Core。此处描述目标协议，不宣称已在小鸿设备实现。

## 1. 业务模型

```json
{
  "version": 1,
  "requestId": "req-0001",
  "op": "desk.setContent",
  "expectedRevision": 0,
  "payload": {
    "name": "张三",
    "title": "技术负责人",
    "organization": "OpenHarmony 社区",
    "template": "classic"
  }
}
```

- `requestId`：用于 ACK 关联和重试去重（相同 requestId + 相同内容返回已有结果）。
- `expectedRevision`：乐观并发控制；不匹配时拒绝覆盖，客户端应先刷新状态。首次配置时由客户端读取设备 revision，不默认无限覆盖。
- `version`：协议版本，当前为 1。
- `op`：初期允许 `desk.getStatus`、`desk.setContent`、`desk.getContent`。
- 字段长度、最大报文大小、字体可用字符集等界限需根据固件 RAM 和中文字库实测后定稿；收到非法或过大输入必须拒绝而非截断溢出。

## 2. 结果事件

```json
{
  "version": 1,
  "requestId": "req-0001",
  "status": "APPLIED",
  "revision": 1,
  "transport": "sle",
  "error": null
}
```

`status` 建议为 `RECEIVED`、`VALIDATED`、`APPLIED`、`PERSISTED`、`REJECTED`。对于 UI 成功，应以实际渲染结果为准；持久化失败必须单独上报。注：状态字段和转移尚需通过设备端实现核对。

## 3. 传输层

### Wi-Fi HTTP

- `GET /api/v1/desk/status`
- `PUT /api/v1/desk/content`
- 在完成设备认领后使用设备侧有效的鉴权凭据。
- JSON Body 直接携带业务模型。HTTP 成功只表示请求被受理，页面更新是否完成应按结果或状态确认。

### BLE GATT

- WS63：Peripheral/GATT Server；App：Central/GATT Client。
- 拟定义 Control（write）、Data（write）、Status（notify/read）特征；**UUID 待实际服务表确定后冻结**。
- 使用实际协商出的 ATT MTU 计算可用负载，不写死单包大小。
- 应用层完成 BEGIN → CHUNK → COMMIT → ACK；顺序号、长度、CRC32 和超时用于确保整包可靠性。
- 在绑定与权限检查完成前禁止执行变更。

### SLE SSAP

- WS63：SLE SSAP Server；ArkTS：SSAP Client。
- 采用与 BLE 相同的 BEGIN/CHUNK/COMMIT/ACK 业务状态机，由 SSAP 属性承载数据与响应。
- SSAP 服务/属性 UUID、可写长度、是否支持 notify 需依据 WS63 SDK 和 HarmonyOS NearLink Kit **实测后固定**。
- 现成 `13_sle_uart` 只能证明双小鸿透传案例存在，不能证明手机已能直接调用其服务。

## 4. 分片状态机

```text
IDLE ─BEGIN(length, crc32, requestId)→ RECEIVING
RECEIVING ─CHUNK(seq, bytes)→ RECEIVING
RECEIVING ─COMMIT→ VERIFYING
VERIFYING ─valid→ APPLYING → ACK(APPLIED) → IDLE
VERIFYING ─invalid→ NACK / RESET → IDLE
RECEIVING ─timeout/disconnect→ RESET → IDLE
```

建议初期仅允许一笔未完成的分片事务；缓冲区上限明确，不足时拒绝而不是动态无限申请。重传同序号必须可识别，不能重复拼接。CRC32 只作误码检测，不替代鉴权和加密。

## 5. 安全/一致性

- Wi-Fi：不允许匿名局域网写入；至少设备认领令牌 + 重放防护；上线远程能力需 TLS。
- BLE/SLE：链路层安全之外增加业务层授权；首次绑定可采用设备屏显一次性配对码。
- 设备状态 `revision` 单调递增；不同传输更新统一序列化，避免交叉覆盖。
- 所有输入须验证 UTF-8、大小限制、模板白名单；日志不得输出完整令牌。
- 未授权、分片校验失败、expectedRevision 冲突都不能修改已显示与持久化内容。

## 6. 未来兼容

后期电子纸屏幕只增加 Renderer/Display Adapter；Protocol 与 Transport 不变。若升级为大图片帧传输，另定义 `desk.pushFrame`，不要借用小文本指令的内存与传输上限。
