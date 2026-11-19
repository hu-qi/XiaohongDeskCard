import { DeskRouter, DeskRouterCandidate, DeskRouterResult } from '../desk/DeskRouter';
import { common } from '@kit.AbilityKit';
import { DeskDeviceDiscovery, DiscoveredDesk } from '../desk/DeskDeviceDiscovery';
import { DeskPermissions } from '../desk/DeskPermissions';
import { DeskAck, DeskChannel, DeskContent, DeskSetCommand, DeskState, DeskTransport } from '../desk/DeskModels';
import { MockDeskTransport } from '../desk/MockDeskTransport';
import { WifiHttpTransport } from '../desk/WifiHttpTransport';
import { isNearLinkAvailable } from '../desk/NearLinkCapability';

/**
 * Stage app entry page.
 * BLE/SLE buttons explicitly execute mock logic, NOT radio communication.
 * Wi-Fi button makes a real HTTP request to a user-specified endpoint.
 * LVGL on WS63 is not part of this mobile application.
 */
@Entry
@Component
struct Index {
  @State private name: string = '张三';
  @State private jobTitle: string = '技术负责人';
  @State private organization: string = 'OpenHarmony 社区';
  @State private theme: string = 'classic';
  @State private deviceName: string = '小鸿 AI';
  @State private deviceTitle: string = '智能电子桌牌';
  @State private deviceOrg: string = 'OpenHarmony';
  @State private revision: number = 0;
  @State private status: string = '待发送（软件模拟器）';
  @State private nearLinkSupported: boolean = false;
  @State private busy: boolean = false;
  @State private discoveryStatus: string = '尚未扫描真实无线设备';
  @State private discoveredDevices: string = '';
  private discovery: DeskDeviceDiscovery | null = null;
  private scanTimer: number = -1;
  @State private endpoint: string = '';
  @State private token: string = '';
  private requestSequence: number = 0;

  aboutToAppear(): void {
    this.nearLinkSupported = isNearLinkAvailable();
    this.discovery = new DeskDeviceDiscovery((item: DiscoveredDesk): void => {
      this.discoveredDevices = item.channel.toUpperCase() + ': ' +
        item.name + ' / ' + item.address + ' / RSSI ' + item.rssi.toString();
    });
  }

  aboutToDisappear(): void {
    if (this.scanTimer !== -1) {
      clearTimeout(this.scanTimer);
      this.scanTimer = -1;
    }
    const discovery: DeskDeviceDiscovery | null = this.discovery;
    this.discovery = null;
    if (discovery !== null) {
      discovery.stopBle();
      discovery.stopSle().catch((_error: Error) => {});
    }
  }

  private async discover(channel: DeskChannel): Promise<void> {
    const discovery: DeskDeviceDiscovery | null = this.discovery;
    if (discovery === null) return;
    try {
      const context: common.UIAbilityContext =
        this.getUIContext().getHostContext() as common.UIAbilityContext;
      if (!await DeskPermissions.request(context, channel)) {
        this.discoveryStatus = '用户未授予 ' + channel.toUpperCase() + ' 权限';
        return;
      }
      await discovery.stopSle();
      discovery.stopBle();
      this.discoveredDevices = '';
      if (channel === DeskChannel.SLE) {
        await discovery.startSle();
      } else {
        discovery.startBle();
      }
      this.discoveryStatus = '正在扫描 ' + channel.toUpperCase() +
        '（仅发现设备，尚不能修改桌牌）';
      if (this.scanTimer !== -1) clearTimeout(this.scanTimer);
      this.scanTimer = setTimeout((): void => {
        if (channel === DeskChannel.BLE) {
          discovery.stopBle();
        } else {
          discovery.stopSle().catch((_error: Error) => {});
        }
        this.discoveryStatus = channel.toUpperCase() + ' 扫描已结束';
        this.scanTimer = -1;
      }, 7000);
    } catch (error) {
      this.discoveryStatus = channel.toUpperCase() + ' 扫描失败：' + String(error);
    }
  }

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

