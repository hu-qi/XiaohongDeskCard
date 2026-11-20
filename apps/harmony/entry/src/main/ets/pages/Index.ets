import { common } from '@kit.AbilityKit';
import { DeskDeviceDiscovery, DiscoveredDesk } from '../desk/DeskDeviceDiscovery';
import { DeskPermissions } from '../desk/DeskPermissions';
import { DeskAck, DeskChannel, DeskContent, DeskSetCommand, DeskState, DeskTransport } from '../desk/DeskModels';
import { DeskHistory, HistoryDevice } from '../desk/DeskHistory';
import { RadioDeskTransport } from '../desk/RadioDeskTransport';
import { WifiHttpTransport } from '../desk/WifiHttpTransport';
import { isNearLinkAvailable } from '../desk/NearLinkCapability';

/**
 * XiaohongDeskCard App 主页面。
 *
 * 交互模式：扫描 → 选择设备 → 连接（保持长连接） → 多次发送 → 断开
 * 连接状态可视化，发送操作在已建立的连接上执行，无需每次重连。
 */

@Entry
@Component
struct Index {
  // ---- 编辑内容 ----
  @State private name: string = '张三';
  @State private jobTitle: string = '技术负责人';
  @State private organization: string = 'OpenHarmony 社区';
  @State private theme: string = 'classic';

  // ---- 连接状态 ----
  @State private connState: string = 'idle'; // idle | scanning | connecting | connected | error
  @State private connChannel: string = '';   // ble | sle | wifi
  @State private connAddress: string = '';
  @State private statusMsg: string = '请扫描并连接设备';

  // ---- 设备回显 ----
  @State private deviceName: string = '';
  @State private deviceTitle: string = '';
  @State private deviceOrg: string = '';
  @State private revision: number = 0;

  // ---- 扫描 ----
  @State private nearLinkSupported: boolean = false;
  @State private discoveryStatus: string = '';
  @State private discovered: Array<DiscoveredDesk> = [];

  // ---- Wi-Fi ----
  @State private endpoint: string = '';
  @State private token: string = '';

  // ---- 操作状态 ----
  @State private busy: boolean = false;
  @State private autoConnecting: boolean = false;
  @State private hasHistory: boolean = false;

  // ---- 内部状态（非 @State） ----
  private discovery: DeskDeviceDiscovery | null = null;
  private scanTimer: number = -1;
  private persistentTransport: DeskTransport | null = null;
  private requestSequence: number = 0;

  aboutToAppear(): void {
    this.nearLinkSupported = isNearLinkAvailable();
    this.discovery = new DeskDeviceDiscovery((item: DiscoveredDesk): void => {
      this.rememberDevice(item);
    });
    this.hasHistory = DeskHistory.get() !== null;
    // 尝试自动连接历史设备
    this.tryAutoConnect();
  }

  aboutToDisappear(): void {
    this.stopScanTimer();
    this.stopDiscovery();
    this.disconnectTransport();
  }

  // ---- 自动连接历史设备 ----

  private historyLabel(): string {
    const hist: HistoryDevice | null = DeskHistory.get();
    if (hist === null) return '';
    if (hist.channel === DeskChannel.WIFI) {
      return 'Wi-Fi · ' + hist.endpoint;
    }
    return hist.channel.toUpperCase() + ' · ' + hist.address;
  }

  private async reconnectHistory(): Promise<void> {
    const hist: HistoryDevice | null = DeskHistory.get();
    if (hist === null) return;
    if (hist.channel === DeskChannel.WIFI) {
      this.endpoint = hist.endpoint;
      this.token = hist.token;
      await this.connectWifi();
    } else {
      await this.connectRadio(hist.channel, hist.address);
    }
  }

  private forgetHistory(): void {
    DeskHistory.clear();
    this.hasHistory = false;
    this.statusMsg = '已清除历史设备记录';
  }

