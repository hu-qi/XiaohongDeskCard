import { http } from '@kit.NetworkKit';
import { DeskAck, DeskChannel, DeskSetCommand, DeskState, DeskTransport } from './DeskModels';

/** Real network HTTP client; target WS63 HTTP server not implemented yet. */
export class WifiHttpTransport implements DeskTransport {
  readonly channel: DeskChannel = DeskChannel.WIFI;
  private baseUrl: string;
  private token: string;

  constructor(baseUrl: string, token: string) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.token = token;
  }

  async getStatus(): Promise<DeskState> {
    const data: string = await this.request('/api/v1/desk/status', http.RequestMethod.GET);
    return JSON.parse(data) as DeskState;
  }

  async setContent(command: DeskSetCommand): Promise<DeskAck> {
    const data: string = await this.request(
      '/api/v1/desk/content', http.RequestMethod.PUT, JSON.stringify(command));
    return JSON.parse(data) as DeskAck;
  }

  private async request(path: string, method: http.RequestMethod, body?: string): Promise<string> {
    const task: http.HttpRequest = http.createHttp();
    try {
      const result: http.HttpResponse = await task.request(this.baseUrl + path, {
        method: method,
        header: {
          'Content-Type': 'application/json',
          'Authorization': 'Bearer ' + this.token
        },
        extraData: body === undefined ? '' : body,
        connectTimeout: 5000,
        readTimeout: 10000
      });
      if (result.responseCode < 200 || result.responseCode >= 300) {
        throw new Error('Desk HTTP ' + result.responseCode.toString());
      }
      if (typeof result.result !== 'string') {
        throw new Error('Unexpected HTTP response data type');
      }
      return result.result;
    } finally {
      task.destroy();
    }
  }
}
