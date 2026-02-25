import { ApiEnvelope, ControllerConfig, HistoryPoint, StatusData } from './types';

const API_BASE = (import.meta.env.VITE_API_BASE as string | undefined) ?? '';

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE}${path}`, {
    headers: {
      'Content-Type': 'application/json',
      ...(init?.headers ?? {})
    },
    ...init
  });

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }

  if (response.headers.get('content-type')?.includes('text/csv')) {
    return (await response.text()) as T;
  }

  const json = (await response.json()) as ApiEnvelope<T>;
  if (!json.ok) {
    throw new Error(json.error?.message ?? 'API error');
  }

  return json.data;
}

export const api = {
  getStatus: () => request<StatusData>('/api/v1/status'),
  startOven: () => request<{}>('/api/v1/control/start', { method: 'POST' }),
  stopOven: () => request<{}>('/api/v1/control/stop', { method: 'POST' }),
  setSetpoint: (setpoint_c: number) => request<{}>('/api/v1/control/setpoint', {
    method: 'POST',
    body: JSON.stringify({ setpoint_c })
  }),
  getHistory: (limit = 500) => request<{ points: HistoryPoint[] }>(`/api/v1/data/history?limit=${limit}`),
  clearHistory: () => request<{}>('/api/v1/data/history', { method: 'DELETE' }),
  exportCsv: () => request<string>('/api/v1/data/export.csv'),
  getTimeSettings: () => request<{ timezone: string; synced: boolean; unix_time_ms: number }>('/api/v1/settings/time'),
  setTimeSettings: (timezone: string) => request<{}>('/api/v1/settings/time', {
    method: 'PUT',
    body: JSON.stringify({ timezone })
  }),
  getWifiStatus: () => request<{ connected: boolean; ssid: string; ip: string; rssi: number }>('/api/v1/settings/wifi/status'),
  scanWifi: () => request<{ networks: Array<{ ssid: string; rssi: number; auth_mode: number }> }>('/api/v1/settings/wifi/networks'),
  connectWifi: (ssid: string, password: string) => request<{}>('/api/v1/settings/wifi/connect', {
    method: 'POST',
    body: JSON.stringify({ ssid, password })
  }),
  disconnectWifi: () => request<{}>('/api/v1/settings/wifi/disconnect', { method: 'POST' }),
  getDataSettings: () => request<{ logging_enabled: boolean; log_interval_ms: number; max_time_ms: number; points: number; bytes_used: number; max_points: number }>('/api/v1/settings/data'),
  setDataSettings: (payload: { logging_enabled: boolean; log_interval_ms: number; max_time_ms: number }) => request<{}>('/api/v1/settings/data', {
    method: 'PUT',
    body: JSON.stringify(payload)
  }),
  getControllerConfig: () => request<ControllerConfig>('/api/v1/controller/config'),
  updatePid: (payload: { kp: number; ki: number; kd: number; derivative_filter_s: number }) => request<{}>('/api/v1/controller/config/pid', {
    method: 'PUT',
    body: JSON.stringify(payload)
  }),
  updateInputFilter: (input_filter_ms: number) => request<{}>('/api/v1/controller/config/filter', {
    method: 'PUT',
    body: JSON.stringify({ input_filter_ms })
  }),
  updateInputs: (channels: number[]) => request<{}>('/api/v1/controller/config/inputs', {
    method: 'PUT',
    body: JSON.stringify({ channels })
  }),
  updateRelays: (pwm_relays: number[], running_relays: number[]) => request<{}>('/api/v1/controller/config/relays', {
    method: 'PUT',
    body: JSON.stringify({ pwm_relays, running_relays })
  }),
  getProfiles: () => request<{ supports_execution: boolean; profiles: Array<{ id: string; name: string; description: string }> }>('/api/v1/profiles')
};