  private async tryAutoConnect(): Promise<void> {
    const hist: HistoryDevice | null = DeskHistory.get();
    if (hist === null) return;
    this.autoConnecting = true;
    this.statusMsg = '正在自动连接上次设备…';
    try {
      if (hist.channel === DeskChannel.WIFI) {
        this.endpoint = hist.endpoint;
        this.token = hist.token;
        await this.connectWifi();
      } else {
        await this.connectRadio(hist.channel, hist.address);
      }
      if (this.connState !== 'connected') {
        this.statusMsg = '自动连接失败，请手动扫描';
      }
    } catch (_err) {
      this.statusMsg = '自动连接失败，请手动扫描';
    } finally {
      this.autoConnecting = false;
    }
  }

  // ---- 扫描 ----

  private stopScanTimer(): void {
    if (this.scanTimer !== -1) {
      clearTimeout(this.scanTimer);
      this.scanTimer = -1;
    }
  }

  private stopDiscovery(): void {
    const discovery: DeskDeviceDiscovery | null = this.discovery;
    if (discovery !== null) {
      discovery.stopBle();
      discovery.stopSle().catch((_error: Error) => {});
    }
  }

  private rememberDevice(item: DiscoveredDesk): void {
    for (let i: number = 0; i < this.discovered.length; i++) {
      if (this.discovered[i].address === item.address &&
          this.discovered[i].channel === item.channel) {
        return;
      }
    }
    const next: Array<DiscoveredDesk> = this.discovered.slice();
    next.push(item);
    this.discovered = next;
  }

  private async scan(channel: DeskChannel): Promise<void> {
    if (this.busy || this.connState === 'connected') return;
    const discovery: DeskDeviceDiscovery | null = this.discovery;
    if (discovery === null) return;
    try {
      const context: common.UIAbilityContext =
        this.getUIContext().getHostContext() as common.UIAbilityContext;
      if (!await DeskPermissions.request(context, channel)) {
        this.discoveryStatus = '未授予 ' + channel.toUpperCase() + ' 权限';
        return;
      }
      this.stopDiscovery();
      this.discovered = [];
      this.connState = 'scanning';
      if (channel === DeskChannel.SLE) {
        await discovery.startSle();
      } else {
        discovery.startBle();
      }
      this.discoveryStatus = '正在扫描 ' + channel.toUpperCase() + '…';
      this.stopScanTimer();
      this.scanTimer = setTimeout((): void => {
        this.stopDiscovery();
        this.discoveryStatus = channel.toUpperCase() + ' 扫描结束，发现 ' +
          this.countOf(channel).toString() + ' 台设备';
        this.connState = 'idle';
        this.scanTimer = -1;
      }, 10000);
    } catch (error) {
      this.discoveryStatus = channel.toUpperCase() + ' 扫描失败：' + String(error);
      this.connState = 'idle';
    }
  }

  private countOf(channel: DeskChannel): number {
    let found: number = 0;
    for (let i: number = 0; i < this.discovered.length; i++) {
      if (this.discovered[i].channel === channel) found++;
    }
    return found;
  }

  // ---- 连接 ----

  private async connectRadio(channel: DeskChannel, address: string): Promise<void> {
    if (this.busy || this.connState === 'connected') return;
    this.busy = true;
    this.connState = 'connecting';
    this.statusMsg = '正在连接 ' + channel.toUpperCase() + ' ' + address + '…';
    this.stopDiscovery();

    const transport: RadioDeskTransport = new RadioDeskTransport(channel, address);
    try {
      const context: common.UIAbilityContext =
        this.getUIContext().getHostContext() as common.UIAbilityContext;
      if (!await DeskPermissions.request(context, channel)) {
        this.statusMsg = '未授予 ' + channel.toUpperCase() + ' 权限';
        this.connState = 'error';
        return;
      }
      await transport.connect();
      this.persistentTransport = transport;
      this.connChannel = channel === DeskChannel.BLE ? 'ble' : 'sle';
      this.connAddress = address;
      this.connState = 'connected';
      this.statusMsg = '已连接 ' + channel.toUpperCase() + ' · ' + address;
      DeskHistory.saveRadio(channel, address);

      // 获取设备状态
      const state: DeskState = await transport.getStatus();
      this.revision = state.revision;
      if (state.display.name.length > 0) {
        this.deviceName = state.display.name;
        this.deviceTitle = state.display.title;
        this.deviceOrg = state.display.organization;
      }
    } catch (err) {
      this.statusMsg = channel.toUpperCase() + ' 连接失败：' + String(err);
      this.connState = 'error';
      try { await transport.close(); } catch (_e) {}
      this.persistentTransport = null;
    } finally {
      this.busy = false;
    }
  }

