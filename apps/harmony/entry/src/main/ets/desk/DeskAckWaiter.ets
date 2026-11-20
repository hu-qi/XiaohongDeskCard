import { DeskAckFrame, DeskRadioAck } from './DeskAckFrame';

/**
 * Exactly one in-flight business command per connected device.
 * Notify callbacks pass the 16-byte Desk ACK to accept().
 *
 * Important: writing all radio packets is not proof that LVGL applied content.
 * Only a valid ACK with matching request ID can complete the transaction.
 */
export class DeskAckWaiter {
  private requestId: number = 0;
  private resolveHandler: ((ack: DeskRadioAck) => void) | null = null;
  private rejectHandler: ((error: Error) => void) | null = null;
  private timeoutId: number = -1;

  wait(requestId: number, timeoutMs: number): Promise<DeskRadioAck> {
    if (this.resolveHandler !== null) {
      throw new Error('A Desk command is already awaiting ACK');
    }
    if (!Number.isInteger(requestId) || requestId <= 0 || timeoutMs < 100) {
      throw new Error('Invalid requestId or ACK timeout');
    }
    return new Promise<DeskRadioAck>((resolve, reject) => {
      this.requestId = requestId;
      this.resolveHandler = resolve;
      this.rejectHandler = reject;
      this.timeoutId = setTimeout(() => {
        this.fail('DESK_ACK_TIMEOUT');
      }, timeoutMs);
    });
  }

  accept(value: Uint8Array): boolean {
    if (this.resolveHandler === null) {
      return false;
    }
    let ack: DeskRadioAck;
    try {
      ack = DeskAckFrame.decode(value, this.requestId);
    } catch (_error) {
      // Noise/late ACK for another request cannot complete the transaction.
      return false;
    }
    const resolve = this.resolveHandler;
    this.clear();
    if (resolve !== null) {
      resolve(ack);
    }
    return true;
  }

  fail(reason: string): void {
    const reject = this.rejectHandler;
    this.clear();
    if (reject !== null) {
      reject(new Error(reason));
    }
  }

  isWaiting(): boolean {
    return this.resolveHandler !== null;
  }

  private clear(): void {
    if (this.timeoutId !== -1) {
      clearTimeout(this.timeoutId);
      this.timeoutId = -1;
    }
    this.requestId = 0;
    this.resolveHandler = null;
    this.rejectHandler = null;
  }
}
