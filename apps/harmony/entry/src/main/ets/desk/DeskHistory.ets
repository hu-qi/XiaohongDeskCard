import { DeskChannel } from './DeskModels';

/**
 * Persistent device history for auto-reconnect.
 * Uses PersistentStorage so the last-connected device survives app restarts.
 */
export interface HistoryDevice {
  channel: DeskChannel;
  address: string;
  /** Wi-Fi only — blank for BLE/SLE. */
  endpoint: string;
  /** Wi-Fi only — blank for BLE/SLE. */
  token: string;
}

const KEY_CHANNEL: string = 'desk_hist_channel';
const KEY_ADDRESS: string = 'desk_hist_address';
const KEY_ENDPOINT: string = 'desk_hist_endpoint';
const KEY_TOKEN: string = 'desk_hist_token';

export class DeskHistory {
  /**
   * Save the last-connected radio device (BLE or SLE).
   * Called after a successful connectRadio().
   */
  static saveRadio(channel: DeskChannel, address: string): void {
    PersistentStorage.persistProp(KEY_CHANNEL, channel);
    PersistentStorage.persistProp(KEY_ADDRESS, address);
    PersistentStorage.persistProp(KEY_ENDPOINT, '');
    PersistentStorage.persistProp(KEY_TOKEN, '');
  }

  /**
   * Save the last-connected Wi-Fi device.
   * Called after a successful connectWifi().
   */
  static saveWifi(endpoint: string, token: string): void {
    PersistentStorage.persistProp(KEY_CHANNEL, DeskChannel.WIFI);
    PersistentStorage.persistProp(KEY_ADDRESS, '');
    PersistentStorage.persistProp(KEY_ENDPOINT, endpoint);
    PersistentStorage.persistProp(KEY_TOKEN, token);
  }

  /**
   * Read the last-connected device, or null if no history exists.
   */
  static get(): HistoryDevice | null {
    const channel: string = AppStorage.get<string>(KEY_CHANNEL) ?? '';
    if (channel.length === 0) return null;
    const address: string = AppStorage.get<string>(KEY_ADDRESS) ?? '';
    const endpoint: string = AppStorage.get<string>(KEY_ENDPOINT) ?? '';
    const token: string = AppStorage.get<string>(KEY_TOKEN) ?? '';

    if (channel === DeskChannel.WIFI) {
      if (endpoint.length === 0) return null;
      return { channel: DeskChannel.WIFI, address: '', endpoint, token };
    }
    // BLE or SLE
    if (address.length === 0) return null;
    return { channel: channel as DeskChannel, address, endpoint: '', token: '' };
  }

  /**
   * Clear all saved device history.
   * Called when the user explicitly forgets a device.
   */
  static clear(): void {
    PersistentStorage.deleteProp(KEY_CHANNEL);
    PersistentStorage.deleteProp(KEY_ADDRESS);
    PersistentStorage.deleteProp(KEY_ENDPOINT);
    PersistentStorage.deleteProp(KEY_TOKEN);
  }
}