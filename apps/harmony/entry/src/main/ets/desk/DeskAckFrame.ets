import { DeskWireFrame } from './DeskWireFrame';

/**
 * Business ACK v1, separate from GATT/SSAP write confirmation.
 * Layout matches firmware/core/src/desk_wire.c::desk_wire_encode_ack.
 */
export interface DeskRadioAck {
  requestId: number;
  revision: number;
  resultCode: number;
  result: string;
}

export class DeskAckFrame {
  private static readonly NAMES: Array<string> = [
    'OK', 'BAD_INPUT', 'UNAUTHORIZED', 'CONFLICT',
    'DUPLICATE_ID', 'STORE_ERROR', 'RENDER_ERROR', 'REVISION_EXHAUSTED'
  ];

  static decode(bytes: Uint8Array, expectedRequestId: number): DeskRadioAck {
    if (bytes.length !== 16 || bytes[0] !== 0xD6 ||
      bytes[1] !== 1 || bytes[3] !== 0) {
      throw new Error('Malformed Desk ACK header/size');
    }
    const view: DataView = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const code: number = bytes[2];
    if (code >= DeskAckFrame.NAMES.length) {
      throw new Error('Unknown Desk ACK result code');
    }
    const actualCrc: number = DeskWireFrame.crc32(bytes.subarray(0, 12));
    if (view.getUint32(12, true) !== actualCrc) {
      throw new Error('Desk ACK CRC mismatch');
    }
    const requestId: number = view.getUint32(4, true);
    if (requestId === 0 || requestId !== expectedRequestId) {
      throw new Error('Desk ACK request ID mismatch');
    }
    return {
      requestId: requestId,
      revision: view.getUint32(8, true),
      resultCode: code,
      result: DeskAckFrame.NAMES[code]
    };
  }
}
