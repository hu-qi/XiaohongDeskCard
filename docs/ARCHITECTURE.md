# 技术架构 · WS63 原屏三模通信

状态：设计草案 v0.1。此文档是**计划架构**，不是已编译通过的源代码。

## 分层

```text
HarmonyOS ArkTS App / CLI / 后续元服务
       │       │         │
     BLE     NearLink    Wi-Fi
     GATT      SSAP       HTTP
       └────────┼──────────┘
          Desk Protocol
                 │
           Desk Command
                 │
             Desk Core
      auth / validate / revision
      persist / ACK / event queue
                 │
          LVGL UI task
                 │
        ST7789 TFT 240x240
```

## 固件模块（拟建）

```text
firmware/
  app/                       # 初始化与生命周期
  core/                      # desk_state、desk_command、desk_storage
  protocol/                  # wire codec、分片组装、CRC、ACK
  transports/
    ble/                     # GATT Server / notify
    sle/                     # SLE SSAP Server / notify
    wifi/                    # HTTP endpoint
  display/                   # LVGL UI adapter + 240x240 layout
  platform/ws63/             # 适配小鸿 SDK / 构建配置
apps/
  harmony/                   # ArkTS 原生 App（首期）
  atomic-service/            # 预留
  wechat-miniapp/            # 预留
tools/
  protocol-test/             # 协议测试/串口诊断
docs/
```

## 角色与连接

- BLE：小鸿 = GATT Peripheral/Server；手机 = Central/Client。
- SLE：小鸿 = SLE SSAP Server；HarmonyOS 手机 = SSAP Client。小鸿官方 `13_sle_uart` 示例是**双小鸿通信**，手机对接需另行实测，不能当作已验证。
- Wi-Fi：小鸿接入 2.4GHz AP 并提供本地 HTTP 接口；App/CLI 同局域网访问。

## 并发边界

1. Radio 回调只接收数据并投递消息，不得直接改 UI。
2. Desk Core 串行化设备内容提交；按设备 revision 避免旧消息覆盖新消息。
3. 仅 UI 任务调用 LVGL。
4. NV/Flash 写入做节流，避免每个分片落盘；仅完整验证且成功提交后持久化。
5. 三条无线适配器独立启停；先单链路，后做 BLE+SLE、BLE+Wi-Fi、SLE+Wi-Fi、三模共存矩阵，记录实测。

## HarmonyOS 端

- 统一 `DeskTransport` 抽象：discover / connect / send / subscribe / disconnect。
- BLE：`BleGattTransport`；NearLink：`SleSsapTransport`；Wi-Fi：`WifiHttpTransport`。
- 启动时查询 NearLink 能力（官方 `manager.isNearLinkSupported()`）；需要 `ohos.permission.ACCESS_NEARLINK` 等相应权限并动态授权。
- NearLink 不支持或连接失败时明确提示并允许 BLE/Wi-Fi 降级，不自动假设任何华为手机都支持 SLE。
- 元服务/小程序未来复用业务 API；实际设备短距通信能力需各宿主独立验证。

## 上游参考及版权边界

- 小鸿工程：https://xiaohong.atomgit.com/docs
- 小鸿星闪双板样例：`vendor/atomgit/xiaohong/samples/13_sle_uart`
- 华为 NearLink Kit：https://developer.huawei.com/consumer/cn/sdk/nearlink-kit/
- WS63 扩展 SDK 与 BLE 样例可作为调研参考，但必须核查所用版本的接口、编译配置和许可证。

不把未经硬件测试的性能、三模并发能力、手机星闪兼容性写成既成事实。
