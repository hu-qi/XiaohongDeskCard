/**
 * Platform-neutral desk-card contracts.
 * Source-only ArkTS module; no DevEco project has been built yet.
 */
export interface DeskContent {
  name: string;
  title: string;
  organization: string;
  theme: string;
}

export interface DeskSetCommand {
  requestId: number;
  expectedRevision: number;
  content: DeskContent;
}

export interface DeskState {
  revision: number;
  content: DeskContent;
  display: DeskContent;
  device: string;
  hardwareValidated: boolean;
}

export interface DeskAck {
  result: string;
  transport: string;
  state: DeskState;
}

export enum DeskChannel {
  WIFI = 'wifi',
  BLE = 'ble',
  SLE = 'sle'
}

export interface DeskTransport {
  readonly channel: DeskChannel;
  getStatus(): Promise<DeskState>;
  setContent(command: DeskSetCommand): Promise<DeskAck>;
}
