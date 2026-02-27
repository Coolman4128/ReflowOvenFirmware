import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis, CartesianGrid, Legend } from 'recharts';
import { api } from '../api';
import { HistoryPoint, StatusData } from '../types';

interface Props {
  status: StatusData | null;
}

interface SeriesDef {
  key: string;
  label: string;
  color: string;
}

const MAX_RENDER_POINTS = 2000;
const SERIES_VISIBILITY_STORAGE_KEY = 'reflow.dataPage.seriesVisibility';

const ANALOG_SERIES: SeriesDef[] = [
  { key: 'processValue', label: 'Process Value', color: '#ef4444' },
  { key: 'setpoint', label: 'Setpoint', color: '#2563eb' },
  { key: 'pidOutput', label: 'PID Output', color: '#16a34a' },
  { key: 'pTerm', label: 'P Term', color: '#f59e0b' },
  { key: 'iTerm', label: 'I Term', color: '#7c3aed' },
  { key: 'dTerm', label: 'D Term', color: '#0f766e' },
  { key: 'temp0', label: 'TC0', color: '#dc2626' },
  { key: 'temp1', label: 'TC1', color: '#ea580c' },
  { key: 'temp2', label: 'TC2', color: '#ca8a04' },
  { key: 'temp3', label: 'TC3', color: '#65a30d' },
  { key: 'servoAngle', label: 'Servo Angle', color: '#0891b2' }
];

const DIGITAL_SERIES: SeriesDef[] = [
  { key: 'relay0', label: 'Relay 0', color: '#be123c' },
  { key: 'relay1', label: 'Relay 1', color: '#1d4ed8' },
  { key: 'relay2', label: 'Relay 2', color: '#15803d' },
  { key: 'relay3', label: 'Relay 3', color: '#a16207' },
  { key: 'relay4', label: 'Relay 4', color: '#7c2d12' },
  { key: 'relay5', label: 'Relay 5', color: '#7e22ce' },
  { key: 'running', label: 'Chamber Running', color: '#111827' }
];

const DEFAULT_VISIBLE: Record<string, boolean> = {
  processValue: true,
  setpoint: true,
  pidOutput: true,
  pTerm: false,
  iTerm: false,
  dTerm: false,
  temp0: false,
  temp1: false,
  temp2: false,
  temp3: false,
  servoAngle: false,
  relay0: false,
  relay1: false,
  relay2: false,
  relay3: false,
  relay4: false,
  relay5: false,
  running: false
};

function loadInitialVisibility(): Record<string, boolean> {
  const next = { ...DEFAULT_VISIBLE };
  try {
    const stored = localStorage.getItem(SERIES_VISIBILITY_STORAGE_KEY);
    if (!stored) {
      return next;
    }

    const parsed = JSON.parse(stored) as Record<string, unknown>;
    for (const key of Object.keys(next)) {
      const value = parsed[key];
      if (typeof value === 'boolean') {
        next[key] = value;
      }
    }
  } catch {
    return next;
  }

  return next;
}

function downsample(points: HistoryPoint[], maxPoints: number): HistoryPoint[] {
  if (points.length <= maxPoints || maxPoints <= 2) {
    return points;
  }

  const sampled: HistoryPoint[] = [];
  sampled.push(points[0]);

  const interiorCount = maxPoints - 2;
  const step = (points.length - 2) / interiorCount;
  for (let i = 0; i < interiorCount; i += 1) {
    const index = 1 + Math.floor(i * step);
    sampled.push(points[index]);
  }

  sampled.push(points[points.length - 1]);
  return sampled;
}