  private updateFromAck(ack: DeskAck): void {
    const state: DeskState = ack.state;
    this.deviceName = state.display.name;
    this.deviceTitle = state.display.title;
    this.deviceOrg = state.display.organization;
    this.revision = state.revision;
    this.status = ack.transport + ': ' + ack.result +
      ' · Revision ' + state.revision.toString();
  }

  private async applyTransport(transport: DeskTransport): Promise<void> {
    if (this.busy) {
      return;
    }
    this.busy = true;
    this.status = '正在发送…';
    try {
      const state: DeskState = await transport.getStatus();
      const command: DeskSetCommand = {
        requestId: this.nextId(),
        expectedRevision: state.revision,
        content: this.content()
      };
      const ack: DeskAck = await transport.setContent(command);
      this.updateFromAck(ack);
    } catch (err) {
      this.status = '发送失败：' + String(err);
    } finally {
      this.busy = false;
    }
  }

  private mock(channel: DeskChannel): void {
    const transport: DeskTransport = new MockDeskTransport(channel);
    this.applyTransport(transport);
  }

  /**
   * Automatically chooses among MOCK adapters. This demonstrates fallback
   * selection only; it does NOT turn on any real BLE/SLE radio.
   */
  private async autoMock(): Promise<void> {
    if (this.busy) return;
    this.busy = true;
    this.status = '模拟三模自动选择中…';
    try {
      const channels: Array<DeskChannel> = [
        DeskChannel.SLE, DeskChannel.BLE, DeskChannel.WIFI
      ];
      const candidates: Array<DeskRouterCandidate> = channels.map(
        (channel: DeskChannel): DeskRouterCandidate => ({
          channel: channel,
          open: async () => {
            if (channel === DeskChannel.SLE && !this.nearLinkSupported) {
              throw new Error('SLE not supported; falling back before write');
            }
            return new MockDeskTransport(channel);
          }
        })
      );
      const router: DeskRouter = new DeskRouter(candidates);
      const outcome: DeskRouterResult = await router.update(
        this.content(), this.nextId());
      this.deviceName = outcome.ack.state.display.name;
      this.deviceTitle = outcome.ack.state.display.title;
      this.deviceOrg = outcome.ack.state.display.organization;
      this.revision = outcome.ack.state.revision;
      this.status = '软件模拟自动选择 ' + outcome.channel.toUpperCase() +
        '；跳过：' + outcome.skipped.join(',') +
        '；ACK：' + outcome.ack.result;
    } catch (err) {
      this.status = '自动选择失败：' + String(err);
    } finally {
      this.busy = false;
    }
  }

  private wifi(): void {
    if (!this.endpoint.startsWith('http://') &&
        !this.endpoint.startsWith('https://')) {
      this.status = '请先输入可访问的 HTTP(S) 设备地址';
      return;
    }
    if (this.token.length === 0) {
      this.status = '请先输入设备令牌；不要使用模拟器默认令牌连接真实设备';
      return;
    }
    this.applyTransport(new WifiHttpTransport(this.endpoint.trim(), this.token));
  }

