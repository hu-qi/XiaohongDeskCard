/**
 * Transport-independent coordinator for HarmonyOS DeskCard.
 *
 * IMPORTANT: Fallback is safe ONLY before a SET is attempted. Once setContent
 * starts, a timeout/disconnect is an UNKNOWN OUTCOME: the device might have
 * applied the command and lost the ACK. NEVER blindly resend on another radio.
 *
 * This file intentionally depends on no HarmonyOS SDK and can be tested by
 * Node.js type stripping. ArkTS compatibility still requires DevEco validation.
 */
export type DeskRouterChannel = 'sle' | 'ble' | 'wifi';

export interface DeskRouterContent {
  name: string;
  title: string;
  organization: string;
  theme: string;
}

export interface DeskRouterState {
  revision: number;
  content: DeskRouterContent;
  display: DeskRouterContent;
}

export interface DeskRouterCommand {
  requestId: number;
  expectedRevision: number;
  content: DeskRouterContent;
}

export interface DeskRouterAck {
  result: string;
  transport: string;
  state: DeskRouterState;
}

export interface DeskRouterTransport {
  getStatus(): Promise<DeskRouterState>;
  setContent(command: DeskRouterCommand): Promise<DeskRouterAck>;
  close?(): Promise<void> | void;
}

export interface DeskRouterCandidate {
  channel: DeskRouterChannel;
  open(): Promise<DeskRouterTransport>;
}

export interface DeskRouterResult {
  channel: DeskRouterChannel;
  ack: DeskRouterAck;
  skipped: Array<DeskRouterChannel>;
}

export class DeskUnavailableError extends Error {
  constructor() {
    super('NO_DESK_TRANSPORT_AVAILABLE');
    this.name = 'DeskUnavailableError';
  }
}

export class DeskOutcomeUnknownError extends Error {
  readonly channel: DeskRouterChannel;
  readonly requestId: number;

  constructor(channel: DeskRouterChannel, requestId: number) {
    super('DESK_OUTCOME_UNKNOWN: check device status before retry');
    this.name = 'DeskOutcomeUnknownError';
    this.channel = channel;
    this.requestId = requestId;
  }
}

export class DeskCommandRejectedError extends Error {
  readonly result: string;
  readonly channel: DeskRouterChannel;

  constructor(channel: DeskRouterChannel, result: string) {
    super('DESK_COMMAND_REJECTED: ' + result);
    this.name = 'DeskCommandRejectedError';
    this.channel = channel;
    this.result = result;
  }
}

export class DeskRouter {
  private active: boolean = false;
  private candidates: Array<DeskRouterCandidate>;

  constructor(candidates: Array<DeskRouterCandidate>) {
    this.candidates = candidates.slice();
  }

  /**
   * Caller must supply a unique request ID. Reuse the SAME ID only when
   * manually resolving an unknown outcome with explicit device state proof.
   */
  async update(content: DeskRouterContent, requestId: number): Promise<DeskRouterResult> {
    if (this.active) throw new Error('DESK_UPDATE_IN_PROGRESS');
    if (!Number.isInteger(requestId) || requestId < 1 || requestId > 4294967295) {
      throw new Error('INVALID_REQUEST_ID');
    }
    this.active = true;
    const skipped: Array<DeskRouterChannel> = [];
    try {
      for (let i = 0; i < this.candidates.length; i++) {
        const candidate: DeskRouterCandidate = this.candidates[i];
        let transport: DeskRouterTransport | null = null;
        try {
          // Discovery, connection, pairing, and permission checks belong to open().
          // No content has been transmitted yet, so moving to the next
          // candidate on failure is safe.
          transport = await candidate.open();
          const state: DeskRouterState = await transport.getStatus();
          if (!Number.isInteger(state.revision) ||
              state.revision < 0 || state.revision > 4294967295) {
            throw new Error('INVALID_DEVICE_REVISION');
          }

          const command: DeskRouterCommand = {
            requestId: requestId,
            expectedRevision: state.revision,
            content: {
              name: content.name,
              title: content.title,
              organization: content.organization,
              theme: content.theme
            }
          };

          // Boundary of uncertainty: once we start invoking setContent, a
          // failed return cannot tell whether WS63 already changed state.
          let ack: DeskRouterAck;
          try {
            ack = await transport.setContent(command);
          } catch (_error) {
            throw new DeskOutcomeUnknownError(candidate.channel, requestId);
          }

          if (ack.result !== 'OK') {
            // A definitive business rejection must be shown to the user.
            // It must NOT silently trigger another transport write.
            throw new DeskCommandRejectedError(candidate.channel, ack.result);
          }
          if (!ack.state || ack.state.revision !== state.revision + 1) {
            // Some already-applied operations may be idempotent, but without a
            // matching business ACK + verified new state, do not say success.
            throw new DeskOutcomeUnknownError(candidate.channel, requestId);
          }
          return { channel: candidate.channel, ack: ack, skipped: skipped };
        } catch (error) {
          if (error instanceof DeskOutcomeUnknownError ||
              error instanceof DeskCommandRejectedError) {
            throw error;
          }
          // Only pre-write failures are eligible for automatic fallback.
          skipped.push(candidate.channel);
        } finally {
          if (transport !== null && transport.close !== undefined) {
            try { await transport.close(); } catch (_error) {
              // Close failures never change the outcome of a completed update.
            }
          }
        }
      }
      throw new DeskUnavailableError();
    } finally {
      this.active = false;
    }
  }
}