  private async connectWifi(): Promise<void> {
    if (this.busy || this.connState === 'connected') return;
    if (!this.endpoint.startsWith('http://') && !this.endpoint.startsWith('https://')) {
      this.statusMsg = '请输入有效的 HTTP 地址';
      return;
    }
    if (this.token.length === 0) {
      this.statusMsg = '请输入设备令牌';
      return;
    }
    this.busy = true;
    this.connState = 'connecting';
    this.statusMsg = '正在连接 Wi-Fi…';

    const transport: WifiHttpTransport =
      new WifiHttpTransport(this.endpoint.trim(), this.token);
    try {
      this.persistentTransport = transport;
      this.connChannel = 'wifi';
      this.connAddress = this.endpoint.trim();
      this.connState = 'connected';
      this.statusMsg = '已连接 Wi-Fi · ' + this.endpoint.trim();
      DeskHistory.saveWifi(this.endpoint.trim(), this.token);

      const state: DeskState = await transport.getStatus();
      this.revision = state.revision;
      if (state.display.name.length > 0) {
        this.deviceName = state.display.name;
        this.deviceTitle = state.display.title;
        this.deviceOrg = state.display.organization;
      }
    } catch (err) {
      this.statusMsg = 'Wi-Fi 连接失败：' + String(err);
      this.connState = 'error';
      this.persistentTransport = null;
    } finally {
      this.busy = false;
    }
  }

  private async disconnectTransport(): Promise<void> {
    const transport: DeskTransport | null = this.persistentTransport;
    this.persistentTransport = null;
    if (transport !== null && transport.close !== undefined) {
      try { await transport.close(); } catch (_error) {}
    }
    this.connState = 'idle';
    this.connChannel = '';
    this.connAddress = '';
    this.deviceName = '';
    this.deviceTitle = '';
    this.deviceOrg = '';
    this.revision = 0;
    this.statusMsg = '已断开连接';
  }

  // ---- 发送 ----

  private content(): DeskContent {
    return {
      name: this.name.trim(),
      title: this.jobTitle.trim(),
      organization: this.organization.trim(),
      theme: this.theme
    };
  }

  private nextId(): number {
    this.requestSequence += 1;
    return (Math.floor(Date.now() % 2000000000) + this.requestSequence) % 4000000000 + 1;
  }

  private async send(): Promise<void> {
    const transport: DeskTransport | null = this.persistentTransport;
    if (transport === null || this.connState !== 'connected') {
      this.statusMsg = '请先连接设备';
      return;
    }
    if (this.busy) return;
    this.busy = true;
    this.statusMsg = '正在发送…';
    try {
      const state: DeskState = await transport.getStatus();
      const command: DeskSetCommand = {
        requestId: this.nextId(),
        expectedRevision: state.revision,
        content: this.content()
      };
      const ack: DeskAck = await transport.setContent(command);
      this.revision = ack.state.revision;
      if (ack.state.display.name.length > 0) {
        this.deviceName = ack.state.display.name;
        this.deviceTitle = ack.state.display.title;
        this.deviceOrg = ack.state.display.organization;
      }
      this.statusMsg = ack.transport + ': ' + ack.result +
        ' · Revision ' + ack.state.revision.toString();
    } catch (err) {
      this.statusMsg = '发送失败：' + String(err);
      // 连接可能已断开
      if (this.connState === 'connected') {
        this.connState = 'error';
      }
    } finally {
      this.busy = false;
    }
  }

  // ---- UI 构建 ----

