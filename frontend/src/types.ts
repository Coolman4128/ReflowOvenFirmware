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

export interface ProfileRuntimeStatus {
  running: boolean;
  name: string;
  source: 'none' | 'uploaded' | 'slot';
  slot_index: number;
  current_step_number: number;
  current_step_type: 'none' | 'direct' | 'wait' | 'soak' | 'ramp_time' | 'ramp_rate' | 'jump';
  step_elapsed_s: number;
  profile_elapsed_s: number;
  last_end_reason: string;
}

export interface StatusData {
  controller: ControllerStatus;
  profile: ProfileRuntimeStatus;
  hardware: HardwareStatus;
  wifi: WifiStatus;
  time: TimeStatus;
  data: DataStatus;
  features: {
    profiles_support_execution: boolean;
  };
}

export type ProfileStep =
  | {
    type: 'direct';
    setpoint_c: number;
  }
  | {
    type: 'wait';
    wait_time_s?: number;
    pv_target_c?: number;
  }
  | {
    type: 'soak';
    setpoint_c: number;
    soak_time_s: number;
    guaranteed?: boolean;
    deviation_c?: number;
  }
  | {
    type: 'ramp_time';
    setpoint_c: number;
    ramp_time_s: number;
  }
  | {
    type: 'ramp_rate';
    setpoint_c: number;
    ramp_rate_c_per_s: number;
  }
  | {
    type: 'jump';
    target_step_number: number;
    repeat_count: number;
  };

export interface ProfileDefinition {
  schema_version: number;
  name: string;
  description: string;
  steps: ProfileStep[];
}

export interface ProfileSlotSummary {
  slot_index: number;
  occupied: boolean;
  name: string;
  step_count: number;
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
    kp: number; // legacy alias for heating.kp
    ki: number; // legacy alias for heating.ki
    kd: number; // legacy alias for heating.kd
    heating: {
      kp: number;
      ki: number;
      kd: number;
    };
    cooling: {
      kp: number;
      ki: number;
      kd: number;
    };
    derivative_filter_s: number;
    setpoint_weight: number;
    integral_zone_c: number;
    integral_leak_s: number;
  };
  input_filter_ms: number;
  inputs: number[];
  relays: {
    pwm_relays: number[];
    pwm_relay_weights: Array<{
      relay: number;
      weight: number;
    }>;
    running_relays: number[];
  };
  door: {
    closed_angle_deg: number;
    open_angle_deg: number;
    max_speed_deg_per_s: number;
  };
  cooling: {
    cool_on_band_c: number;
    cool_off_band_c: number;
  };
}
