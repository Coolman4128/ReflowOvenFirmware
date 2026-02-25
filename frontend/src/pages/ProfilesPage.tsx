import { useEffect, useState } from 'react';
import { Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis, CartesianGrid } from 'recharts';
import { api } from '../api';

interface Profile {
  id: string;
  name: string;
  description: string;
}

const sampleCurve = [
  { t: 0, temp: 25 },
  { t: 60, temp: 150 },
  { t: 120, temp: 180 },
  { t: 180, temp: 220 },
  { t: 210, temp: 245 },
  { t: 260, temp: 190 },
  { t: 320, temp: 40 }
];

export function ProfilesPage() {
  const [profiles, setProfiles] = useState<Profile[]>([]);

  useEffect(() => {
    api.getProfiles().then((data) => setProfiles(data.profiles)).catch(() => setProfiles([]));
  }, []);

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <h2 className="section-title">Profiles</h2>
      <div className="warning">Profile execution is not implemented yet. This page is scaffold/sample-only.</div>

      <div className="grid two">
        <section className="card">
          <h3 className="section-title">Available Profiles</h3>
          {profiles.map((profile) => (
            <article key={profile.id} className="card" style={{ marginBottom: '0.75rem', border: '1px solid #fecaca' }}>
              <strong>{profile.name}</strong>
              <p className="muted">{profile.description}</p>
            </article>
          ))}
          <button className="secondary" disabled style={{ width: '100%' }}>
            Start Profile (Disabled)
          </button>
        </section>

        <section className="card">
          <h3 className="section-title">Sample Temperature Curve</h3>
          <div style={{ height: 280 }}>
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={sampleCurve}>
                <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
                <XAxis dataKey="t" />
                <YAxis />
                <Tooltip />
                <Line type="monotone" dataKey="temp" stroke="#ef4444" strokeWidth={3} dot={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </section>
      </div>
    </div>
  );
}
