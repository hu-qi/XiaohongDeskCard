import { ble } from '@kit.ConnectivityKit';
import { scan } from '@kit.NearLinkKit';
import { DeskChannel } from './DeskModels';
import { DeskServiceIds } from './DeskServiceIds';
import { isNearLinkAvailable } from './NearLinkCapability';

/**
 * Device discovery adapters for HarmonyOS SDK 22 (compile not yet verified).
 * Permissions must be granted by the host UI before starting scans.
 * Scan results do NOT prove device pairing, ownership or Desk service support.
 */
export interface DiscoveredDesk {
  address: string;
  name: string;
  rssi: number;
  channel: DeskChannel;
}

export class DeskDeviceDiscovery {
  private bleScanning: boolean = false;
  private sleScanning: boolean = false;
  private onFound: (device: DiscoveredDesk) => void;

  private bleFound = (items: Array<ble.ScanResult>): void => {
    for (let i: number = 0; i < items.length; i++) {
      const device: ble.ScanResult = items[i];
      if (device.deviceName !== DeskServiceIds.ADVERTISED_NAME) continue;
      this.onFound({
        address: device.deviceId,
        name: device.deviceName,
        rssi: device.rssi,
        channel: DeskChannel.BLE
      });
    }
  };

  private sleFound = (items: Array<scan.ScanResults>): void => {
    for (let i: number = 0; i < items.length; i++) {
      const device: scan.ScanResults = items[i];
      if (device.deviceName !== DeskServiceIds.ADVERTISED_NAME) continue;
      if (!device.isConnectable) continue;
      this.onFound({
        address: device.address,
        name: device.deviceName,
        rssi: device.rssi,
        channel: DeskChannel.SLE
      });
    }
  };

  constructor(onFound: (device: DiscoveredDesk) => void) {
    this.onFound = onFound;
  }

  startBle(): void {
    if (this.bleScanning) return;
    ble.on('BLEDeviceFind', this.bleFound);
    try {
      const filter: ble.ScanFilter = { name: DeskServiceIds.ADVERTISED_NAME };
      ble.startBLEScan([filter]);
      this.bleScanning = true;
    } catch (err) {
      ble.off('BLEDeviceFind', this.bleFound);
      throw err;
    }
  }

  stopBle(): void {
    if (!this.bleScanning) return;
    this.bleScanning = false;
    try {
      ble.stopBLEScan();
    } finally {
      ble.off('BLEDeviceFind', this.bleFound);
    }
  }

  async startSle(): Promise<void> {
    if (this.sleScanning) return;
    if (!isNearLinkAvailable()) throw new Error('NearLink not supported on this device');
    scan.on('deviceFound', this.sleFound);
    try {
      const filter: scan.ScanFilters = {
        deviceName: DeskServiceIds.ADVERTISED_NAME
      };
      await scan.startScan([filter]);
      this.sleScanning = true;
    } catch (err) {
      scan.off('deviceFound', this.sleFound);
      throw err;
    }
  }

  async stopSle(): Promise<void> {
    if (!this.sleScanning) return;
    this.sleScanning = false;
    try {
      await scan.stopScan();
    } finally {
      scan.off('deviceFound', this.sleFound);
    }
  }
}
