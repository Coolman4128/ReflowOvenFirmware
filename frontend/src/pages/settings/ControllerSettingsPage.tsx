import { useEffect, useState } from 'react';
import { api } from '../../api';
import { ControllerConfig } from '../../types';

interface Props {
  onBack: () => void;
}

function csvToNumbers(input: string): number[] {
  return input
    .split(',')
    .map((v) => Number(v.trim()))
    .filter((v) => Number.isFinite(v));
}

function clampWeight(value: number): number {
  if (!Number.isFinite(value)) return 1;
  return Math.min(1, Math.max(0, value));
}

function sanitizeRelays(input: string): number[] {
  return Array.from(new Set(csvToNumbers(input).map((value) => Math.trunc(value))))
    .filter((value) => value >= 0 && value <= 7)
    .sort((a, b) => a - b);
}

export function ControllerSettingsPage({ onBack }: Props) {
  const [config, setConfig] = useState<ControllerConfig | null>(null);
  const [inputsCsv, setInputsCsv] = useState('0');
  const [pwmRelaysCsv, setPwmRelaysCsv] = useState('0,1');
  const [runningRelaysCsv, setRunningRelaysCsv] = useState('2');
  const [relayWeights, setRelayWeights] = useState<Record<number, number>>({});

  const refresh = async () => {
    const value = await api.getControllerConfig();

    const heatingKp = Number.isFinite(value.pid.heating?.kp) ? value.pid.heating.kp : value.pid.kp;
    const heatingKi = Number.isFinite(value.pid.heating?.ki) ? value.pid.heating.ki : value.pid.ki;
    const heatingKd = Number.isFinite(value.pid.heating?.kd) ? value.pid.heating.kd : value.pid.kd;
    const coolingKp = Number.isFinite(value.pid.cooling?.kp) ? value.pid.cooling.kp : heatingKp;
    const coolingKi = Number.isFinite(value.pid.cooling?.ki) ? value.pid.cooling.ki : 0;
    const coolingKd = Number.isFinite(value.pid.cooling?.kd) ? value.pid.cooling.kd : heatingKd;

    const coolOnBand = Number.isFinite(value.cooling?.cool_on_band_c) ? Math.max(0, value.cooling.cool_on_band_c) : 5;
    let coolOffBand = Number.isFinite(value.cooling?.cool_off_band_c) ? Math.max(0, value.cooling.cool_off_band_c) : 2;
    if (coolOffBand >= coolOnBand) {
      coolOffBand = Math.max(0, coolOnBand - 0.1);
    }

    setConfig({
      ...value,
      pid: {
        ...value.pid,
        kp: heatingKp,
        ki: heatingKi,
        kd: heatingKd,
        heating: {
          kp: heatingKp,
          ki: heatingKi,
          kd: heatingKd
        },
        cooling: {
          kp: coolingKp,
          ki: coolingKi,
          kd: coolingKd
        },
        setpoint_weight: Number.isFinite(value.pid.setpoint_weight) ? value.pid.setpoint_weight : 0.5,
        integral_zone_c: Number.isFinite(value.pid.integral_zone_c) ? Math.max(0, value.pid.integral_zone_c) : 0,
        integral_leak_s: Number.isFinite(value.pid.integral_leak_s) ? Math.max(0, value.pid.integral_leak_s) : 0
      },
      cooling: {
        cool_on_band_c: coolOnBand,
        cool_off_band_c: coolOffBand
      }
    });

    setInputsCsv(value.inputs.join(','));
    setPwmRelaysCsv(value.relays.pwm_relays.join(','));
    setRunningRelaysCsv(value.relays.running_relays.join(','));
    const nextWeights: Record<number, number> = {};
    for (let relay = 0; relay < 8; relay += 1) {
      nextWeights[relay] = 1;
    }
    for (const entry of value.relays.pwm_relay_weights ?? []) {
      if (Number.isFinite(entry.relay) && entry.relay >= 0 && entry.relay <= 7) {
        nextWeights[entry.relay] = clampWeight(entry.weight);
      }
    }
    setRelayWeights(nextWeights);
  };

  useEffect(() => {
    refresh().catch(() => undefined);
  }, []);

  const savePid = async () => {
    if (!config) return;
    await api.updatePid(config.pid);
    await refresh();
  };

  const saveCooling = async () => {
    if (!config) return;
    await api.updateCoolingConfig(config.cooling);
    await refresh();
  };

  const saveFilter = async () => {
    if (!config) return;
    await api.updateInputFilter(config.input_filter_ms);
    await refresh();
  };

  const saveInputs = async () => {
    await api.updateInputs(csvToNumbers(inputsCsv));
    await refresh();
  };

  const saveRelays = async () => {
    const pwmRelays = sanitizeRelays(pwmRelaysCsv);
    const runningRelays = sanitizeRelays(runningRelaysCsv);
    const pwmRelayWeights = pwmRelays.map((relay) => ({
      relay,
      weight: clampWeight(relayWeights[relay] ?? 1)
    }));
    await api.updateRelays(pwmRelays, runningRelays, pwmRelayWeights);
    await refresh();
  };

  if (!config) {
    return <div className="card">Loading controller config...</div>;
  }

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <div className="toolbar">
        <h2 className="section-title" style={{ margin: 0 }}>Controller Settings</h2>
        <button onClick={onBack}>Back</button>
      </div>

      <section className="card">
        <h3 className="section-title">PID Modes</h3>
        <div className="grid two">
          <div>
            <label className="label">Heat Kp</label>
            <input
              className="input"
              type="number"
              value={config.pid.heating.kp}
              onChange={(e) => setConfig({
                ...config,
                pid: {
                  ...config.pid,
                  kp: Number(e.target.value),
                  heating: { ...config.pid.heating, kp: Number(e.target.value) }
                }
              })}
            />
          </div>
          <div>
            <label className="label">Heat Ki</label>
            <input
              className="input"
              type="number"
              value={config.pid.heating.ki}
              onChange={(e) => setConfig({
                ...config,
                pid: {
                  ...config.pid,
                  ki: Number(e.target.value),
                  heating: { ...config.pid.heating, ki: Number(e.target.value) }
                }
              })}
            />
          </div>
          <div>
            <label className="label">Heat Kd</label>
            <input
              className="input"
              type="number"
              value={config.pid.heating.kd}
              onChange={(e) => setConfig({
                ...config,
                pid: {
                  ...config.pid,
                  kd: Number(e.target.value),
                  heating: { ...config.pid.heating, kd: Number(e.target.value) }
                }
              })}
            />
          </div>
          <div>
            <label className="label">Cool Kp</label>
            <input
              className="input"
              type="number"
              value={config.pid.cooling.kp}
              onChange={(e) => setConfig({ ...config, pid: { ...config.pid, cooling: { ...config.pid.cooling, kp: Number(e.target.value) } } })}
            />
          </div>
          <div>
            <label className="label">Cool Ki</label>
            <input
              className="input"
              type="number"
              value={config.pid.cooling.ki}
              onChange={(e) => setConfig({ ...config, pid: { ...config.pid, cooling: { ...config.pid.cooling, ki: Number(e.target.value) } } })}
            />
          </div>
          <div>
            <label className="label">Cool Kd</label>
            <input
              className="input"
              type="number"
              value={config.pid.cooling.kd}
              onChange={(e) => setConfig({ ...config, pid: { ...config.pid, cooling: { ...config.pid.cooling, kd: Number(e.target.value) } } })}
            />
          </div>
          <div>
            <label className="label">Derivative Filter (s)</label>
            <input className="input" type="number" value={config.pid.derivative_filter_s} onChange={(e) => setConfig({ ...config, pid: { ...config.pid, derivative_filter_s: Number(e.target.value) } })} />
          </div>
          <div>
            <label className="label">Setpoint Weight (0-1)</label>
            <input
              className="input"
              type="number"
              min="0"
              max="1"
              step="0.01"
              value={config.pid.setpoint_weight}
              onChange={(e) => setConfig({ ...config, pid: { ...config.pid, setpoint_weight: Number(e.target.value) } })}
            />
          </div>
          <div>
            <label className="label">I Zone (C)</label>
            <input
              className="input"
              type="number"
              min="0"
              step="0.1"
              value={config.pid.integral_zone_c}
              onChange={(e) => setConfig({ ...config, pid: { ...config.pid, integral_zone_c: Number(e.target.value) } })}
            />
          </div>
          <div>
            <label className="label">Integrator Leak Time (s, 0=off)</label>
            <input
              className="input"
              type="number"
              min="0"
              step="0.1"
              value={config.pid.integral_leak_s}
              onChange={(e) => setConfig({ ...config, pid: { ...config.pid, integral_leak_s: Number(e.target.value) } })}
            />
          </div>
        </div>
        <button className="primary" style={{ marginTop: '0.75rem' }} onClick={savePid}>Save PID</button>
      </section>

      <section className="card">
        <h3 className="section-title">Cooling Door Hysteresis</h3>
        <div className="grid two">
          <div>
            <label className="label">Cool On Band (C above SP)</label>
            <input
              className="input"
              type="number"
              min="0"
              step="0.1"
              value={config.cooling.cool_on_band_c}
              onChange={(e) => setConfig({ ...config, cooling: { ...config.cooling, cool_on_band_c: Number(e.target.value) } })}
            />
          </div>
          <div>
            <label className="label">Cool Off Band (C above SP)</label>
            <input
              className="input"
              type="number"
              min="0"
              step="0.1"
              value={config.cooling.cool_off_band_c}
              onChange={(e) => setConfig({ ...config, cooling: { ...config.cooling, cool_off_band_c: Number(e.target.value) } })}
            />
          </div>
        </div>
        <div className="muted" style={{ marginTop: '0.5rem' }}>
          Cooling enables when PV {`>`} SP + On Band and disables when PV {`<`} SP + Off Band. Off band must be lower than on band.
        </div>
        <button className="primary" style={{ marginTop: '0.75rem' }} onClick={saveCooling}>Save Cooling Bands</button>
      </section>

      <section className="card">
        <h3 className="section-title">Input Filtering</h3>
        <label className="label">Input Filter (ms)</label>
        <input className="input" type="number" value={config.input_filter_ms} onChange={(e) => setConfig({ ...config, input_filter_ms: Number(e.target.value) })} />
        <button className="primary" style={{ marginTop: '0.75rem' }} onClick={saveFilter}>Save Filter</button>
      </section>

      <section className="card">
        <h3 className="section-title">Input Channels</h3>
        <label className="label">CSV (0-7)</label>
        <input className="input" value={inputsCsv} onChange={(e) => setInputsCsv(e.target.value)} />
        <button className="primary" style={{ marginTop: '0.75rem' }} onClick={saveInputs}>Save Inputs</button>
      </section>

      <section className="card">
        <h3 className="section-title">Relay Grouping</h3>
        <label className="label">PWM Relays CSV (0-7)</label>
        <input className="input" value={pwmRelaysCsv} onChange={(e) => setPwmRelaysCsv(e.target.value)} />
        <div className="grid two" style={{ marginTop: '0.75rem' }}>
          {Array.from({ length: 8 }, (_, relay) => (
            <div key={relay}>
              <label className="label">Relay {relay} PWM Weight (0-1)</label>
              <input
                className="input"
                type="number"
                min="0"
                max="1"
                step="0.01"
                value={relayWeights[relay] ?? 1}
                onChange={(e) => setRelayWeights({ ...relayWeights, [relay]: clampWeight(Number(e.target.value)) })}
              />
            </div>
          ))}
        </div>
        <div className="muted" style={{ marginTop: '0.5rem' }}>
          Relay weights are only applied to relays listed in PWM Relays CSV. A weight of 0 keeps that selected relay always off.
        </div>
        <label className="label" style={{ marginTop: '0.5rem' }}>Running Relays CSV (0-7)</label>
        <input className="input" value={runningRelaysCsv} onChange={(e) => setRunningRelaysCsv(e.target.value)} />
        <button className="primary" style={{ marginTop: '0.75rem' }} onClick={saveRelays}>Save Relays</button>
      </section>
    </div>
  );
}
