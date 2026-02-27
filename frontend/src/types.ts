export interface ControllerStatus {
  running: boolean;
  door_open: boolean;
  alarming: boolean;
  state: string;
  setpoint_c: number;
  process_value_c: number;
  pid_output: number;
  p_term: number;
  i_term: number;
  d_term: number;
}

export interface HardwareStatus {
  temperatures_c: number[];
  relay_states: boolean[];
  servo_angle: number;
}

export interface WifiStatus {
  connected: boolean;
  ssid: string;
  ip: string;
  rssi: number;
}

export interface TimeStatus {
  synced: boolean;
  unix_time_ms: number;
  timezone: string;
}

export interface DataStatus {
  logging_enabled: boolean;
  log_interval_ms: number;
  max_time_ms: number;
  points: number;
  bytes_used: number;
  max_points: number;
}

export interface StatusData {
  controller: ControllerStatus;
  hardware: HardwareStatus;
  wifi: WifiStatus;
  time: TimeStatus;
  data: DataStatus;
  features: {
    profiles_support_execution: boolean;
  };
}

export interface ApiEnvelope<T> {
  ok: boolean;
  data: T;
  error?: {
    code: string;
    message: string;
  };
}

export interface HistoryPoint {
  timestamp: number;
  setpoint: number;
  process_value: number;
  pid_output: number;
  p: number;
  i: number;
  d: number;
  temperatures: number[];
  relay_states: number;
  servo_angle: number;
  running: boolean;
}

export interface ControllerConfig {
  pid: {
    kp: number;
    ki: number;
    kd: number;
    derivative_filter_s: number;
    setpoint_weight: number;
  };
  input_filter_ms: number;
  inputs: number[];
  relays: {
    pwm_relays: number[];
    running_relays: number[];
  };
}
