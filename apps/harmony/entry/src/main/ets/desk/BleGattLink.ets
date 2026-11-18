import { ble, constant } from '@kit.ConnectivityKit';
import { DeskWireFrame } from './DeskWireFrame';

/**
 * BLE GATT client write adapter (SDK integration prototype).
 *
 * This layer performs actual GATT connection / service discovery / writes
 * WHEN an address and service/characteristic UUIDs are provided and the
 * mobile app has obtained user permission. The WS63 service is not built yet.
 * A resolved send() means GATT writes succeeded, NOT Desk Core APPLIED ACK.
 */
export class BleGattLink {
  private device: ble.GattClientDevice | null = null;
  private serviceUuid: string;
  private writeUuid: string;
  private maxWriteBytes: number;

  constructor(serviceUuid: string, writeUuid: string, maxWriteBytes: number = 20) {
    this.serviceUuid = serviceUuid;
    this.writeUuid = writeUuid;
    this.maxWriteBytes = maxWriteBytes;
  }

  async connect(address: string): Promise<void> {
    if (this.device !== null) {
      throw new Error('BLE client already opened');
    }
    const device: ble.GattClientDevice = ble.createGattClientDevice(address);
    this.device = device;
    try {
      await new Promise<void>((resolve, reject) => {
        let complete: boolean = false;
        let timeoutId: number = 0;
        const handler = (state: ble.BLEConnectionChangeState): void => {
          if (state.state === constant.ProfileConnectionState.STATE_CONNECTED && !complete) {
            complete = true;
            clearTimeout(timeoutId);
            device.off('BLEConnectionStateChange', handler);
            resolve();
          }
        };
        timeoutId = setTimeout((): void => {
          if (!complete) {
            complete = true;
            device.off('BLEConnectionStateChange', handler);
            reject(new Error('BLE GATT connection timed out'));
          }
        }, 15000);
        device.on('BLEConnectionStateChange', handler);
        try {
          device.connect();
        } catch (err) {
          if (!complete) {
            complete = true;
            clearTimeout(timeoutId);
            device.off('BLEConnectionStateChange', handler);
            reject(err);
          }
        }
      });
      const services: Array<ble.GattService> = await device.getServices();
      let found: boolean = false;
      for (let i: number = 0; i < services.length; i++) {
        if (services[i].serviceUuid.toLowerCase() !== this.serviceUuid.toLowerCase()) {
          continue;
        }
        for (let j: number = 0; j < services[i].characteristics.length; j++) {
          if (services[i].characteristics[j].characteristicUuid.toLowerCase() ===
              this.writeUuid.toLowerCase()) {
            found = true;
          }
        }
      }
      if (!found) throw new Error('Desk BLE write characteristic not found');
    } catch (err) {
      this.close();
      throw err;
    }
  }

  async sendFrame(frame: Uint8Array): Promise<void> {
    const device: ble.GattClientDevice | null = this.device;
    if (device === null) throw new Error('BLE not connected');
    const packets: Array<Uint8Array> = DeskWireFrame.packets(frame, this.maxWriteBytes);
    // Serialized writes; the SDK forbids overlapping read/write operations.
    for (let i: number = 0; i < packets.length; i++) {
      const characteristic: ble.BLECharacteristic = {
        serviceUuid: this.serviceUuid,
        characteristicUuid: this.writeUuid,
        characteristicValue: packets[i].buffer,
        descriptors: []
      };
      await device.writeCharacteristicValue(characteristic, ble.GattWriteType.WRITE);
    }
  }

  close(): void {
    const device: ble.GattClientDevice | null = this.device;
    this.device = null;
    if (device !== null) {
      try {
        device.disconnect();
        device.close();
      } catch (_error) {
        // Disconnect is best-effort; SDK manages the underlying radio.
      }
    }
  }
}
