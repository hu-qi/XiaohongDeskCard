import { BleGattLink } from './BleGattLink';
import { SleSsapLink } from './SleSsapLink';
import { DeskServiceIds } from './DeskServiceIds';
import { DeskWireFrame } from './DeskWireFrame';
import { DeskRadioAck } from './DeskAckFrame';
import {
  DeskAck, DeskChannel, DeskContent, DeskSetCommand, DeskState, DeskTransport
} from './DeskModels';

/**
 * Real BLE GATT / SLE SSAP transport for the WS63 desk card.
 *
 * This is not a simulation: it opens the radio link, discovers the Desk Card
 * service, writes the Desk Wire v1 frame in BEGIN/CHUNK/COMMIT packets and
 * waits for the device's own 16-byte business ACK. A successful radio write is
 * never treated as proof that the card was updated.
 *
 * Status: Desk Wire v1 has no read-back operation over BLE/SLE. Desk Core does
 * reject a SET whose expectedRevision is stale *before* persisting anything, and
 * that rejection still carries the device's real revision. getStatus() uses
 * exactly that as a non-mutating revision probe, so callers (and DeskRouter)
 * always start from the device's actual revision.
 */

/** Default BLE MTU 23 leaves 20 bytes per write-with-response. */
const BLE_WRITE_BYTES: number = 20;
/** The WS63 SLE server negotiates MTU 512; stay well under it until measured. */
const SLE_WRITE_BYTES: number = 64;
const ACK_TIMEOUT_MS: number = 15000;

/**
 * expectedRevision sent by the probe. No real revision can equal this unless
 * the device is at UINT32_MAX, and then Desk Core answers REVISION_EXHAUSTED.
 * Both outcomes are refusals, so the probe never changes the card.
 */
const PROBE_REVISION: number = 4294967295;
/** Probe request IDs live above any ID the UI generates (it stays under 4e9). */
const PROBE_ID_BASE: number = 4294967000;

export class RadioDeskTransport implements DeskTransport {
  readonly channel: DeskChannel;
  private address: string;
  private bleLink: BleGattLink | null = null;
  private sleLink: SleSsapLink | null = null;
  private revision: number = 0;
  private known: boolean = false;
  private applied: DeskContent = {
    name: '', title: '', organization: '', theme: 'classic'
  };
  private probeSeq: number = 0;

  constructor(channel: DeskChannel, address: string) {
    if (channel === DeskChannel.WIFI) {
      throw new Error('RadioDeskTransport handles BLE and SLE only');
    }
    if (address.length === 0) {
      throw new Error('A discovered device address is required');
    }
    this.channel = channel;
    this.address = address;
  }

  /** Open the radio link and subscribe to the ACK notification. */
  async connect(): Promise<void> {
    if (this.bleLink !== null || this.sleLink !== null) {
      throw new Error('Desk link already open');
    }
    if (this.channel === DeskChannel.BLE) {
      const link: BleGattLink = new BleGattLink(
        DeskServiceIds.SERVICE, DeskServiceIds.COMMAND, DeskServiceIds.ACK,
        BLE_WRITE_BYTES);
      await link.connect(this.address);
      this.bleLink = link;
      return;
    }
    const link: SleSsapLink = new SleSsapLink(
      DeskServiceIds.SERVICE, DeskServiceIds.COMMAND, DeskServiceIds.ACK,
      SLE_WRITE_BYTES);
    await link.connect(this.address);
    this.sleLink = link;
  }

  /** Device revision and last confirmed content, from a real device reply. */
  async getStatus(): Promise<DeskState> {
    if (!this.known) {
      await this.probeRevision();
    }
    return this.snapshot();
  }

  async setContent(command: DeskSetCommand): Promise<DeskAck> {
    if (!this.known) {
      // A caller that skipped getStatus() still gets the right revision.
      await this.probeRevision();
    }
    const ack: DeskRadioAck =
      await this.transmit(DeskWireFrame.encodeSet(command), command.requestId);

    // Every ACK reports the device's revision: the new one on OK, the current
    // one on CONFLICT / DUPLICATE_ID. Either way it is the value the next
    // command must expect.
    this.revision = ack.revision;
    if (ack.result === 'OK') {
      this.applied = {
        name: command.content.name,
        title: command.content.title,
        organization: command.content.organization,
        theme: command.content.theme
      };
    }
    return {
      result: ack.result,
      transport: this.channel === DeskChannel.BLE ? 'BLE_GATT' : 'SLE_SSAP',
      state: this.snapshot()
    };
  }

  async close(): Promise<void> {
    const ble: BleGattLink | null = this.bleLink;
    const sle: SleSsapLink | null = this.sleLink;
    this.bleLink = null;
    this.sleLink = null;
    if (ble !== null) {
      ble.close();
    }
    if (sle !== null) {
      await sle.close();
    }
  }

  /**
   * Learn the device revision without changing the card. Desk Core compares
   * expectedRevision before saving, so a deliberate mismatch is rejected with
   * CONFLICT and the ACK still reports the live revision.
   */
  private async probeRevision(): Promise<void> {
    this.probeSeq += 1;
    const command: DeskSetCommand = {
      requestId: PROBE_ID_BASE + this.probeSeq,
      expectedRevision: PROBE_REVISION,
      content: { name: 'probe', title: '', organization: '', theme: 'classic' }
    };
    const ack: DeskRadioAck = await this.transmit(
      DeskWireFrame.encodeSet(command), command.requestId);
    if (ack.result !== 'CONFLICT' && ack.result !== 'REVISION_EXHAUSTED') {
      // Anything else means the probe was not treated as a refused write.
      throw new Error('Desk revision probe was not refused: ' + ack.result);
    }
    this.revision = ack.revision;
    this.known = true;
  }

  private async transmit(frame: Uint8Array,
                         requestId: number): Promise<DeskRadioAck> {
    const ble: BleGattLink | null = this.bleLink;
    if (ble !== null) {
      return ble.sendFrameAwaitAck(frame, requestId, ACK_TIMEOUT_MS);
    }
    const sle: SleSsapLink | null = this.sleLink;
    if (sle !== null) {
      return sle.sendFrameAwaitAck(frame, requestId, ACK_TIMEOUT_MS);
    }
    throw new Error('Desk link is not connected');
  }

  private snapshot(): DeskState {
    const content: DeskContent = {
      name: this.applied.name,
      title: this.applied.title,
      organization: this.applied.organization,
      theme: this.applied.theme
    };
    return {
      revision: this.revision,
      content: content,
      display: {
        name: content.name,
        title: content.title,
        organization: content.organization,
        theme: content.theme
      },
      device: DeskServiceIds.ADVERTISED_NAME + '@' + this.address,
      // Only the device can attest hardware validation; the app cannot.
      hardwareValidated: false
    };
  }
}
