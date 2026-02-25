import { useEffect, useMemo, useState } from 'react';
import { Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis, CartesianGrid, Legend } from 'recharts';
import { api } from '../api';
import { HistoryPoint, StatusData } from '../types';

interface Props {
  status: StatusData | null;
}

export function DataPage({ status }: Props) {
  const [history, setHistory] = useState<HistoryPoint[]>([]);
  const [showTemp, setShowTemp] = useState(true);
  const [showSetpoint, setShowSetpoint] = useState(true);
  const [showPid, setShowPid] = useState(true);

  useEffect(() => {
    api.getHistory(1000).then((data) => setHistory(data.points)).catch(() => setHistory([]));
  }, []);

  const chartData = useMemo(() => history.map((p) => ({
    t: p.timestamp,
    temperature: p.process_value,
    setpoint: p.setpoint,
    pid: p.pid_output
  })), [history]);

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
        <div className="row">
          <button onClick={exportCsv}>Export CSV</button>
          <button onClick={clearData} className="secondary">Clear</button>
        </div>
      </div>

      <section className="card">
        <div className="row" style={{ marginBottom: '0.75rem', flexWrap: 'wrap' }}>
          <label><input type="checkbox" checked={showTemp} onChange={() => setShowTemp(!showTemp)} /> Temperature</label>
          <label><input type="checkbox" checked={showSetpoint} onChange={() => setShowSetpoint(!showSetpoint)} /> Setpoint</label>
          <label><input type="checkbox" checked={showPid} onChange={() => setShowPid(!showPid)} /> PID Output</label>
        </div>
        <div style={{ height: 360 }}>
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={chartData}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
              <XAxis dataKey="t" hide />
              <YAxis />
              <Tooltip />
              <Legend />
              {showTemp && <Line type="monotone" dataKey="temperature" stroke="#ef4444" dot={false} />}
              {showSetpoint && <Line type="monotone" dataKey="setpoint" stroke="#3b82f6" dot={false} />}
              {showPid && <Line type="monotone" dataKey="pid" stroke="#10b981" dot={false} />}
            </LineChart>
          </ResponsiveContainer>
        </div>
      </section>

      <div className="grid three">
        <section className="card"><div className="muted">Points</div><div className="kpi">{status?.data.points ?? history.length}</div></section>
        <section className="card"><div className="muted">Bytes Used</div><div className="kpi">{status?.data.bytes_used ?? 0}</div></section>
        <section className="card"><div className="muted">Max Points</div><div className="kpi">{status?.data.max_points ?? 0}</div></section>
      </div>
    </div>
  );
}
