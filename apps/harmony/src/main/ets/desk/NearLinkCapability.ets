/**
 * Runtime NearLink capability guard. Requires HarmonyOS SDK API 26+.
 * Do NOT import this file into lower API SDK builds without compatibility
 * handling at the project/dependency boundary.
 *
 * Presence of this API or a true result does not guarantee that a particular
 * WS63 SSAP service will be compatible or connect successfully.
 */
import { manager } from '@kit.ConnectivityKit';

export function isNearLinkAvailable(): boolean {
  try {
    return manager.isNearLinkSupported();
  } catch (error) {
    return false;
  }
}
