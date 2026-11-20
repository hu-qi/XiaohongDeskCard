/**
 * NearLink Kit compatible with API 13+ (HarmonyOS 5.0.1+).
 * @kit.ConnectivityKit has newer API 26 equivalents; do not mix SDK
 * generations in a single compiled target without explicit migration.
 */
import { manager } from '@kit.NearLinkKit';

export function isNearLinkAvailable(): boolean {
  try {
    return manager.isNearLinkSupported();
  } catch (_error) {
    return false;
  }
}
