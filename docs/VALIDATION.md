# 验证计划 · 小鸿原装 240×240 TFT

状态：待实机执行；**下表均为计划，非已通过测试**。

## 0. 测试准备

- 小鸿 AI WS63 原机 1 台；如验证官方双板 SLE 样例，额外准备第 2 台小鸿/兼容 WS63 板卡。
- 支持 BLE 的 HarmonyOS 手机至少 1 台；支持 NearLink Kit 的 HarmonyOS 手机至少 1 台，并记录型号/系统版本/API 版本。
- USB 串口与烧录工具；2.4GHz Wi-Fi AP；开发主机；串口日志可导出。
- 源码基线：固定小鸿 vendor 仓库 commit、SDK 版本和构建环境。

## 1. 最小闭环

| ID | 测试 | 期望 |
|---|---|---|
| DISP-01 | 启动显示静态姓名、职位、组织 | 中文正常，布局不越界 |
| DISP-02 | UI 事件队列更新内容 | 不直接跨线程调用 LVGL |
| STORE-01 | 保存内容后断电重启 | 显示最后一次成功提交内容 |
| DATA-01 | 超长/非法 UTF-8/未知模板 | 拒绝，不崩溃、不覆盖 |
| AUTH-01 | 未绑定客户端写入 | 拒绝并记录错误码 |

## 2. 单链路测试

| ID | 链路 | 操作 | 验收 |
|---|---|---|---|
| BLE-01 | BLE GATT | 广播→扫描→连接→授权→写入 | 原屏刷新并返回业务 ACK |
| BLE-02 | BLE GATT | 发送多分片长文本 | 顺序重组、CRC 校验成功 |
| BLE-03 | BLE GATT | 中途断连/重试 | 有超时和恢复行为 |
| SLE-00 | SLE UART | 两块 WS63 跑官方透传样例 | 握手与双向传输日志 |
| SLE-01 | SLE SSAP | HarmonyOS 手机扫描/连接/写入 | 原屏刷新并返回业务 ACK |
| SLE-02 | SLE SSAP | 连接中断/重连 | 不留下半包脏状态 |
| WIFI-01 | Wi-Fi HTTP | 同网段认证请求修改桌牌 | 原屏刷新并返回业务 ACK |
| WIFI-02 | Wi-Fi HTTP | 弱网/重试/重复请求 | 去重、不重复执行 |
| XPORT-01 | 三种链路 | 各自发送等价命令 | 最终显示及 revision 语义一致 |

## 3. 共存与回退测试（在单链路可用后）

| ID | 场景 | 必须记录 |
|---|---|---|
| COEX-01 | BLE + SLE | 能否共存、建立连接耗时、断线情况 |
| COEX-02 | BLE + Wi-Fi | 是否出现连接失败/显示卡顿 |
| COEX-03 | SLE + Wi-Fi | 是否稳定、是否需调度无线任务 |
| COEX-04 | BLE + SLE + Wi-Fi | 资源占用、是否支持三模同时开启 |
| FALL-01 | 手机不支持 NearLink | App 明确提示并降级 BLE/Wi-Fi |
| FALL-02 | SLE 连接失败 | 自动/手动切换是否避免重复写入 |
| RACE-01 | 不同链路同时修改 | revision 冲突有可解释结果 |

## 4. 必须记录的证据

每项测试附：固件 commit SHA、手机型号及 HarmonyOS 版本、SDK 版本、串口日志、App 日志、桌牌前后照片、测试日期、通过/失败、阻塞原因。

## 5. 技术风险清单

1. **手机星闪支持**：WS63 支持 SLE 不保证所有手机支持；需调用 NearLink Kit 能力查询并进行真机验证。
2. **上游样例角色**：官方 SLE UART 样例为两块小鸿设备，不等价于 HarmonyOS 手机 SSAP 控制桌牌。
3. **无线共存**：不预先承诺 BLE/SLE/Wi-Fi 同时可用；若并发受限，采用互斥/时分调度并在 App 告知。
4. **存储磨损**：分片数据不直接持久化，只提交最终配置。
5. **中文字库**：确认 LVGL 字体覆盖所选中文字符，复杂文字需设计缺字回退。
6. **远程安全**：HTTP 不能开放匿名写入到可被外网访问的接口。

## 6. 官方依据

- 小鸿官方开发资料：https://xiaohong.atomgit.com/docs
- 小鸿 SLE UART 双板样例：https://xiaohong.atomgit.com/docs-en.html
- NearLink Kit：https://developer.huawei.com/consumer/cn/sdk/nearlink-kit/
- NearLink 开发准备：https://developer.huawei.com/consumer/cn/doc/HarmonyOS-Guides/nearlink-preparations-guide/
- 华为星闪手机支持清单：https://consumer.huawei.com/cn/support/content/zh-cn16074557/
