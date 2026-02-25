import { useEffect, useState } from 'react';
import { api } from '../../api';

interface Props {
  onBack: () => void;
}

export function DataSettingsPage({ onBack }: Props) {
  const [loggingEnabled, setLoggingEnabled] = useState(true);
  const [interval, setIntervalMs] = useState(1000);
  const [maxTime, setMaxTime] = useState(1800000);
  const [stats, setStats] = useState<{ points: number; bytes_used: number; max_points: number } | null>(null);

  const refresh = async () => {
    const data = await api.getDataSettings();
    setLoggingEnabled(data.logging_enabled);
    setIntervalMs(data.log_interval_ms);
    setMaxTime(data.max_time_ms);
    setStats({ points: data.points, bytes_used: data.bytes_used, max_points: data.max_points });
  };

  useEffect(() => {
    refresh().catch(() => undefined);
  }, []);

  const save = async () => {
    await api.setDataSettings({ logging_enabled: loggingEnabled, log_interval_ms: interval, max_time_ms: maxTime });
    await refresh();
  };

  const clear = async () => {
    await api.clearHistory();
    await refresh();
  };

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <div className="toolbar">
        <h2 className="section-title" style={{ margin: 0 }}>Data Settings</h2>
        <button onClick={onBack}>Back</button>
      </div>

      <section className="card">
        <label className="row"><input type="checkbox" checked={loggingEnabled} onChange={(e) => setLoggingEnabled(e.target.checked)} /> Enable Logging</label>
        <label className="label" style={{ marginTop: '0.6rem' }}>Log Interval (ms)</label>
        <input className="input" type="number" value={interval} onChange={(e) => setIntervalMs(Number(e.target.value))} />

        <label className="label" style={{ marginTop: '0.6rem' }}>Max Time Saved (ms)</label>
        <input className="input" type="number" value={maxTime} onChange={(e) => setMaxTime(Number(e.target.value))} />

        <div className="row" style={{ marginTop: '0.8rem' }}>
          <button className="primary" onClick={save}>Save</button>
          <button onClick={clear}>Clear History</button>
        </div>
      </section>

      <section className="card">
        <div><strong>Points:</strong> {stats?.points ?? 0}</div>
        <div><strong>Bytes Used:</strong> {stats?.bytes_used ?? 0}</div>
        <div><strong>Max Points:</strong> {stats?.max_points ?? 0}</div>
      </section>
    </div>
  );
}