  build() {
    Scroll() {
      Column({ space: 16 }) {
        Column({ space: 5 }) {
          Text('小鸿智能桌牌').fontSize(25).fontWeight(FontWeight.Bold).fontColor('#152b4c')
          Text('XiaohongDeskCard · 240 × 240 原屏优先')
            .fontSize(12).fontColor('#718095')
        }.alignItems(HorizontalAlign.Start).width('100%')

        Column({ space: 8 }) {
          Text('桌牌编辑预览').fontSize(15).fontWeight(FontWeight.Medium)
          Column() {
            Text(this.organization).fontSize(12).fontColor('#ffffff')
              .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
              .width('100%').textAlign(TextAlign.Center)
              .padding(12).backgroundColor(this.theme === 'modern' ? '#152b4c' : '#b32942')
            Column({ space: 10 }) {
              Text(this.name).fontSize(30).fontWeight(FontWeight.Bold)
                .fontColor('#172b45').textAlign(TextAlign.Center)
                .maxLines(2)
              Text(this.jobTitle).fontSize(13).fontColor('#5d6876')
                .maxLines(2).textAlign(TextAlign.Center)
            }.layoutWeight(1).justifyContent(FlexAlign.Center).width('100%')
            Text('XIAOHONG DESK').fontSize(9).fontColor('#b32942')
              .padding({ bottom: 9 })
          }.width(240).height(240).backgroundColor('#fcfcf9')
            .border({ width: 7, color: '#23314a', radius: 13 })
          Text('上图为手机端模拟排版，不是 WS63 LCD 实拍')
            .fontSize(11).fontColor('#768399')
        }.width('100%').padding(16).backgroundColor('#ffffff').borderRadius(16)

        Column({ space: 10 }) {
          Text('编辑内容').fontSize(16).fontWeight(FontWeight.Medium)
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
        }.width('100%').padding(16).backgroundColor('#ffffff').borderRadius(16)

        Column({ space: 10 }) {
          Text('三模通信验证').fontSize(16).fontWeight(FontWeight.Medium)
          Text('星闪硬件能力：' + (this.nearLinkSupported ? '支持' : '未检测到 / 不可用'))
            .fontSize(12).fontColor('#667588')
          Row({ space: 8 }) {
            Button('BLE Mock').layoutWeight(1).enabled(!this.busy)
              .onClick(() => { this.mock(DeskChannel.BLE); })
            Button('SLE Mock').layoutWeight(1).enabled(!this.busy)
              .onClick(() => { this.mock(DeskChannel.SLE); })
          }.width('100%')
          Button('自动选择链路 · Mock').width('100%').enabled(!this.busy)
            .backgroundColor('#4b6088')
            .onClick(() => { this.autoMock(); })
          Text('BLE/SLE 按钮只模拟协议层，未连接手机无线模块')
            .fontSize(11).fontColor('#9b6726')
          Row({ space: 8 }) {
            Button('扫描 BLE 设备').layoutWeight(1)
              .backgroundColor('#427a8b')
              .onClick(() => { this.discover(DeskChannel.BLE); })
            Button('扫描星闪设备').layoutWeight(1).enabled(this.nearLinkSupported)
              .backgroundColor('#427a8b')
              .onClick(() => { this.discover(DeskChannel.SLE); })
          }.width('100%')
          Text(this.discoveryStatus).fontSize(12).fontColor('#426483')
          Text(this.discoveredDevices).fontSize(11).fontColor('#647386')
          Text('扫描功能调用真实无线 API，需要用户授权；没有 WS63 桌牌广播时不会发现目标')
            .fontSize(11).fontColor('#9b6726')
          TextInput({ text: this.endpoint, placeholder: 'HTTP 地址，如 http://192.168.x.x:8765' })
            .onChange((value: string) => { this.endpoint = value; })
          TextInput({ text: this.token, placeholder: '设备授权令牌' })
            .type(InputType.Password)
            .onChange((value: string) => { this.token = value; })
          Button('Wi-Fi HTTP 真实请求').width('100%').enabled(!this.busy)
            .backgroundColor('#1d5e84').onClick(() => { this.wifi(); })
          Text('注意：127.0.0.1 表示手机自身，无法直接访问 Mac 模拟器')
            .fontSize(11).fontColor('#9b6726')
        }.width('100%').padding(16).backgroundColor('#ffffff').borderRadius(16)

        Column({ space: 7 }) {
          Text('设备回显（模拟状态）').fontSize(15).fontWeight(FontWeight.Medium)
          Text(this.deviceName + ' · ' + this.deviceTitle).fontSize(14)
          Text(this.deviceOrg + ' / Revision ' + this.revision.toString())
            .fontSize(12).fontColor('#667588')
          Text(this.status).fontSize(12).fontColor('#305682')
        }.alignItems(HorizontalAlign.Start).width('100%')
          .padding(16).backgroundColor('#ffffff').borderRadius(16)
        Text('Beta · App 已在 HarmonyOS 7 启动；小鸿固件及三模联调待验证')
          .fontSize(11).fontColor('#7d8696')
      }.width('100%').padding(16).alignItems(HorizontalAlign.Center)
    }.height('100%').width('100%').backgroundColor('#eff2f6')
  }
}
