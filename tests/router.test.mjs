/**
 * Executes the same TypeScript router intended for HarmonyOS.
 * Node 22's native type stripping is used, without npm dependencies.
 * This verifies routing policy, NOT radio APIs or DevEco compilation.
 */
import assert from 'node:assert/strict';
import test from 'node:test';
import {
  DeskRouter,
  DeskUnavailableError,
  DeskOutcomeUnknownError,
  DeskCommandRejectedError
} from '../apps/harmony/entry/src/main/ets/desk/DeskRouter.ts';

const content = {
  name: '张三', title: '开发者',
  organization: 'OpenHarmony', theme: 'classic'
};

function state(revision) {
  return { revision, content: { ...content }, display: { ...content } };
}

function candidate(channel, config = {}) {
  const events = config.events ?? [];
  return {
    channel,
    async open() {
      events.push(channel + ':open');
      if (config.openError) throw Error('offline');
      return {
        async getStatus() {
          events.push(channel + ':status');
          if (config.statusError) throw Error('status offline');
          return state(config.revision ?? 4);
        },
        async setContent(command) {
          events.push(channel + ':write:' + command.requestId);
          if (config.writeError) throw Error('ACK timeout');
          if (config.hold) await config.hold;
          const result = config.result ?? 'OK';
          return {
            result,
            transport: channel.toUpperCase(),
            state: state(config.ackRevision ?? 5)
          };
        },
        async close() {
          events.push(channel + ':close');
          if (config.closeError) throw Error('close error');
        }
      };
    }
  };
}

test('pre-write SLE unavailable falls back BLE, then returns verified ACK', async () => {
  const events = [];
  const router = new DeskRouter([
    candidate('sle', { openError: true, events }),
    candidate('ble', { events }),
    candidate('wifi', { events })
  ]);
  const result = await router.update(content, 101);
  assert.equal(result.channel, 'ble');
  assert.deepEqual(result.skipped, ['sle']);
  assert.equal(result.ack.state.revision, 5);
  assert.deepEqual(events, ['sle:open', 'ble:open', 'ble:status', 'ble:write:101', 'ble:close']);
});

test('pre-write status failure allows fallback to Wi-Fi', async () => {
  const events = [];
  const router = new DeskRouter([
    candidate('ble', { statusError: true, events }),
    candidate('wifi', { events })
  ]);
  const result = await router.update(content, 202);
  assert.equal(result.channel, 'wifi');
  assert.deepEqual(events, [
    'ble:open', 'ble:status', 'ble:close',
    'wifi:open', 'wifi:status', 'wifi:write:202', 'wifi:close'
  ]);
});

test('timeout after beginning a write is UNKNOWN and MUST NOT retry elsewhere', async () => {
  const events = [];
  const router = new DeskRouter([
    candidate('sle', { writeError: true, events }),
    candidate('ble', { events }),
    candidate('wifi', { events })
  ]);
  await assert.rejects(router.update(content, 303), err => {
    assert.ok(err instanceof DeskOutcomeUnknownError);
    assert.equal(err.channel, 'sle');
    assert.equal(err.requestId, 303);
    return true;
  });
  assert.deepEqual(events, ['sle:open', 'sle:status', 'sle:write:303', 'sle:close']);
});

test('explicit business CONFLICT is definitive and MUST NOT be retried', async () => {
  const events = [];
  const router = new DeskRouter([
    candidate('sle', { result: 'CONFLICT', events }),
    candidate('ble', { events })
  ]);
  await assert.rejects(router.update(content, 404), err => {
    assert.ok(err instanceof DeskCommandRejectedError);
    assert.equal(err.result, 'CONFLICT');
    return true;
  });
  assert.ok(!events.includes('ble:open'));
});

test('success without matching revision cannot claim a successful update', async () => {
  const events = [];
  const router = new DeskRouter([
    candidate('ble', { ackRevision: 4, events }),
    candidate('wifi', { events })
  ]);
  await assert.rejects(router.update(content, 505), DeskOutcomeUnknownError);
  assert.ok(!events.includes('wifi:open'));
});

test('all channels inaccessible returns an unavailable error', async () => {
  const router = new DeskRouter([
    candidate('sle', { openError: true }),
    candidate('ble', { statusError: true }),
    candidate('wifi', { openError: true })
  ]);
  await assert.rejects(router.update(content, 606), DeskUnavailableError);
});

test('parallel updates are rejected before reaching transport', async () => {
  let release;
  const hold = new Promise(resolve => { release = resolve; });
  const events = [];
  const router = new DeskRouter([candidate('ble', { hold, events })]);
  const first = router.update(content, 707);
  await assert.rejects(router.update(content, 708), /DESK_UPDATE_IN_PROGRESS/);
  release();
  const result = await first;
  assert.equal(result.channel, 'ble');
  assert.deepEqual(events, ['ble:open', 'ble:status', 'ble:write:707', 'ble:close']);
});

test('bad request ID and cleanup errors do not bypass validation', async () => {
  const events = [];
  const router = new DeskRouter([candidate('wifi', { closeError: true, events })]);
  await assert.rejects(router.update(content, 0), /INVALID_REQUEST_ID/);
  assert.deepEqual(events, []);
  const result = await router.update(content, 909);
  assert.equal(result.channel, 'wifi');
  assert.ok(events.includes('wifi:close'));
});
