import http from 'node:http';
import { WebSocketServer } from 'ws';
import { parse } from 'node:url';

const port = 8787;

const state = {
  running: false,
  doorOpen: false,
  alarming: false,
  state: 'Idle',
  setpoint: 200,
  process: 28,
  pid: 0,
  p: 0,
  i: 0,
  d: 0,
  timezone: 'EST5EDT,M3.2.0/2,M11.1.0/2',
  loggingEnabled: true,
  logIntervalMs: 1000,
  maxTimeMs: 1800000,
  wifiConnected: true,
  wifiSsid: 'ReflowLab',
  wifiIp: '192.168.1.88',
  wifiRssi: -52,
  points: []
};

const sampleProfiles = [
  {
    id: 'sample-lead-free',
    name: 'Lead-Free SAC305 (Sample)',
    description: 'Sample profile. Execution not implemented yet.'
  }
];

function json(res, code, payload) {
  res.writeHead(code, {
    'Content-Type': 'application/json',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Content-Type'
  });
  res.end(JSON.stringify(payload));
}

function makeStatusData() {
  return {
    controller: {
      running: state.running,
      door_open: state.doorOpen,
      alarming: state.alarming,
      state: state.state,
      setpoint_c: state.setpoint,
      process_value_c: state.process,
      pid_output: state.pid,
      p_term: state.p,
      i_term: state.i,
      d_term: state.d
    },
    hardware: {
      temperatures_c: [state.process + 0.3, state.process - 0.4, state.process + 0.7, state.process - 0.1],
      relay_states: [state.running, state.running, false, false, false, false],
      servo_angle: state.pid < 0 ? Math.min(180, Math.abs(state.pid) * 1.8) : 0
    },
    wifi: {
      connected: state.wifiConnected,
      ssid: state.wifiSsid,
      ip: state.wifiIp,
      rssi: state.wifiRssi
    },
    time: {
      synced: true,
      unix_time_ms: Date.now(),
      timezone: state.timezone
    },
    data: {
      logging_enabled: state.loggingEnabled,
      log_interval_ms: state.logIntervalMs,
      max_time_ms: state.maxTimeMs,
      points: state.points.length,
      bytes_used: state.points.length * 64,
      max_points: 8000
    },
    features: {
      profiles_support_execution: false
    }
  };
}

function envelope(data) {
  return { ok: true, data };
}

function errEnvelope(code, message) {
  return { ok: false, error: { code, message } };
}

async function readBody(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString('utf8');
}

