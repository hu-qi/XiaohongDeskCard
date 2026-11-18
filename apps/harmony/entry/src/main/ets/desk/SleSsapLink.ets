import { ssap } from '@kit.NearLinkKit';
import { DeskWireFrame } from './DeskWireFrame';
import { isNearLinkAvailable } from './NearLinkCapability';

/**
 * NearLink SSAP client write adapter using @kit.NearLinkKit (API 13+).
 *
 * Must use UUIDs advertised by the future WS63 SSAP Server.
 * Success of sendFrame means write promises completed, NOT business ACK.
 * Business ACK/notify and device authorization still require WS63 work.
 */
export class SleSsapLink {
  private client: ssap.Client | null = null;
  private serviceUuid: string;
  private writeUuid: string;
  private maxWriteBytes: number;

  constructor(serviceUuid: string, writeUuid: string, maxWriteBytes: number = 64) {
    this.serviceUuid = serviceUuid;
    this.writeUuid = writeUuid;
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
      let found: boolean = false;
      for (let i: number = 0; i < services.length; i++) {
        if (services[i].serviceUuid.toLowerCase() !== this.serviceUuid.toLowerCase()) {
          continue;
        }
        for (let j: number = 0; j < services[i].properties.length; j++) {
          if (services[i].properties[j].propertyUuid.toLowerCase() ===
              this.writeUuid.toLowerCase()) found = true;
        }
      }
      if (!found) throw new Error('Desk SLE write property not found');
    } catch (err) {
      await this.close();
      throw err;
    }
  }

  async sendFrame(frame: Uint8Array): Promise<void> {
    const client: ssap.Client | null = this.client;
    if (client === null) throw new Error('SLE SSAP not connected');
    // maxWriteBytes must be revised after MTU negotiation.
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

  async close(): Promise<void> {
    const client: ssap.Client | null = this.client;
    this.client = null;
    if (client !== null) {
      try {
        await client.disconnect();
      } finally {
        client.close();
      }
    }
  }
}
