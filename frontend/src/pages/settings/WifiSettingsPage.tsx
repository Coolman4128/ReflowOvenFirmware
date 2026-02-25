import { useEffect, useState } from 'react';
import { api } from '../../api';

interface Props {
  onBack: () => void;
}

export function WifiSettingsPage({ onBack }: Props) {
  const [ssid, setSsid] = useState('');
  const [password, setPassword] = useState('');
  const [status, setStatus] = useState<{ connected: boolean; ssid: string; ip: string; rssi: number } | null>(null);
  const [networks, setNetworks] = useState<Array<{ ssid: string; rssi: number }>>([]);

  const refresh = async () => {
    const wifi = await api.getWifiStatus();
    setStatus(wifi);
  };

  const scan = async () => {
    const result = await api.scanWifi();
    setNetworks(result.networks.map((n) => ({ ssid: n.ssid, rssi: n.rssi })));
  };

  const connect = async () => {
    await api.connectWifi(ssid, password);
    await refresh();
  };

  const disconnect = async () => {
    await api.disconnectWifi();
    await refresh();
  };

  useEffect(() => {
    refresh().catch(() => undefined);
  }, []);

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <div className="toolbar">
        <h2 className="section-title" style={{ margin: 0 }}>WiFi Settings</h2>
        <button onClick={onBack}>Back</button>
      </div>

      <section className="card">
        <div><strong>Status:</strong> {status?.connected ? 'Connected' : 'Disconnected'}</div>
        <div className="muted">SSID: {status?.ssid || 'N/A'} | IP: {status?.ip || 'N/A'} | RSSI: {status?.rssi ?? '--'}</div>
      </section>

      <section className="card">
        <label className="label">SSID</label>
        <input className="input" value={ssid} onChange={(e) => setSsid(e.target.value)} />
        <label className="label" style={{ marginTop: '0.5rem' }}>Password</label>
        <input className="input" type="password" value={password} onChange={(e) => setPassword(e.target.value)} />
        <div className="row" style={{ marginTop: '0.75rem' }}>
          <button className="primary" onClick={connect}>Connect</button>
          <button className="secondary" onClick={disconnect}>Disconnect</button>
          <button onClick={scan}>Scan</button>
        </div>
      </section>

      <section className="card">
        <h3 className="section-title">Nearby Networks</h3>
        {networks.map((network) => (
          <div key={network.ssid} className="row" style={{ justifyContent: 'space-between', padding: '0.3rem 0' }}>
            <span>{network.ssid}</span>
            <span className="muted">{network.rssi} dBm</span>
          </div>
        ))}
      </section>
    </div>
  );
}
