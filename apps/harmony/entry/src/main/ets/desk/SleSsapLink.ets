import { ssap } from '@kit.NearLinkKit';
import { DeskWireFrame } from './DeskWireFrame';
import { DeskAckWaiter } from './DeskAckWaiter';
import { DeskRadioAck } from './DeskAckFrame';
import { isNearLinkAvailable } from './NearLinkCapability';

/**
 * SLE SSAP client prototype, for HarmonyOS @kit.NearLinkKit (API 13+).
 *
 * The WS63 server must advertise a writable Desk property and an ACK notify
 * property. ACK is required to confirm Desk Core processing, not merely
 * SSAP transport success. Service/Property UUID and hardware integration
 * remain unvalidated.
 */
export class SleSsapLink {
  private client: ssap.Client | null = null;
  private serviceUuid: string;
  private writeUuid: string;
  private ackUuid: string;
  private maxWriteBytes: number;
  private ackWaiter: DeskAckWaiter = new DeskAckWaiter();
  private notificationReady: boolean = false;

  private onPropertyChanged = (changed: ssap.Property): void => {
    if (changed.serviceUuid.toLowerCase() !== this.serviceUuid.toLowerCase() ||
        changed.propertyUuid.toLowerCase() !== this.ackUuid.toLowerCase()) {
      return;
    }
    this.ackWaiter.accept(new Uint8Array(changed.value));
  };

  constructor(serviceUuid: string, writeUuid: string, ackUuid: string,
              maxWriteBytes: number = 64) {
    this.serviceUuid = serviceUuid;
    this.writeUuid = writeUuid;
    this.ackUuid = ackUuid;
    this.maxWriteBytes = maxWriteBytes;
  }

  async connect(address: string): Promise<void> {
    if (!isNearLinkAvailable()) {
      throw new Error('This HarmonyOS device does not support NearLink');
    }
    if (this.client !== null) throw new Error('SLE client already opened');
    const client: ssap.Client = ssap.createClient(address);
    this.client = client;
    try {
      await client.connect();
      const services: Array<ssap.Service> = await client.getServices();
      let writeFound: boolean = false;
      let ackProperty: ssap.Property | null = null;
      for (let i: number = 0; i < services.length; i++) {
        if (services[i].serviceUuid.toLowerCase() !== this.serviceUuid.toLowerCase()) {
          continue;
        }
        for (let j: number = 0; j < services[i].properties.length; j++) {
          const prop: ssap.Property = services[i].properties[j];
          if (prop.propertyUuid.toLowerCase() === this.writeUuid.toLowerCase()) {
            writeFound = true;
          }
          if (prop.propertyUuid.toLowerCase() === this.ackUuid.toLowerCase()) {
            ackProperty = prop;
          }
        }
      }
      if (!writeFound || ackProperty === null) {
        throw new Error('Desk SLE command/ACK property not found');
      }
      client.on('propertyChange', this.onPropertyChanged);
      await client.setPropertyNotification(ackProperty, true);
      this.notificationReady = true;
    } catch (err) {
      await this.close();
      throw err;
    }
  }

  async sendFrame(frame: Uint8Array): Promise<void> {
    const client: ssap.Client | null = this.client;
    if (client === null || !this.notificationReady) {
      throw new Error('SLE command/ACK service not ready');
    }
    const packets: Array<Uint8Array> = DeskWireFrame.packets(frame, this.maxWriteBytes);
    for (let i: number = 0; i < packets.length; i++) {
      const property: ssap.Property = {
        serviceUuid: this.serviceUuid,
        propertyUuid: this.writeUuid,
        value: packets[i].buffer
      };
      await client.writeProperty(property, ssap.PropertyWriteType.WRITE);
    }
  }

  async sendFrameAwaitAck(frame: Uint8Array, requestId: number,
                          timeoutMs: number = 15000): Promise<DeskRadioAck> {
    if (!this.notificationReady) throw new Error('SLE ACK notification not subscribed');
    const pending: Promise<DeskRadioAck> = this.ackWaiter.wait(requestId, timeoutMs);
    // Attach a rejection observer immediately: ACK may time out while radio
    // writes are still pending. The original Promise remains rejected.
    pending.catch((_error: Error) => {});
    try {
      await this.sendFrame(frame);
      return await pending;
    } catch (err) {
      this.ackWaiter.fail('SLE transfer failed or ACK missing: ' + String(err));
      throw err;
    }
  }

  async close(): Promise<void> {
    this.ackWaiter.fail('SLE disconnected');
    this.notificationReady = false;
    const client: ssap.Client | null = this.client;
    this.client = null;
    if (client !== null) {
      try {
        client.off('propertyChange', this.onPropertyChanged);
        await client.disconnect();
      } finally {
        client.close();
      }
    }
  }
}
