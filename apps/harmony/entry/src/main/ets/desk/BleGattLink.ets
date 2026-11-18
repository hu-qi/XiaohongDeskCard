import { ble, constant } from '@kit.ConnectivityKit';
import { DeskWireFrame } from './DeskWireFrame';
import { DeskAckWaiter } from './DeskAckWaiter';
import { DeskRadioAck } from './DeskAckFrame';

/**
 * BLE GATT client prototype for the future WS63 server.
 * Discovers both writable Desk characteristic and the ACK notification
 * characteristic. Only a matching 16-byte application-level ACK confirms
 * execution. Radio write promise alone is insufficient.
 *
 * Service UUIDs, negotiated MTU, device authorization and phone SDK build
 * are NOT yet verified against real WS63 hardware.
 */
export class BleGattLink {
  private device: ble.GattClientDevice | null = null;
  private serviceUuid: string;
  private writeUuid: string;
  private ackUuid: string;
  private maxWriteBytes: number;
  private ackWaiter: DeskAckWaiter = new DeskAckWaiter();
  private notificationReady: boolean = false;

  private onCharacteristicChanged = (changed: ble.BLECharacteristic): void => {
    if (changed.serviceUuid.toLowerCase() !== this.serviceUuid.toLowerCase() ||
        changed.characteristicUuid.toLowerCase() !== this.ackUuid.toLowerCase()) {
      return;
    }
    this.ackWaiter.accept(new Uint8Array(changed.characteristicValue));
  };

  constructor(serviceUuid: string, writeUuid: string, ackUuid: string,
              maxWriteBytes: number = 20) {
    this.serviceUuid = serviceUuid;
    this.writeUuid = writeUuid;
    this.ackUuid = ackUuid;
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
      let writeFound: boolean = false;
      let ackChar: ble.BLECharacteristic | null = null;
      for (let i: number = 0; i < services.length; i++) {
        if (services[i].serviceUuid.toLowerCase() !== this.serviceUuid.toLowerCase()) {
          continue;
        }
        for (let j: number = 0; j < services[i].characteristics.length; j++) {
          const characteristic: ble.BLECharacteristic = services[i].characteristics[j];
          if (characteristic.characteristicUuid.toLowerCase() === this.writeUuid.toLowerCase()) {
            writeFound = true;
          }
          if (characteristic.characteristicUuid.toLowerCase() === this.ackUuid.toLowerCase()) {
            ackChar = characteristic;
          }
        }
      }
      if (!writeFound || ackChar === null) {
        throw new Error('Desk BLE command/ACK characteristic not found');
      }
      // Must be present in WS63 GATT table with CCCD for notification support.
      device.on('BLECharacteristicChange', this.onCharacteristicChanged);
      await device.setCharacteristicChangeNotification(ackChar, true);
      this.notificationReady = true;
    } catch (err) {
      this.close();
      throw err;
    }
  }

  async sendFrame(frame: Uint8Array): Promise<void> {
    const device: ble.GattClientDevice | null = this.device;
    if (device === null || !this.notificationReady) {
      throw new Error('BLE command/ACK service not ready');
    }
    const packets: Array<Uint8Array> = DeskWireFrame.packets(frame, this.maxWriteBytes);
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

  async sendFrameAwaitAck(frame: Uint8Array, requestId: number,
                          timeoutMs: number = 15000): Promise<DeskRadioAck> {
    if (!this.notificationReady) throw new Error('BLE ACK notification not subscribed');
    const pending: Promise<DeskRadioAck> = this.ackWaiter.wait(requestId, timeoutMs);
    // Attach a rejection observer immediately: ACK may time out while radio
    // writes are still pending. The original Promise remains rejected.
    pending.catch((_error: Error) => {});
    try {
      await this.sendFrame(frame);
      return await pending;
    } catch (err) {
      this.ackWaiter.fail('BLE transfer failed or ACK missing: ' + String(err));
      throw err;
    }
  }

  close(): void {
    this.ackWaiter.fail('BLE disconnected');
    this.notificationReady = false;
    const device: ble.GattClientDevice | null = this.device;
    this.device = null;
    if (device !== null) {
      try {
        device.off('BLECharacteristicChange', this.onCharacteristicChanged);
        device.disconnect();
        device.close();
      } catch (_error) {
        // Best-effort shutdown on a lost connection.
      }
    }
  }
}
