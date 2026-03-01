import {
  ApiEnvelope,
  ControllerConfig,
  HistoryPoint,
  ProfileDefinition,
  ProfileSlotSummary,
  StatusData
} from './types';

const API_BASE = (import.meta.env.VITE_API_BASE as string | undefined) ?? '';

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE}${path}`, {
    headers: {
      'Content-Type': 'application/json',
      ...(init?.headers ?? {})
    },
    ...init
  });

  if (response.headers.get('content-type')?.includes('text/csv')) {
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return (await response.text()) as T;
  }

  let json: ApiEnvelope<T>;
  try {
    json = (await response.json()) as ApiEnvelope<T>;
  } catch {
    throw new Error(`HTTP ${response.status}`);
  }

  if (!response.ok) {
    throw new Error(json.error?.message ?? `HTTP ${response.status}`);
  }

  if (!json.ok) {
    throw new Error(json.error?.message ?? 'API error');
  }

  return json.data;
}

export const api = {
  getStatus: () => request<StatusData>('/api/v1/status'),
  startOven: () => request<{}>('/api/v1/control/start', { method: 'POST' }),
  stopOven: () => request<{}>('/api/v1/control/stop', { method: 'POST' }),
  openDoor: () => request<{}>('/api/v1/control/door/open', { method: 'POST' }),
  closeDoor: () => request<{}>('/api/v1/control/door/close', { method: 'POST' }),
  previewDoorAngle: (angle_deg: number) => request<{}>('/api/v1/control/door/preview', {
    method: 'POST',
    body: JSON.stringify({ angle_deg })
  }),
  clearDoorPreview: () => request<{}>('/api/v1/control/door/preview', { method: 'DELETE' }),
  setSetpoint: (setpoint_c: number) => request<{}>('/api/v1/control/setpoint', {
    method: 'POST',
    body: JSON.stringify({ setpoint_c })
  }),
  getHistory: (limit?: number) => request<{ points: HistoryPoint[] }>(
    typeof limit === 'number' ? `/api/v1/data/history?limit=${limit}` : '/api/v1/data/history'
  ),
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
  updatePid: (payload: { kp: number; ki: number; kd: number; derivative_filter_s: number; setpoint_weight: number }) => request<{}>('/api/v1/controller/config/pid', {
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
  updateRelays: (
    pwm_relays: number[],
    running_relays: number[],
    pwm_relay_weights: Array<{ relay: number; weight: number }>
  ) => request<{}>('/api/v1/controller/config/relays', {
    method: 'PUT',
    body: JSON.stringify({ pwm_relays, running_relays, pwm_relay_weights })
  }),
  updateDoorCalibration: (payload: { closed_angle_deg: number; open_angle_deg: number; max_speed_deg_per_s: number }) => request<{}>('/api/v1/controller/config/door', {
    method: 'PUT',
    body: JSON.stringify(payload)
  }),
  getProfilesIndex: () => request<{
    supports_execution: boolean;
    limits: { max_slots: number; max_steps: number };
    uploaded: { present: boolean; name?: string; step_count?: number };
    slots: ProfileSlotSummary[];
  }>('/api/v1/profiles'),
  getUploadedProfile: () => request<ProfileDefinition>('/api/v1/profiles/uploaded'),
  setUploadedProfile: (profile: ProfileDefinition) => request<{}>('/api/v1/profiles/uploaded', {
    method: 'POST',
    body: JSON.stringify(profile)
  }),
  clearUploadedProfile: () => request<{}>('/api/v1/profiles/uploaded', { method: 'DELETE' }),
  getSlotProfile: (slot_index: number) => request<ProfileDefinition>(`/api/v1/profiles/slots/${slot_index}`),
  saveSlotProfile: (slot_index: number, profile: ProfileDefinition) => request<{}>(`/api/v1/profiles/slots/${slot_index}`, {
    method: 'PUT',
    body: JSON.stringify(profile)
  }),
  deleteSlotProfile: (slot_index: number) => request<{}>(`/api/v1/profiles/slots/${slot_index}`, { method: 'DELETE' }),
  runProfile: (payload: { source: 'uploaded' } | { source: 'slot'; slot_index: number }) => request<{}>('/api/v1/profiles/run', {
    method: 'POST',
    body: JSON.stringify(payload)
  }),
  getProfiles: () => request<{
    supports_execution: boolean;
    limits: { max_slots: number; max_steps: number };
    uploaded: { present: boolean; name?: string; step_count?: number };
    slots: ProfileSlotSummary[];
  }>('/api/v1/profiles')
};