  @Builder
  connBadge() {
    Row({ space: 6 }) {
      if (this.connState === 'connected') {
        Text('●').fontSize(10).fontColor('#22c55e')
        Text(this.connChannel.toUpperCase() + ' 已连接').fontSize(12).fontColor('#22c55e')
      } else if (this.connState === 'connecting') {
        Text('●').fontSize(10).fontColor('#f59e0b')
        Text(this.autoConnecting ? '自动连接中…' : '正在连接…').fontSize(12).fontColor('#f59e0b')
      } else if (this.connState === 'scanning') {
        Text('●').fontSize(10).fontColor('#3b82f6')
        Text('正在扫描…').fontSize(12).fontColor('#3b82f6')
      } else if (this.connState === 'error') {
        Text('●').fontSize(10).fontColor('#ef4444')
        Text('连接异常').fontSize(12).fontColor('#ef4444')
      } else {
        Text('●').fontSize(10).fontColor('#9ca3af')
        Text('未连接').fontSize(12).fontColor('#9ca3af')
      }
    }
  }


  @Builder
  deviceRow(item: DiscoveredDesk) {
    Row({ space: 8 }) {
      Column({ space: 2 }) {
        Text(item.name + ' · ' + item.channel.toUpperCase())
          .fontSize(13).fontColor('#1f3a5c')
        Text(item.address + ' · RSSI ' + item.rssi.toString())
          .fontSize(11).fontColor('#6c7b8f')
      }.alignItems(HorizontalAlign.Start).layoutWeight(1)
      Button('连接').fontSize(12).enabled(!this.busy && this.connState !== 'connected')
        .backgroundColor('#2f6f8f')
        .onClick(() => { this.connectRadio(item.channel, item.address); })
    }.width('100%').padding({ top: 6, bottom: 6 })
  }

  @Builder
  historyRow() {
    Row({ space: 8 }) {
      Column({ space: 2 }) {
        Text('上次连接的设备')
          .fontSize(12).fontColor('#6c7b8f')
        Text(this.historyLabel())
          .fontSize(13).fontColor('#1f3a5c')
      }.alignItems(HorizontalAlign.Start).layoutWeight(1)
      Button('重连').fontSize(12).enabled(!this.busy)
        .backgroundColor('#427a8b')
        .onClick(() => { this.reconnectHistory(); })
      Button('忘记').fontSize(12).enabled(!this.busy)
        .backgroundColor('#97a0ad')
        .onClick(() => { this.forgetHistory(); })
    }.width('100%').padding({ top: 6, bottom: 6 })
  }