export function DataPage({ status }: Props) {
  const [history, setHistory] = useState<HistoryPoint[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const inFlightRef = useRef(false);
  const pendingReloadRef = useRef(false);

  const [visible, setVisible] = useState<Record<string, boolean>>(() => loadInitialVisibility());

  const loadHistory = useCallback(async () => {
    if (inFlightRef.current) {
      pendingReloadRef.current = true;
      return;
    }

    inFlightRef.current = true;
    setLoading(true);
    setError(null);

    try {
      const data = await api.getHistory();
      setHistory(Array.isArray(data.points) ? data.points : []);
    } catch {
      setHistory([]);
      setError('Failed to load data history.');
    } finally {
      setLoading(false);
      inFlightRef.current = false;
      if (pendingReloadRef.current) {
        pendingReloadRef.current = false;
        void loadHistory();
      }
    }
  }, []);

  useEffect(() => {
    void loadHistory();
  }, [loadHistory]);

  useEffect(() => {
    if (typeof status?.data.points === 'number') {
      void loadHistory();
    }
  }, [status?.data.points, loadHistory]);

  useEffect(() => {
    try {
      localStorage.setItem(SERIES_VISIBILITY_STORAGE_KEY, JSON.stringify(visible));
    } catch {
      // Ignore storage failures and keep in-memory state.
    }
  }, [visible]);

  const sampledHistory = useMemo(() => downsample(history, MAX_RENDER_POINTS), [history]);

  const chartData = useMemo(() => sampledHistory.map((p) => ({
    t: p.timestamp,
    processValue: p.process_value,
    setpoint: p.setpoint,
    pidOutput: p.pid_output,
    pTerm: p.p,
    iTerm: p.i,
    dTerm: p.d,
    temp0: p.temperatures?.[0] ?? null,
    temp1: p.temperatures?.[1] ?? null,
    temp2: p.temperatures?.[2] ?? null,
    temp3: p.temperatures?.[3] ?? null,
    servoAngle: p.servo_angle,
    relay0: (p.relay_states & (1 << 0)) ? 1 : 0,
    relay1: (p.relay_states & (1 << 1)) ? 1 : 0,
    relay2: (p.relay_states & (1 << 2)) ? 1 : 0,
    relay3: (p.relay_states & (1 << 3)) ? 1 : 0,
    relay4: (p.relay_states & (1 << 4)) ? 1 : 0,
    relay5: (p.relay_states & (1 << 5)) ? 1 : 0,
    running: p.running ? 1 : 0
  })), [sampledHistory]);

  const visibleAnalogSeries = useMemo(
    () => ANALOG_SERIES.filter((series) => visible[series.key]),
    [visible]
  );

  const visibleDigitalSeries = useMemo(
    () => DIGITAL_SERIES.filter((series) => visible[series.key]),
    [visible]
  );

  const toggleSeries = (key: string) => {
    setVisible((current) => ({ ...current, [key]: !current[key] }));
    void loadHistory();
  };

  const exportCsv = async () => {
    const csv = await api.exportCsv();
    const blob = new Blob([csv], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'reflow-history.csv';
    a.click();
    URL.revokeObjectURL(url);
  };

  const clearData = async () => {
    await api.clearHistory();
    setHistory([]);
  };

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <div className="toolbar">
        <h2 className="section-title" style={{ margin: 0 }}>Historical Data</h2>
        <div className="row" style={{ flexWrap: 'wrap' }}>
          <button onClick={exportCsv}>Export CSV</button>
          <button onClick={clearData} className="secondary">Clear</button>
        </div>
      </div>

      <section className="card">
        <div className="grid two" style={{ marginBottom: '0.75rem' }}>
          <div>
            <div className="muted" style={{ marginBottom: '0.5rem' }}>Analog Series</div>
            <div className="row" style={{ flexWrap: 'wrap', rowGap: '0.35rem' }}>
              {ANALOG_SERIES.map((series) => (
                <label key={series.key}>
                  <input
                    type="checkbox"
                    checked={!!visible[series.key]}
                    onChange={() => toggleSeries(series.key)}
                  /> {series.label}
                </label>
              ))}
            </div>
          </div>

          <div>
            <div className="muted" style={{ marginBottom: '0.5rem' }}>Digital Series</div>
            <div className="row" style={{ flexWrap: 'wrap', rowGap: '0.35rem' }}>
              {DIGITAL_SERIES.map((series) => (
                <label key={series.key}>
                  <input
                    type="checkbox"
                    checked={!!visible[series.key]}
                    onChange={() => toggleSeries(series.key)}
                  /> {series.label}
                </label>
              ))}
            </div>
          </div>
        </div>

        {error && <div className="warning" style={{ marginBottom: '0.75rem' }}>{error}</div>}
        {loading && <div className="muted" style={{ marginBottom: '0.75rem' }}>Loading history...</div>}

        <div style={{ height: 360, marginBottom: '1rem' }}>
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={chartData}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
              <XAxis dataKey="t" hide />
              <YAxis />
              <Tooltip />
              <Legend />
              {visibleAnalogSeries.map((series) => (
                <Line
                  key={series.key}
                  type="monotone"
                  dataKey={series.key}
                  name={series.label}
                  stroke={series.color}
                  dot={false}
                  isAnimationActive={false}
                />
              ))}
            </LineChart>
          </ResponsiveContainer>
        </div>

        <div style={{ height: 240 }}>
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={chartData}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
              <XAxis dataKey="t" hide />
              <YAxis domain={[0, 1]} ticks={[0, 1]} allowDecimals={false} />
              <Tooltip />
              <Legend />
              {visibleDigitalSeries.map((series) => (
                <Line
                  key={series.key}
                  type="stepAfter"
                  dataKey={series.key}
                  name={series.label}
                  stroke={series.color}
                  dot={false}
                  isAnimationActive={false}
                />
              ))}
            </LineChart>
          </ResponsiveContainer>
        </div>
      </section>

      <div className="grid three">
        <section className="card"><div className="muted">Points Loaded</div><div className="kpi">{history.length}</div></section>
        <section className="card"><div className="muted">Points Graphed</div><div className="kpi">{sampledHistory.length}</div></section>
        <section className="card"><div className="muted">Max Points</div><div className="kpi">{status?.data.max_points ?? 0}</div></section>
      </div>
    </div>
  );
}
