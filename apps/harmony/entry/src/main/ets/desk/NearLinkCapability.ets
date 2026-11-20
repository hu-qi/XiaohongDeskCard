/**
 * NearLink capability probe.
 *
 * The project compiles against `targetSdkVersion 6.0.2(22)`, where the
 * `@kit.NearLinkKit` module itself is available but individual manager members
 * are gated by the running system image. Never assume the hardware/OS supports
 * NearLink: ask the compiled-in system capability first and keep the SDK call
 * inside a guard so an unsupported device degrades to `false` instead of
 * throwing at startup.
 */
import { manager } from '@kit.NearLinkKit';

export const NEARLINK_SYSTEM_CAPABILITY: string =
  'SystemCapability.Communication.NearLink.Base';

export function isNearLinkCapabilityDeclared(): boolean {
  return canIUse(NEARLINK_SYSTEM_CAPABILITY);
}

export function isNearLinkAvailable(): boolean {
  if (!isNearLinkCapabilityDeclared()) {
    return false;
  }
  try {
    return manager.isNearLinkSupported();
  } catch (_error) {
    return false;
  }
}
