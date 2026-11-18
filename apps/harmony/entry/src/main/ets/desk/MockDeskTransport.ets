import { DeskAck, DeskChannel, DeskContent, DeskSetCommand, DeskState, DeskTransport } from './DeskModels';

/**
 * Pure ArkTS mock, shared state across all simulated channels in this app.
 * This is intentionally independent of physical BLE and SLE hardware.
 * For C-backed protocol fidelity tests use tools/simulator on Mac/Linux.
 */
export class MockDeskTransport implements DeskTransport {
  readonly channel: DeskChannel;
  private static revision: number = 0;
  private static applied: DeskContent = {
    name: '小鸿 AI', title: '智能电子桌牌',
    organization: 'OpenHarmony', theme: 'classic'
  };
  private static lastRequestId: number = 0;
  private static lastRequestRevision: number = -1;
  private static lastContent: DeskContent | null = null;

  constructor(channel: DeskChannel) {
    this.channel = channel;
  }

  async getStatus(): Promise<DeskState> {
    return MockDeskTransport.snapshot();
  }

  async setContent(command: DeskSetCommand): Promise<DeskAck> {
    let result: string = 'OK';
    if (!this.isValid(command.content) || command.requestId < 1) {
      result = 'BAD_INPUT';
    } else if (command.requestId === MockDeskTransport.lastRequestId) {
      result = command.expectedRevision === MockDeskTransport.lastRequestRevision &&
        this.sameContent(command.content, MockDeskTransport.lastContent) ?
        'OK' : 'DUPLICATE_ID';
    } else if (command.expectedRevision !== MockDeskTransport.revision) {
      result = 'CONFLICT';
    } else {
      MockDeskTransport.applied = {
        name: command.content.name,
        title: command.content.title,
        organization: command.content.organization,
        theme: command.content.theme
      };
      MockDeskTransport.revision += 1;
      MockDeskTransport.lastRequestId = command.requestId;
      MockDeskTransport.lastRequestRevision = command.expectedRevision;
      MockDeskTransport.lastContent = {
        name: command.content.name,
        title: command.content.title,
        organization: command.content.organization,
        theme: command.content.theme
      };
    }
    return {
      result: result,
      transport: this.channel.toUpperCase() + '_ARKTS_MOCK',
      state: MockDeskTransport.snapshot()
    };
  }

  private sameContent(left: DeskContent, right: DeskContent | null): boolean {
    return right !== null &&
      left.name === right.name &&
      left.title === right.title &&
      left.organization === right.organization &&
      left.theme === right.theme;
  }

  private isValid(content: DeskContent): boolean {
    if (content.name.length < 1 || content.name.length > 32) return false;
    if (content.title.length > 48 || content.organization.length > 70) return false;
    if (content.name.indexOf('\n') >= 0 || content.title.indexOf('\n') >= 0) return false;
    return content.theme === 'classic' ||
      content.theme === 'modern' || content.theme === 'minimal';
  }

  private static snapshot(): DeskState {
    const c: DeskContent = MockDeskTransport.applied;
    const value: DeskContent = {
      name: c.name, title: c.title, organization: c.organization, theme: c.theme
    };
    return {
      revision: MockDeskTransport.revision,
      content: value,
      display: {
        name: c.name, title: c.title, organization: c.organization, theme: c.theme
      },
      device: 'XiaohongDesk-ArkTS-MOCK',
      hardwareValidated: false
    };
  }
}
