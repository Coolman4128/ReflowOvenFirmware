import { useEffect, useMemo, useState } from 'react';
import { api } from './api';
import { StatusData } from './types';

const WS_URL = (import.meta.env.VITE_WS_URL as string | undefined) ?? `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;

interface MessageEnvelope {
  type?: string;
  data?: StatusData;
}

export function useLiveStatus() {
  const [status, setStatus] = useState<StatusData | null>(null);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    let ws: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let attempts = 0;
    let cancelled = false;

    const fetchInitial = async () => {
      try {
        const data = await api.getStatus();
        if (!cancelled) {
          setStatus(data);
        }
      } catch {
        // keep previous state
      }
    };

    const connect = () => {
      if (cancelled) return;

      ws = new WebSocket(WS_URL);
      ws.onopen = () => {
        setConnected(true);
        attempts = 0;
      };
      ws.onclose = () => {
        setConnected(false);
        if (cancelled) return;
        const backoffMs = Math.min(1000 * Math.pow(2, attempts), 10000) + Math.floor(Math.random() * 200);
        attempts += 1;
        reconnectTimer = window.setTimeout(connect, backoffMs);
      };
      ws.onerror = () => {
        ws?.close();
      };
      ws.onmessage = (event) => {
        try {
          const parsed = JSON.parse(event.data) as MessageEnvelope;
          if (parsed.data) {
            setStatus(parsed.data);
          }
        } catch {
          // ignore malformed messages
        }
      };
    };

    fetchInitial();
    connect();

    const pollTimer = window.setInterval(fetchInitial, 5000);

    return () => {
      cancelled = true;
      if (reconnectTimer !== null) {
        window.clearTimeout(reconnectTimer);
      }
      window.clearInterval(pollTimer);
      ws?.close();
    };
  }, []);

  return useMemo(() => ({ status, connected }), [status, connected]);
}
