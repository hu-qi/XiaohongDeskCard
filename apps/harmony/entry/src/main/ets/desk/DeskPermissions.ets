import { abilityAccessCtrl, common, Permissions } from '@kit.AbilityKit';
import { DeskChannel } from './DeskModels';

/**
 * Ask for user-grant permissions before scanning or connecting.
 * A module.json5 declaration is insufficient: requests are user mediated.
 */
export class DeskPermissions {
  static async request(context: common.UIAbilityContext,
                       channel: DeskChannel): Promise<boolean> {
    if (channel === DeskChannel.WIFI) {
      return true; // INTERNET is a normal permission, not a runtime prompt.
    }
    const required: Permissions = channel === DeskChannel.SLE ?
      'ohos.permission.ACCESS_NEARLINK' : 'ohos.permission.ACCESS_BLUETOOTH';
    const manager: abilityAccessCtrl.AtManager = abilityAccessCtrl.createAtManager();
    const result = await manager.requestPermissionsFromUser(context, [required]);
    return result.authResults.length === 1 && result.authResults[0] === 0;
  }
}
