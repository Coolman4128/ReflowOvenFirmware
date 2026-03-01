import { useEffect, useMemo, useState } from 'react';
import { api } from '../../api';

interface Props {
  onBack: () => void;
  chamberRunning: boolean;
}

function clampAngle(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.min(180, Math.max(0, value));
}

export function DoorCalibrationSettingsPage({ onBack, chamberRunning }: Props) {
  const [closedAngle, setClosedAngle] = useState(50);
  const [openAngle, setOpenAngle] = useState(90);
  const [maxSpeedDegPerSec, setMaxSpeedDegPerSec] = useState(60);
  const [lastEdited, setLastEdited] = useState<'closed' | 'open' | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);

  const refresh = async () => {
    const config = await api.getControllerConfig();
    setClosedAngle(clampAngle(config.door.closed_angle_deg));
    setOpenAngle(clampAngle(config.door.open_angle_deg));
    setMaxSpeedDegPerSec(clampSpeed(config.door.max_speed_deg_per_s));
    setLastEdited(null);
  };

  useEffect(() => {
    refresh().finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    return () => {
      void api.clearDoorPreview().catch(() => undefined);
    };
  }, []);

  useEffect(() => {
    if (!chamberRunning) {
      return;
    }
    void api.clearDoorPreview().catch(() => undefined);
  }, [chamberRunning]);

  const previewTarget = useMemo(() => {
    if (lastEdited === 'closed') return closedAngle;
    if (lastEdited === 'open') return openAngle;
    return null;
  }, [lastEdited, closedAngle, openAngle]);

  useEffect(() => {
    if (chamberRunning || previewTarget === null || loading) {
      return;
    }

    const timer = window.setTimeout(() => {
      void api.previewDoorAngle(clampAngle(previewTarget)).catch(() => undefined);
    }, 150);

    return () => {
      window.clearTimeout(timer);
    };
  }, [chamberRunning, previewTarget, loading]);

  const save = async () => {
    setSaving(true);
    try {
      await api.updateDoorCalibration({
        closed_angle_deg: clampAngle(closedAngle),
        open_angle_deg: clampAngle(openAngle),
        max_speed_deg_per_s: clampSpeed(maxSpeedDegPerSec)
      });
      await api.clearDoorPreview().catch(() => undefined);
      await refresh();
    } finally {
      setSaving(false);
    }
  };

  const back = async () => {
    await api.clearDoorPreview().catch(() => undefined);
    onBack();
  };

  if (loading) {
    return <div className="card">Loading door calibration...</div>;
  }

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <div className="toolbar">
        <h2 className="section-title" style={{ margin: 0 }}>Door Calibration</h2>
        <button onClick={back}>Back</button>
      </div>

      <section className="card">
        <h3 className="section-title">Door Closed Angle</h3>
        <input
          className="input"
          type="number"
          min="0"
          max="180"
          value={closedAngle}
          disabled={chamberRunning}
          onChange={(e) => {
            setClosedAngle(clampAngle(Number(e.target.value)));
            setLastEdited('closed');
          }}
        />
        <input
          type="range"
          min="0"
          max="180"
          value={closedAngle}
          disabled={chamberRunning}
          onChange={(e) => {
            setClosedAngle(clampAngle(Number(e.target.value)));
            setLastEdited('closed');
          }}
          style={{ width: '100%', marginTop: '0.75rem' }}
        />
      </section>

      <section className="card">
        <h3 className="section-title">Door Open Angle</h3>
        <input
          className="input"
          type="number"
          min="0"
          max="180"
          value={openAngle}
          disabled={chamberRunning}
          onChange={(e) => {
            setOpenAngle(clampAngle(Number(e.target.value)));
            setLastEdited('open');
          }}
        />
        <input
          type="range"
          min="0"
          max="180"
          value={openAngle}
          disabled={chamberRunning}
          onChange={(e) => {
            setOpenAngle(clampAngle(Number(e.target.value)));
            setLastEdited('open');
          }}
          style={{ width: '100%', marginTop: '0.75rem' }}
        />
      </section>

      <section className="card">
        <h3 className="section-title">Door Max Speed</h3>
        <input
          className="input"
          type="number"
          min="1"
          max="360"
          value={maxSpeedDegPerSec}
          disabled={chamberRunning}
          onChange={(e) => {
            setMaxSpeedDegPerSec(clampSpeed(Number(e.target.value)));
          }}
        />
        <div className="muted" style={{ marginTop: '0.5rem' }}>
          Degrees per second. Door motion is rate-limited to this speed.
        </div>
      </section>

      <section className="card">
        <div className="row" style={{ justifyContent: 'space-between' }}>
          <span className="muted">Live preview follows the value you are editing.</span>
          <button className="primary" onClick={save} disabled={chamberRunning || saving}>
            {saving ? 'Saving...' : 'Save Calibration'}
          </button>
        </div>
        {chamberRunning && (
          <div className="muted" style={{ marginTop: '0.5rem' }}>
            Stop the chamber to edit calibration and preview servo movement.
          </div>
        )}
      </section>
    </div>
  );
}

function clampSpeed(value: number): number {
  if (!Number.isFinite(value)) return 60;
  return Math.min(360, Math.max(1, value));
}
