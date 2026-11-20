/**
 * Shared app-level desk data types.
 * Device radio write protocols are not claimed to work until WS63 verified.
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
  /** Open the link. Radio transports need this; HTTP is stateless. */
  connect?(): Promise<void>;
  /** Release the link. Always called by the caller, even after a failure. */
  close?(): Promise<void>;
}
