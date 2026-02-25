import { Clock3, Wifi, Database, SlidersHorizontal } from 'lucide-react';

interface Props {
  onOpen: (page: 'settings-time' | 'settings-wifi' | 'settings-data' | 'settings-controller') => void;
}

export function SettingsHomePage({ onOpen }: Props) {
  const cards = [
    { id: 'settings-time' as const, label: 'Time', icon: Clock3 },
    { id: 'settings-wifi' as const, label: 'WiFi', icon: Wifi },
    { id: 'settings-data' as const, label: 'Data', icon: Database },
    { id: 'settings-controller' as const, label: 'Controller', icon: SlidersHorizontal }
  ];

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <h2 className="section-title">Settings</h2>
      <div className="grid two">
        {cards.map((card) => {
          const Icon = card.icon;
          return (
            <button key={card.id} className="card row" onClick={() => onOpen(card.id)} style={{ justifyContent: 'center', minHeight: 120 }}>
              <Icon color="#b91c1c" />
              <strong>{card.label}</strong>
            </button>
          );
        })}
      </div>
    </div>
  );
}
