import { useEffect, useMemo, useState } from 'react';
import { LayoutDashboard, FileText, BarChart3, Settings } from 'lucide-react';
import { api } from './api';
import { useLiveStatus } from './ws';
import { DashboardPage } from './pages/DashboardPage';
import { ProfilesPage } from './pages/ProfilesPage';
import { DataPage } from './pages/DataPage';
import { SettingsHomePage } from './pages/SettingsHomePage';
import { TimeSettingsPage } from './pages/settings/TimeSettingsPage';
import { WifiSettingsPage } from './pages/settings/WifiSettingsPage';
import { DataSettingsPage } from './pages/settings/DataSettingsPage';
import { ControllerSettingsPage } from './pages/settings/ControllerSettingsPage';

type Route =
  | 'dashboard'
  | 'profiles'
  | 'data'
  | 'settings'
  | 'settings-time'
  | 'settings-wifi'
  | 'settings-data'
  | 'settings-controller';

const nav = [
  { id: 'dashboard' as const, label: 'Dashboard', icon: LayoutDashboard },
  { id: 'profiles' as const, label: 'Profiles', icon: FileText },
  { id: 'data' as const, label: 'Data', icon: BarChart3 },
  { id: 'settings' as const, label: 'Settings', icon: Settings }
];

function isSettingsRoute(route: Route) {
  return route === 'settings' || route.startsWith('settings-');
}

export default function App() {
  const [route, setRoute] = useState<Route>('dashboard');
  const [localNow, setLocalNow] = useState(Date.now());
  const { status, connected } = useLiveStatus();

  useEffect(() => {
    const timer = window.setInterval(() => setLocalNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);

  const displayTime = useMemo(() => {
    const source = status?.time.unix_time_ms || localNow;
    return new Date(source);
  }, [status?.time.unix_time_ms, localNow]);

  const toggleRun = async () => {
    if (status?.controller.running) {
      await api.stopOven();
    } else {
      await api.startOven();
    }
  };

  const onSetpoint = async (value: number) => {
    await api.setSetpoint(value);
  };

  const renderPage = () => {
    switch (route) {
      case 'dashboard':
        return <DashboardPage status={status} onSetpoint={onSetpoint} />;
      case 'profiles':
        return <ProfilesPage />;
      case 'data':
        return <DataPage status={status} />;
      case 'settings':
        return <SettingsHomePage onOpen={(next) => setRoute(next)} />;
      case 'settings-time':
        return <TimeSettingsPage onBack={() => setRoute('settings')} />;
      case 'settings-wifi':
        return <WifiSettingsPage onBack={() => setRoute('settings')} />;
      case 'settings-data':
        return <DataSettingsPage onBack={() => setRoute('settings')} />;
      case 'settings-controller':
        return <ControllerSettingsPage onBack={() => setRoute('settings')} />;
      default:
        return <DashboardPage status={status} onSetpoint={onSetpoint} />;
    }
  };

  return (
    <div className="app-shell">
      <header className="top-nav">
        <div className="nav-buttons">
          {nav.map((item) => {
            const Icon = item.icon;
            const active = item.id === 'settings' ? isSettingsRoute(route) : route === item.id;
            return (
              <button
                key={item.id}
                className={`nav-btn ${active ? 'active' : ''}`}
                onClick={() => setRoute(item.id)}
              >
                <span className="row"><Icon size={16} /> {item.label}</span>
              </button>
            );
          })}
        </div>

        <div style={{ textAlign: 'right' }}>
          <div style={{ fontSize: '1.45rem', fontWeight: 700 }}>{displayTime.toLocaleTimeString('en-US', { hour12: true })}</div>
          <div className="muted">{displayTime.toLocaleDateString()}</div>
        </div>
      </header>

      <main className="main-content">{renderPage()}</main>

      <footer className="status-bar">
        <div className="row" style={{ justifyContent: 'space-between', flexWrap: 'wrap', width: '100%' }}>
          <div className="row">
            <span className="muted">Oven Status:</span>
            <span className="pill">{status?.controller.state ?? 'Unknown'}</span>
            <span className="muted">WS: {connected ? 'Connected' : 'Reconnecting...'}</span>
          </div>

          <button className={status?.controller.running ? 'secondary' : 'primary'} onClick={toggleRun}>
            {status?.controller.running ? 'Stop Oven' : 'Start Oven'}
          </button>
        </div>
      </footer>
    </div>
  );
}
