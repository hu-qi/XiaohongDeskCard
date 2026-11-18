/**
 * Wi-Fi HTTP transport for a future HarmonyOS ArkTS app.
 *
 * Uses official NetworkKit HttpRequest APIs. This module is not compiled by
 * host CMake/CI and has NOT been verified in DevEco/phone.
 *
 * Set baseUrl to the actual reachable device or simulator address; localhost
 * always refers to the phone itself, not the development Mac.
 */
import { http } from '@kit.NetworkKit';
import { DeskAck, DeskChannel, DeskSetCommand, DeskState, DeskTransport } from './DeskModels';

export class WifiHttpTransport implements DeskTransport {
  readonly channel: DeskChannel = DeskChannel.WIFI;

  constructor(private readonly baseUrl: string, private readonly token: string) {
  }

  async getStatus(): Promise<DeskState> {
    const response: string = await this.request('/api/v1/desk/status', http.RequestMethod.GET);
    return JSON.parse(response) as DeskState;
  }

  async setContent(command: DeskSetCommand): Promise<DeskAck> {
    const response: string = await this.request(
      '/api/v1/desk/content', http.RequestMethod.PUT, JSON.stringify(command));
    return JSON.parse(response) as DeskAck;
  }

  private async request(path: string, method: http.RequestMethod, body?: string): Promise<string> {
    const task: http.HttpRequest = http.createHttp();
    try {
      const headers: Record<string, string> = {
        'Content-Type': 'application/json',
        'Authorization': 'Bearer ' + this.token
      };
      const result: http.HttpResponse = await task.request(this.baseUrl + path, {
        method: method,
        header: headers,
        extraData: body === undefined ? '' : body,
        connectTimeout: 5000,
        readTimeout: 10000
      });
      if (result.responseCode < 200 || result.responseCode >= 300) {
        throw new Error('Desk HTTP failed: ' + result.responseCode.toString());
      }
      if (typeof result.result !== 'string') {
        throw new Error('Desk HTTP response is not a JSON string');
      }
      return result.result;
    } finally {
      task.destroy();
    }
  }
}
