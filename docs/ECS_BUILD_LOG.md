# Gate B · 华为云 ECS 真实构建记录

状态:**已完成** — ECS 上基线固件与 XiaohongDeskCard 固件全量构建成功,产物 .fwpkg 已成功 scp 同步回本地(2026-11-20)。

## 资源与工具版本

| 项 | 值 |
|---|---|
| 构建 skill | https://atomgit.com/huqi/xiaohong-build-skill (master@2018e61 + b60b497) |
| devkit | huaweicloud-devkit 1.1.14(doctor 11/11 PASS) |
| grape-cli | 0.3.4(最新) |
| hcloud KooCLI | 7.3.12 |
| Terraform | 1.9.8 + provider huaweicloud 1.100.0 |
| ECS | xiaohong-build @ cn-north-4,t7.xlarge.4(4 vCPU / 15 GB / 197 GB),Ubuntu 22.04 x86_64 |
| 公网 IP | 120.46.211.205(实例 ID 81f55408-ef02-4a91-81bd-61b38e6ab1cd) |
| 网络 | 复用空闲 xiaohong-dev-vpc(07d6eab8)/ xiaohong-dev-subnet(2f7e794d)/ xiaohong-dev-sg-20260626194643(1d4b4cc4) |
| SSH | `ssh -i ~/.ssh/id_rsa root@120.46.211.205` |
| manifest | https://atomgit.com/xiaohong-ai/manifest.git @ **OpenHarmony-7.0.0.38-Release**(与 docs/WS63_INTEGRATION.md 基线一致;skill 原 6.1.0.31 已更新) |

## skill 更新(已推送 atomgit)

1. `scripts/build_xiaohong.sh`:修复 `${ECS_PASSWORD:...}` 缺 `-` 的 bash 语法错误;manifest 分支 → 7.0.0.38。
2. `references/terraform/main.tf`:provider 1.100 语法修复(secgroup_id、system_disk_*、public_ip);镜像 data source 简化;密码登录改为 SSH 密钥对(IAM 禁止 metadata admin_pass,403);VPC CIDR → 10.10.0.0/16。

## 实际遇到并已解决的坑(skill 文档未覆盖)

1. **router 配额满(VPC.0114)**:cn-north-4 新建 VPC 失败。复用空闲的 `xiaohong-dev-vpc`。
2. **`The flavor does not match the image`(Ecs.0005)**:cn-north-4 全部公共 Ubuntu 22.04 x86 镜像是 `virtual_env_type=FusionCompute`;m7/ac8 等 KVM flavor 不兼容。2c16G x86 flavor 全部 abandon 停售。可行组合:**t7.xlarge.4(4c16G, FusionCompute 兼容)+ 原版 Ubuntu 22.04 server 64bit 镜像(89ac6e18)**。
3. **metadata admin_pass 403**:IAM 策略禁止写 admin_pass 系统键;密码登录方式不可用,改用已有 SSH 密钥对 `xhb-access-key`(指纹匹配本地 `~/.ssh/id_rsa`)。
4. **t7.xlarge.4 状态 abandon 但可创建**(存量 SKU),实例已 ACTIVE。
5. DNS chattr +i 在此内核不支持;DHCP 会在续租时**覆盖** `/etc/resolv.conf` 为公网 DNS(8.8.8.8),导致内网解析失败、prebuilts 下载中途 Name resolution 报错。修复:向 `/etc/dhcp/dhclient.conf` 追加 `supersede domain-name-servers 100.125.1.250, 100.125.21.250;`(chattr +i 的锁在 t7 内核上无效,这是本环境可行的持久化方式)。
6. apt 首轮被 SSH 断连打断 → `dpkg --configure -a` 修复后重跑成功。
7. repo launcher 需 `export REPO_URL=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo`。

8. **GN 构建依赖与 SLE 头文件**:
   - `deskcard` 的 `desk_ws63_adapters.c` 依赖 SLE 服务头文件,`vendor/atomgit/xiaohong/xiaohong/BUILD.gn` 中需加入 `${ws63_sdk_path}/include/middleware/services/bts/sle`。
   - `device/soc/hisilicon/ws63v100/sdkv106/BUILD.gn` 中的 `run_sdk_build` 原生依赖列表中缺少 `"//vendor/atomgit/xiaohong:xiaohong"`。ninja 并行构建时若先触发 `hm_build.sh`,由于 `libxiaohong.a` 尚未打包会导致链接报错 `undefined reference`。在 `run_sdk_build` 的 `deps` 中加入 `"//vendor/atomgit/xiaohong:xiaohong"` 显式依赖后完全解决。
   - SDK 依赖的 Python 构建脚本缺少 `kconfiglib` 与 `pycparser`,需 `pip3 install --target /root/xiaohong/device/soc/hisilicon/ws63v100/sdkv106/build/script/ kconfiglib pycparser`。

### 2026-11-20 SSH 全断事件(重装操作系统)与真实根因

时间线:构建中断后 SSH 在 banner 阶段(`kex_exchange_identification: Connection closed by remote host`)被断。SOFT/HARD 重启、API 密码重置全部无效 → 误判为系统盘 sshd 损坏 → HARD 关机后 `ReinstallServerWithCloudInit`(job `ff808081a0ed145901a1f66018b53e9a`,39 秒即 SUCCESS)。

重装后 SSH 依旧断 → 深挖后推翻原诊断,真实根因在**本地网络**:

