# Gate B · 华为云 ECS 真实构建记录

状态:**进行中** — ECS 已就绪,源码同步中(2026-11-20)。

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

## 构建步骤进度

- [x] ECS 创建(t7.xlarge.4 + Ubuntu 22.04 + 密钥对)
- [x] DNS 配置
- [x] apt 依赖 + repo + ohos-build + git lfs
- [ ] repo sync 源码(~11 GB)
- [ ] `git lfs pull` 全仓 LFS
- [ ] prebuilts_download.sh(~11 GB)
- [ ] clang 软链 llvm
- [ ] RISC-V 工具链 PATH 校验
- [ ] `./build.sh --product-name xiaohong --gn-args is_debug=false`
- [ ] 产物 `ws63-liteos-app_all.fwpkg` / `_load_only.fwpkg` scp 回本地
- [ ] XiaohongDeskCard deskcard overlay 集成构建(基线固件成功后)

## 后续(XiaohongDeskCard Gate B 对接)

基线官方固件编译成功后,在同一 ECS:
1. `python3 tools/ws63_sdk_probe.py --root ~/xiaohong --output ws63-sdk-probe.json`(本地或远端执行均可,输出回传审查)
2. `python3 tools/ws63_preflight.py --root ~/xiaohong --json`
3. 人工评审后 `tools/ws63_gn_integrate.py` 只读预览 → `--apply`
4. 集成后再次 `./build.sh` 产出含 deskcard 的固件

## 清理提示

构建完成后如不再需要:`terraform destroy`(ECS/EIP);VPC/子网/安全组是复用资源,**不在** terraform 管理范围,不会被误删。