const server = http.createServer(async (req, res) => {
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Headers': 'Content-Type',
      'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE,OPTIONS'
    });
    res.end();
    return;
  }

  const parsed = parse(req.url || '', true);
  const path = parsed.pathname || '/';

  if (req.method === 'GET' && path === '/api/v1/status') {
    json(res, 200, envelope(makeStatusData()));
    return;
  }

  if (req.method === 'POST' && path === '/api/v1/control/start') {
    state.running = true;
    state.state = 'Steady State';
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'POST' && path === '/api/v1/control/stop') {
    state.running = false;
    state.state = 'Idle';
    state.pid = 0;
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'POST' && path === '/api/v1/control/setpoint') {
    const body = JSON.parse(await readBody(req));
    state.setpoint = Number(body.setpoint_c ?? state.setpoint);
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/settings/time') {
    json(res, 200, envelope({ timezone: state.timezone, synced: true, unix_time_ms: Date.now() }));
    return;
  }

  if (req.method === 'PUT' && path === '/api/v1/settings/time') {
    const body = JSON.parse(await readBody(req));
    state.timezone = body.timezone || state.timezone;
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/settings/wifi/status') {
    json(res, 200, envelope({ connected: state.wifiConnected, ssid: state.wifiSsid, ip: state.wifiIp, rssi: state.wifiRssi }));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/settings/wifi/networks') {
    json(res, 200, envelope({ networks: [
      { ssid: 'ReflowLab', rssi: -52, auth_mode: 3 },
      { ssid: 'Workshop-2G', rssi: -68, auth_mode: 3 },
      { ssid: 'Guest', rssi: -74, auth_mode: 0 }
    ] }));
    return;
  }

  if (req.method === 'POST' && path === '/api/v1/settings/wifi/connect') {
    const body = JSON.parse(await readBody(req));
    state.wifiConnected = true;
    state.wifiSsid = body.ssid || state.wifiSsid;
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'POST' && path === '/api/v1/settings/wifi/disconnect') {
    state.wifiConnected = false;
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/settings/data') {
    json(res, 200, envelope({
      logging_enabled: state.loggingEnabled,
      log_interval_ms: state.logIntervalMs,
      max_time_ms: state.maxTimeMs,
      points: state.points.length,
      bytes_used: state.points.length * 64,
      max_points: 8000
    }));
    return;
  }

  if (req.method === 'PUT' && path === '/api/v1/settings/data') {
    const body = JSON.parse(await readBody(req));
    state.loggingEnabled = Boolean(body.logging_enabled);
    state.logIntervalMs = Number(body.log_interval_ms || state.logIntervalMs);
    state.maxTimeMs = Number(body.max_time_ms || state.maxTimeMs);
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/controller/config') {
    json(res, 200, envelope({
      pid: { kp: 15, ki: 2, kd: 0, derivative_filter_s: 0 },
      input_filter_ms: 1000,
      inputs: [0],
      relays: { pwm_relays: [0, 1], running_relays: [2] }
    }));
    return;
  }

  if (req.method === 'PUT' && path.startsWith('/api/v1/controller/config/')) {
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/data/history') {
    json(res, 200, envelope({ points: state.points.slice(-200) }));
    return;
  }

  if (req.method === 'DELETE' && path === '/api/v1/data/history') {
    state.points = [];
    json(res, 200, envelope({}));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/data/export.csv') {
    const csv = ['timestamp,setpoint,process_value,pid_output', ...state.points.map((p) => `${p.timestamp},${p.setpoint},${p.process_value},${p.pid_output}`)].join('\n');
    res.writeHead(200, {
      'Content-Type': 'text/csv',
      'Access-Control-Allow-Origin': '*'
    });
    res.end(csv);
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/system/info') {
    json(res, 200, envelope({
      project_name: 'reflow_oven_firmware',
      version: 'mock',
      idf_version: 'mock',
      build_date: new Date().toDateString(),
      build_time: new Date().toTimeString()
    }));
    return;
  }

  if (req.method === 'GET' && path === '/api/v1/profiles') {
    json(res, 200, envelope({ supports_execution: false, profiles: sampleProfiles }));
    return;
  }

  json(res, 404, errEnvelope('NOT_FOUND', 'Endpoint not found'));
});

const wss = new WebSocketServer({ noServer: true });

server.on('upgrade', (request, socket, head) => {
  const parsed = parse(request.url || '', true);
  if (parsed.pathname !== '/ws') {
    socket.destroy();
    return;
  }

  wss.handleUpgrade(request, socket, head, (ws) => {
    wss.emit('connection', ws, request);
  });
});

wss.on('connection', (ws) => {
  ws.send(JSON.stringify({ type: 'hello', data: makeStatusData() }));
});

setInterval(() => {
  const drift = (Math.random() - 0.5) * 1.2;
  const target = state.running ? state.setpoint : 28;
  state.process += (target - state.process) * 0.08 + drift;
  state.pid = Math.max(-100, Math.min(100, (state.setpoint - state.process) * 1.5));
  state.p = state.pid * 0.7;
  state.i = state.pid * 0.2;
  state.d = state.pid * 0.1;

  const sample = {
    timestamp: Date.now(),
    setpoint: state.setpoint,
    process_value: state.process,
    pid_output: state.pid
  };

  state.points.push(sample);
  if (state.points.length > 5000) {
    state.points.shift();
  }

  const payload = JSON.stringify({ type: 'telemetry', data: makeStatusData() });
  for (const client of wss.clients) {
    if (client.readyState === 1) {
      client.send(payload);
    }
  }
}, 250);

server.listen(port, () => {
  console.log(`Mock API/WS server running on http://localhost:${port}`);
});