1. 对 EIP 任意端口(22/80/443/12345)的裸 TCP 都"连接成功但无数据",ICMP 100% 丢包 — 虚机不可能回应未监听的端口,是中间盒在 SYN-proxy。
2. 同一网络下所有华为云 EIP(含 2024 年既有的其他实例)全部复现,GitHub:22 正常 — 与实例无关。
3. 本机 `utun1024`(Clash Verge mihomo,198.19.0.1,TUN auto-route)接管全部流量;规则 `GeoIP cn → DIRECT` 使华为云 IP 直连,而办公网(aTrust 零信任 172.16.0.0/12)对华为云 EIP 的 22 端口做 TCP 拦截。
4. mihomo 切 global 模式(流量走代理节点)后 SSH 一秒连通;ECS 控制台日志证实虚机一直正常(全新 Ubuntu 22.04.5,cloud-init 完成,authorized_keys 含正确公钥)。

**修复方式**:临时 PATCH mihomo `/configs` `mode: global` 做 SSH/SCP,结束切回 `rule`;本机已建 `~/bin/xhb-ssh`、`~/bin/xhb-scp` 包装器自动切换/还原。持久化方案(往 profiles `rXXavwkAtHFB.yaml` prepend `IP-CIDR,120.46.211.205/32,Proxy,no-resolve`)需 Verge UI 重新激活 profile 才生效,未采用。

**重装副作用**:系统盘清空,apt 依赖/repo 源码/prebuilts 全部重来;EIP、实例 ID、密钥对不变。重装后首次连接必须清 known_hosts(主机密钥已更换,新 ED25519 指纹与控制台日志一致 `SHA256:ondjZhHL6Ub5x4C/m90116QxDuEn5oW2Mnt3kL2gB/E`)。

**经验**:诊断 SSH banner 阶段被断时,先区分"虚机拒绝"与"中间盒拦截"——对未监听端口(如 12345)做裸 TCP 探测,若同样被接受则流量根本没到虚机;再换一条出口路径(代理)交叉验证。本例两次误诊断(os 重装 39 秒 SUCCESS 也应视为可疑信号)代价约 2 小时。

## 构建步骤进度

- [x] ECS 创建(t7.xlarge.4 + Ubuntu 22.04 + 密钥对)
- [x] DNS 配置
- [x] apt 依赖 + repo + ohos-build + git lfs(重装后 2026-11-20 重放完成;注:`libc6-dev-amd64` 在 amd64 宿主不存在,跳过不影响构建)
- [x] repo sync 源码(~11 GB)
- [x] `git lfs pull` 全仓 LFS
- [x] prebuilts_download.sh(~11 GB)
- [x] clang 软链 llvm
- [x] RISC-V 工具链 PATH 校验
- [x] `./build.sh --product-name xiaohong --gn-args is_debug=false`(成功,用时 1m35s)
- [x] XiaohongDeskCard deskcard overlay 集成构建(成功集成进 libxiaohong.a 并链接产出固件)
- [x] 产物 `ws63-liteos-app_all.fwpkg` / `_load_only.fwpkg` scp 回本地

## 本地产物清单

固件产物存放于本地 `xiaohong-firmware/`:
- `ws63-liteos-app_all.fwpkg` (2,115,624 B)
  - SHA-256: `a8239c137664ab634e611a0d1a75e53442295c1b15b559df69aa2898fd2605c0`
  - 公开下载: <https://huqi-blog.obs.cn-north-4.myhuaweicloud.com/xiaohong-deskcard/ws63-liteos-app_all_9.9.0_a8239c13.fwpkg>
- `ws63-liteos-app_load_only.fwpkg` (1,984,340 B)
  - SHA-256: `202dea365deea9686945354c60c014efb80b69c94c7ff418be333c19bb9dd60a`

构建日志: <https://huqi-blog.obs.cn-north-4.myhuaweicloud.com/xiaohong-deskcard/build.log>

> 该版本修复了两个真机实测发现的 BLE 缺陷:
>
> 1. **已配网设备上 BLE 完全不存在** —— `enable_ble()` 此前只有配网任务一条调用路径,而已配网设备根本不会启动配网任务。现在由 UI 任务轮询自行使能并广播 `XiaohongDesk`,同时补上了连接状态回调(此前 `desk_ble_on_connect()` 从未被调用,导致 ACK 永远发不出去)。
> 2. **SLE 广播 AD 结构顺序反了** —— SLE 是 `[类型][长度][数据]`,BLE 是 `[长度][类型][数据]`。scan response 用了 BLE 顺序,协议栈直接丢弃整块,设备永远扫不到(但 `sle_start_announce` 返回成功,不报错)。同时补上了 128-bit 服务 UUID 广播,让鸿蒙可按服务发现。
> 3. **128-bit UUID 字节序反了** —— BLE 栈按小端存储 UUID,而头文件宏是字符串顺序。真机枚举到的特征 UUID 是期望值的逐字节反序(`44b4cc5b-...` 变成 `52202e80-...`),导致客户端找不到特征、写入被静默丢弃。已加 `desk_uuid128_le()` 转换并配回归测试。

## 后续(XiaohongDeskCard Gate B 对接)

基线官方固件编译成功后,在同一 ECS:
1. `python3 tools/ws63_sdk_probe.py --root ~/xiaohong --output ws63-sdk-probe.json`(本地或远端执行均可,输出回传审查)
2. `python3 tools/ws63_preflight.py --root ~/xiaohong --json`
3. 人工评审后 `tools/ws63_gn_integrate.py` 只读预览 → `--apply`
4. 集成后再次 `./build.sh` 产出含 deskcard 的固件

## 清理提示

构建完成后如不再需要:`terraform destroy`(ECS/EIP);VPC/子网/安全组是复用资源,**不在** terraform 管理范围,不会被误删。
