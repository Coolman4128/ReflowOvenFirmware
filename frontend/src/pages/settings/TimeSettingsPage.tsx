import { useEffect, useState } from 'react';
import { api } from '../../api';

interface Props {
  onBack: () => void;
}

export function TimeSettingsPage({ onBack }: Props) {
  const [timezone, setTimezone] = useState('EST5EDT,M3.2.0/2,M11.1.0/2');
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    api.getTimeSettings().then((value) => setTimezone(value.timezone)).catch(() => undefined);
  }, []);

  const save = async () => {
    setSaving(true);
    try {
      await api.setTimeSettings(timezone);
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <div className="toolbar">
        <h2 className="section-title" style={{ margin: 0 }}>Time Settings</h2>
        <button onClick={onBack}>Back</button>
      </div>
      <section className="card">
        <label className="label">Timezone (POSIX TZ string)</label>
        <input className="input" value={timezone} onChange={(e) => setTimezone(e.target.value)} />
        <div className="row" style={{ marginTop: '0.8rem' }}>
          <button className="primary" onClick={save} disabled={saving}>{saving ? 'Saving...' : 'Save Timezone'}</button>
        </div>
      </section>
    </div>
  );
}
