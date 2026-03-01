import { useMemo, useState } from 'react';
import { StatusData } from '../types';

interface Props {
  status: StatusData | null;
  onSetpoint: (value: number) => Promise<void>;
  onToggleDoor: (open: boolean) => Promise<void>;
}

export function DashboardPage({ status, onSetpoint, onToggleDoor }: Props) {
  const degC = '\u00B0C';
  const [setpointInput, setSetpointInput] = useState('');
  const [saving, setSaving] = useState(false);
  const [doorSaving, setDoorSaving] = useState(false);

  const controller = status?.controller;
  const hardware = status?.hardware;
  const profileRunning = !!status?.profile?.running;

  const pidDirection = useMemo(() => {
    if (!controller) return 'idle';
    if (controller.pid_output > 0) return 'heating';
    if (controller.pid_output < 0) return 'venting';
    return 'idle';
  }, [controller]);

  const applySetpoint = async () => {
    const parsed = Number(setpointInput);
    if (!Number.isFinite(parsed)) return;
    setSaving(true);
    try {
      await onSetpoint(parsed);
      setSetpointInput('');
    } finally {
      setSaving(false);
    }
  };

  const toggleDoor = async () => {
    if (!controller) return;
    setDoorSaving(true);
    try {
      await onToggleDoor(!controller.door_open);
    } finally {
      setDoorSaving(false);
    }
  };

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <h2 className="section-title">Dashboard</h2>

      <div className="grid two">
        <section className="card" style={{ background: 'linear-gradient(135deg,#ef4444,#b91c1c)', color: 'white' }}>
          <h3 className="section-title" style={{ color: 'white' }}>Current Temperature</h3>
          <div className="kpi">{controller ? controller.process_value_c.toFixed(1) : '--'}{degC}</div>
          <p style={{ opacity: 0.9 }}>Average chamber process value</p>
        </section>

        <section className="card">
          <h3 className="section-title">Setpoint</h3>
          <div className="kpi">{controller ? controller.setpoint_c.toFixed(1) : '--'}{degC}</div>
          <div className="row">
            <input
              className="input"
              placeholder={`New setpoint ${degC}`}
              value={setpointInput}
              onChange={(e) => setSetpointInput(e.target.value)}
              disabled={profileRunning}
            />
            <button className="primary" onClick={applySetpoint} disabled={saving || profileRunning}>
              {profileRunning ? 'Locked' : (saving ? 'Saving...' : 'Apply')}
            </button>
          </div>
          {profileRunning && <div className="muted" style={{ marginTop: '0.5rem' }}>Setpoint is locked while a profile is running.</div>}
        </section>
      </div>

      <section className="card">
        <h3 className="section-title">PID Output</h3>
        <div className="row" style={{ marginBottom: '0.5rem' }}>
          <strong>{controller ? controller.pid_output.toFixed(1) : '--'}%</strong>
          <span className="muted">mode: {pidDirection}</span>
        </div>
        <div style={{ position: 'relative', height: '24px', borderRadius: '10px', background: '#e5e7eb', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', left: '50%', width: '2px', top: 0, bottom: 0, background: '#111827' }} />
          {controller && controller.pid_output >= 0 && (
            <div
              style={{
                position: 'absolute',
                left: '50%',
                top: 0,
                bottom: 0,
                width: `${controller.pid_output / 2}%`,
                background: 'linear-gradient(90deg,#ef4444,#b91c1c)'
              }}
            />
          )}
          {controller && controller.pid_output < 0 && (
            <div
              style={{
                position: 'absolute',
                right: '50%',
                top: 0,
                bottom: 0,
                width: `${Math.abs(controller.pid_output) / 2}%`,
                background: 'linear-gradient(270deg,#3b82f6,#1d4ed8)'
              }}
            />
          )}
        </div>
      </section>

      <section className="card">
        <h3 className="section-title">Door Control</h3>
        <div className="row" style={{ justifyContent: 'space-between' }}>
          <span className="muted">State: {controller?.door_open ? 'Open' : 'Closed'}</span>
          <button
            className="primary"
            onClick={toggleDoor}
            disabled={!controller || controller.running || doorSaving}
          >
            {doorSaving ? 'Applying...' : (controller?.door_open ? 'Close Door' : 'Open Door')}
          </button>
        </div>
        {controller?.running && (
          <div className="muted" style={{ marginTop: '0.5rem' }}>
            Door control is only available while chamber is stopped.
          </div>
        )}
      </section>

      <section className="card">
        <h3 className="section-title">Sensor Channels</h3>
        <div className="grid two">
          {(hardware?.temperatures_c ?? []).map((temp, idx) => (
            <div key={idx} className="row" style={{ justifyContent: 'space-between' }}>
              <span>TC{idx}</span>
              <strong>{temp.toFixed(1)}{degC}</strong>
            </div>
          ))}
        </div>
      </section>
    </div>
  );
}
