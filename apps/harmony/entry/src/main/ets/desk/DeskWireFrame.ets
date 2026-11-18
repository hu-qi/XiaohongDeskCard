import { util } from '@kit.ArkTS';
import { DeskSetCommand } from './DeskModels';

/**
 * Desk Wire v1 encoder. Layout is identical to firmware/core/src/desk_wire.c.
 * Supports BLE and SLE; no radio access is performed in this module.
 */
export class DeskWireFrame {
  static readonly FRAME_MAX: number = 384;
  static readonly HEADER_SIZE: number = 18;
  static readonly BEGIN: number = 0xA1;
  static readonly CHUNK: number = 0xA2;
  static readonly COMMIT: number = 0xA3;

  static encodeSet(command: DeskSetCommand): Uint8Array {
    if (!Number.isInteger(command.requestId) || command.requestId < 1 ||
        command.requestId > 4294967295 ||
        !Number.isInteger(command.expectedRevision) ||
        command.expectedRevision < 0 || command.expectedRevision > 4294967295) {
      throw new Error('Invalid requestId or expectedRevision');
    }
    const encoder: util.TextEncoder = new util.TextEncoder('utf-8');
    const fields: Array<string> = [
      command.content.name, command.content.title,
      command.content.organization, command.content.theme
    ];
    const capacities: Array<number> = [49, 65, 97, 17];
    const values: Array<Uint8Array> = [];
    let payloadSize: number = 4;
    for (let i: number = 0; i < fields.length; i++) {
      const value: Uint8Array = encoder.encodeInto(fields[i]);
      if (value.length >= capacities[i]) {
        throw new Error('UTF-8 field exceeds byte limit: ' + i.toString());
      }
      values.push(value);
      payloadSize += value.length;
    }
    if (fields[0].length === 0 ||
      (fields[3] !== 'classic' && fields[3] !== 'modern' && fields[3] !== 'minimal')) {
      throw new Error('Invalid name/theme');
    }
    const total: number = DeskWireFrame.HEADER_SIZE + payloadSize;
    if (total > DeskWireFrame.FRAME_MAX) {
      throw new Error('Frame too large');
    }

    const frame: Uint8Array = new Uint8Array(total);
    const header: DataView = new DataView(frame.buffer);
    frame[0] = 0xD5;
    frame[1] = 1;
    frame[2] = 1;
    frame[3] = 0;
    header.setUint32(4, command.requestId, true);
    header.setUint32(8, command.expectedRevision, true);
    header.setUint16(12, payloadSize, true);
    let offset: number = DeskWireFrame.HEADER_SIZE;
    for (let i: number = 0; i < values.length; i++) {
      const bytes: Uint8Array = values[i];
      frame[offset++] = bytes.length;
      frame.set(bytes, offset);
      offset += bytes.length;
    }
    header.setUint32(14, DeskWireFrame.crc32(frame.subarray(18)), true);
    return frame;
  }

  static crc32(bytes: Uint8Array): number {
    let crc: number = 0xffffffff;
    for (let i: number = 0; i < bytes.length; i++) {
      crc = (crc ^ bytes[i]) >>> 0;
      for (let j: number = 0; j < 8; j++) {
        crc = (crc >>> 1) ^ ((crc & 1) === 1 ? 0xEDB88320 : 0);
      }
    }
    return (crc ^ 0xffffffff) >>> 0;
  }

  /**
   * Application chunk packet contract (transport adaptation is pending):
   * BEGIN  = A1 | uint16(frame length, LE) | uint32(frame CRC, LE)
   * CHUNK  = A2 | uint16(offset, LE) | bytes
   * COMMIT = A3
   *
   * maxWriteBytes is the negotiated attribute value capacity, NOT the MTU.
   * For the default BLE MTU 23, maxWriteBytes is at most 20 bytes.
   */
  static packets(frame: Uint8Array, maxWriteBytes: number): Array<Uint8Array> {
    if (maxWriteBytes < 7 || maxWriteBytes > 65535 ||
        frame.length > DeskWireFrame.FRAME_MAX || frame.length < 18) {
      throw new Error('Unsupported frame or write capacity');
    }
    const begin: Uint8Array = new Uint8Array(7);
    begin[0] = DeskWireFrame.BEGIN;
    new DataView(begin.buffer).setUint16(1, frame.length, true);
    new DataView(begin.buffer).setUint32(3, DeskWireFrame.crc32(frame), true);
    const result: Array<Uint8Array> = [begin];
    const maxChunkData: number = maxWriteBytes - 3;
    for (let offset: number = 0; offset < frame.length; offset += maxChunkData) {
      const length: number = Math.min(maxChunkData, frame.length - offset);
      const chunk: Uint8Array = new Uint8Array(3 + length);
      chunk[0] = DeskWireFrame.CHUNK;
      new DataView(chunk.buffer).setUint16(1, offset, true);
      chunk.set(frame.subarray(offset, offset + length), 3);
      result.push(chunk);
    }
    result.push(new Uint8Array([DeskWireFrame.COMMIT]));
    return result;
  }
}