  build() {
    Scroll() {
      Column({ space: 12 }) {
        // 标题 + 连接状态
        Row() {
          Column() {
            Text('小鸿智能桌牌').fontSize(22).fontWeight(FontWeight.Bold).fontColor('#152b4c')
            Text('XiaohongDeskCard').fontSize(11).fontColor('#718095')
          }.alignItems(HorizontalAlign.Start).layoutWeight(1)
          this.connBadge()
        }.width('100%')

        // 桌牌预览
        Column() {
          Text(this.organization).fontSize(12).fontColor('#ffffff')
            .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
            .width('100%').textAlign(TextAlign.Center)
            .padding(12).backgroundColor(this.theme === 'modern' ? '#152b4c' : '#b32942')
          Column({ space: 10 }) {
            Text(this.name).fontSize(30).fontWeight(FontWeight.Bold)
              .fontColor('#172b45').textAlign(TextAlign.Center).maxLines(2)
            Text(this.jobTitle).fontSize(13).fontColor('#5d6876')
              .maxLines(2).textAlign(TextAlign.Center)
          }.layoutWeight(1).justifyContent(FlexAlign.Center).width('100%')
          Text('XIAOHONG DESK').fontSize(9).fontColor('#b32942')
            .padding({ bottom: 9 })
        }.width(240).height(240).backgroundColor('#fcfcf9')
          .border({ width: 7, color: '#23314a', radius: 13 })

        // 编辑内容
        Column({ space: 8 }) {
          Text('编辑内容').fontSize(15).fontWeight(FontWeight.Medium).fontColor('#152b4c')
          TextInput({ text: this.name, placeholder: '姓名' })
            .onChange((value: string) => { this.name = value; })
          TextInput({ text: this.jobTitle, placeholder: '职位' })
            .onChange((value: string) => { this.jobTitle = value; })
          TextInput({ text: this.organization, placeholder: '单位 / 组织' })
            .onChange((value: string) => { this.organization = value; })
          Row({ space: 8 }) {
            Button('经典红').fontSize(12).layoutWeight(1)
              .backgroundColor(this.theme === 'classic' ? '#b32942' : '#97a0ad')
              .onClick(() => { this.theme = 'classic'; })
            Button('现代蓝').fontSize(12).layoutWeight(1)
              .backgroundColor(this.theme === 'modern' ? '#173a60' : '#97a0ad')
              .onClick(() => { this.theme = 'modern'; })
            Button('极简白').fontSize(12).layoutWeight(1)
              .backgroundColor(this.theme === 'minimal' ? '#5f6b7b' : '#97a0ad')
              .onClick(() => { this.theme = 'minimal'; })
          }.width('100%')
        }.width('100%').padding(14).backgroundColor('#ffffff').borderRadius(12)

        // 连接管理
        Column({ space: 8 }) {
          Text('设备连接').fontSize(15).fontWeight(FontWeight.Medium).fontColor('#152b4c')

          // 已连接时显示断开按钮
          if (this.connState === 'connected') {
            Row({ space: 8 }) {
              Text(this.connChannel.toUpperCase() + ' · ' + this.connAddress)
                .fontSize(13).fontColor('#22c55e').layoutWeight(1)
              Button('断开').fontSize(12).backgroundColor('#ef4444')
                .onClick(() => { this.disconnectTransport(); })
            }.width('100%')
          } else {
            // 历史设备提示（未连接且非自动连接中且有历史时显示）
            if (!this.autoConnecting && this.hasHistory) {
              this.historyRow()
            }

            // 扫描按钮
            Row({ space: 8 }) {
              Button('扫描 BLE').layoutWeight(1).enabled(!this.busy)
                .backgroundColor('#427a8b')
                .onClick(() => { this.scan(DeskChannel.BLE); })
              Button('扫描星闪').layoutWeight(1)
                .enabled(this.nearLinkSupported && !this.busy)
                .backgroundColor('#427a8b')
                .onClick(() => { this.scan(DeskChannel.SLE); })
            }.width('100%')

            Text(this.discoveryStatus).fontSize(12).fontColor('#426483')

            // 设备列表
            if (this.discovered.length > 0) {
              ForEach(this.discovered, (item: DiscoveredDesk) => {
                this.deviceRow(item)
              }, (item: DiscoveredDesk): string => item.channel + ':' + item.address)
            }

            // Wi-Fi 连接
            Divider().margin({ top: 4, bottom: 4 })
            TextInput({ text: this.endpoint, placeholder: 'HTTP 地址，如 http://192.168.x.x' })
              .onChange((value: string) => { this.endpoint = value; })
            TextInput({ text: this.token, placeholder: '设备授权令牌' })
              .type(InputType.Password)
              .onChange((value: string) => { this.token = value; })
            Button('Wi-Fi 连接').width('100%').enabled(!this.busy)
              .backgroundColor('#1d5e84')
              .onClick(() => { this.connectWifi(); })
          }
        }.width('100%').padding(14).backgroundColor('#ffffff').borderRadius(12)

        // 发送按钮（仅已连接时可用）
        if (this.connState === 'connected') {
          Button('发送到设备').width('100%').enabled(!this.busy)
            .backgroundColor('#2f6f8f').fontSize(16)
            .onClick(() => { this.send(); })
        }

        // 设备回显
        if (this.deviceName.length > 0) {
          Column({ space: 4 }) {
            Text('设备回显').fontSize(14).fontWeight(FontWeight.Medium).fontColor('#152b4c')
            Text(this.deviceName + (this.deviceTitle.length > 0 ? ' · ' + this.deviceTitle : ''))
              .fontSize(14).fontColor('#172b45')
            Text(this.deviceOrg + ' / Revision ' + this.revision.toString())
              .fontSize(12).fontColor('#667588')
          }.alignItems(HorizontalAlign.Start).width('100%')
            .padding(14).backgroundColor('#ffffff').borderRadius(12)
        }

        // 状态消息
        Text(this.statusMsg).fontSize(12).fontColor('#305682').width('100%')
          .textAlign(TextAlign.Center)

        Text('Beta · 三模通信直连小鸿固件')
          .fontSize(10).fontColor('#7d8696')
      }.width('100%').padding(14).alignItems(HorizontalAlign.Center)
    }.height('100%').width('100%').backgroundColor('#eff2f6')
  }
}
