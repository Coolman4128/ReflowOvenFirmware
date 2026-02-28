import { useEffect, useMemo, useRef, useState } from 'react';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ReferenceArea,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis
} from 'recharts';
import { api } from '../api';
import { ProfileDefinition, ProfileSlotSummary, ProfileStep, StatusData } from '../types';

interface Props {
  status: StatusData | null;
}

interface PreviewPoint {
  t: number;
  target: number;
}

interface StepWindow {
  stepNumber: number;
  start: number;
  end: number;
}

interface PvOverlayPoint {
  t: number;
  pv: number;
}

const DEFAULT_PROFILE: ProfileDefinition = {
  schema_version: 1,
  name: 'New Profile',
  description: '',
  steps: [{ type: 'direct', setpoint_c: 25 }]
};

const STEP_TYPES: Array<ProfileStep['type']> = ['direct', 'wait', 'soak', 'ramp_time', 'ramp_rate', 'jump'];

function newStep(type: ProfileStep['type']): ProfileStep {
  switch (type) {
    case 'direct':
      return { type: 'direct', setpoint_c: 25 };
    case 'wait':
      return { type: 'wait', wait_time_s: 10 };
    case 'soak':
      return { type: 'soak', setpoint_c: 150, soak_time_s: 30, guaranteed: false };
    case 'ramp_time':
      return { type: 'ramp_time', setpoint_c: 200, ramp_time_s: 60 };
    case 'ramp_rate':
      return { type: 'ramp_rate', setpoint_c: 220, ramp_rate_c_per_s: 1 };
    case 'jump':
      return { type: 'jump', target_step_number: 1, repeat_count: 1 };
  }
}

function formatSeconds(value: number) {
  if (!Number.isFinite(value) || value < 0) return '0.0s';
  if (value < 60) return `${value.toFixed(1)}s`;
  const m = Math.floor(value / 60);
  const s = value - (m * 60);
  return `${m}m ${s.toFixed(1)}s`;
}

function validateProfile(profile: ProfileDefinition) {
  const issues: string[] = [];

  if (!profile.name.trim()) issues.push('Profile name is required.');
  if (!Array.isArray(profile.steps) || profile.steps.length === 0) issues.push('At least one step is required.');
  if (profile.steps.length > 40) issues.push('Profiles support up to 40 steps.');

  profile.steps.forEach((step, idx) => {
    const n = idx + 1;
    switch (step.type) {
      case 'direct':
        if (!Number.isFinite(step.setpoint_c)) issues.push(`Step ${n}: direct setpoint is required.`);
        break;
      case 'wait': {
        const hasTime = Number.isFinite(step.wait_time_s ?? NaN);
        const hasPv = Number.isFinite(step.pv_target_c ?? NaN);
        if (!hasTime && !hasPv) issues.push(`Step ${n}: wait requires time and/or PV target.`);
        if (hasTime && (step.wait_time_s ?? 0) <= 0) issues.push(`Step ${n}: wait_time_s must be > 0.`);
        break;
      }
      case 'soak':
        if (!Number.isFinite(step.setpoint_c)) issues.push(`Step ${n}: soak setpoint is required.`);
        if (!Number.isFinite(step.soak_time_s) || step.soak_time_s <= 0) issues.push(`Step ${n}: soak_time_s must be > 0.`);
        if (step.guaranteed && (!Number.isFinite(step.deviation_c ?? NaN) || (step.deviation_c ?? 0) <= 0)) {
          issues.push(`Step ${n}: guaranteed soak requires deviation_c > 0.`);
        }
        break;
      case 'ramp_time':
        if (!Number.isFinite(step.setpoint_c)) issues.push(`Step ${n}: ramp setpoint is required.`);
        if (!Number.isFinite(step.ramp_time_s) || step.ramp_time_s <= 0) issues.push(`Step ${n}: ramp_time_s must be > 0.`);
        break;
      case 'ramp_rate':
        if (!Number.isFinite(step.setpoint_c)) issues.push(`Step ${n}: ramp setpoint is required.`);
        if (!Number.isFinite(step.ramp_rate_c_per_s) || step.ramp_rate_c_per_s <= 0) issues.push(`Step ${n}: ramp_rate_c_per_s must be > 0.`);
        break;
      case 'jump':
        if (!Number.isFinite(step.target_step_number) || step.target_step_number < 1 || step.target_step_number > profile.steps.length) {
          issues.push(`Step ${n}: jump target must be a valid step number.`);
        }
        if ((step.target_step_number ?? 1) >= n) issues.push(`Step ${n}: jump target must point backward.`);
        if (!Number.isFinite(step.repeat_count) || step.repeat_count < 0) issues.push(`Step ${n}: repeat_count must be >= 0.`);
        break;
    }
  });

  return issues;
}

function buildPreview(profile: ProfileDefinition) {
  const points: PreviewPoint[] = [];
  const stepWindows: StepWindow[] = [];

  if (!profile.steps.length) {
    return { points, stepWindows };
  }

  let t = 0;
  let setpoint = 0;
  let pc = 0;
  let transitions = 0;
  const maxTransitions = 800;
  const jumpRemaining = new Map<number, number>();

  profile.steps.forEach((step, idx) => {
    if (step.type === 'jump') jumpRemaining.set(idx, Math.max(0, Math.floor(step.repeat_count)));
  });

  const resetJumpCounters = (start: number, endExclusive: number) => {
    const s = Math.max(0, start);
    const e = Math.min(profile.steps.length, endExclusive);
    for (let i = s; i < e; i += 1) {
      const step = profile.steps[i];
      if (step.type === 'jump') jumpRemaining.set(i, Math.max(0, Math.floor(step.repeat_count)));
    }
  };

  points.push({ t: 0, target: setpoint });

  while (pc >= 0 && pc < profile.steps.length && transitions < maxTransitions) {
    transitions += 1;
    const step = profile.steps[pc];
    const stepStart = t;

    switch (step.type) {
      case 'direct':
        setpoint = step.setpoint_c;
        points.push({ t, target: setpoint });
        t += 0.001;
        stepWindows.push({ stepNumber: pc + 1, start: stepStart, end: t });
        pc += 1;
        break;

      case 'wait': {
        const duration = Math.max(0, step.wait_time_s ?? 0);
        points.push({ t, target: setpoint });
        t += duration;
        points.push({ t, target: setpoint });
        stepWindows.push({ stepNumber: pc + 1, start: stepStart, end: t });
        pc += 1;
        break;
      }

      case 'soak':
        setpoint = step.setpoint_c;
        points.push({ t, target: setpoint });
        t += Math.max(0, step.soak_time_s);
        points.push({ t, target: setpoint });
        stepWindows.push({ stepNumber: pc + 1, start: stepStart, end: t });
        pc += 1;
        break;

      case 'ramp_time': {
        const target = step.setpoint_c;
        const duration = Math.max(0.001, step.ramp_time_s);
        const startSp = setpoint;
        const samples = Math.max(2, Math.min(20, Math.ceil(duration / 5)));
        for (let i = 1; i <= samples; i += 1) {
          const p = i / samples;
          points.push({ t: t + duration * p, target: startSp + ((target - startSp) * p) });
        }
        t += duration;
        setpoint = target;
        stepWindows.push({ stepNumber: pc + 1, start: stepStart, end: t });
        pc += 1;
        break;
      }

      case 'ramp_rate': {
        const target = step.setpoint_c;
        const rate = Math.max(0.001, step.ramp_rate_c_per_s);
        const duration = Math.max(0.001, Math.abs(target - setpoint) / rate);
        const startSp = setpoint;
        const samples = Math.max(2, Math.min(20, Math.ceil(duration / 5)));
        for (let i = 1; i <= samples; i += 1) {
          const p = i / samples;
          points.push({ t: t + duration * p, target: startSp + ((target - startSp) * p) });
        }
        t += duration;
        setpoint = target;
        stepWindows.push({ stepNumber: pc + 1, start: stepStart, end: t });
        pc += 1;
        break;
      }

      case 'jump': {
        const remaining = jumpRemaining.get(pc) ?? Math.max(0, Math.floor(step.repeat_count));
        const jumpTarget = Math.max(0, step.target_step_number - 1);
        if (remaining > 0) {
          jumpRemaining.set(pc, remaining - 1);
          resetJumpCounters(jumpTarget, pc);
          stepWindows.push({ stepNumber: pc + 1, start: stepStart, end: t + 0.001 });
          t += 0.001;
          pc = jumpTarget;
        } else {
          jumpRemaining.set(pc, Math.max(0, Math.floor(step.repeat_count)));
          stepWindows.push({ stepNumber: pc + 1, start: stepStart, end: t + 0.001 });
          t += 0.001;
          pc += 1;
        }
        break;
      }
    }
  }

  return { points: points.sort((a, b) => a.t - b.t), stepWindows };
}

function interpolateTargetAtTime(points: PreviewPoint[], timeS: number) {
  if (points.length === 0) {
    return undefined;
  }

  if (timeS <= points[0].t) {
    return points[0].target;
  }

  for (let i = 1; i < points.length; i += 1) {
    const prev = points[i - 1];
    const curr = points[i];
    if (timeS > curr.t) {
      continue;
    }

    const span = curr.t - prev.t;
    if (span <= 0) {
      return curr.target;
    }

    const progress = (timeS - prev.t) / span;
    return prev.target + ((curr.target - prev.target) * progress);
  }

  return points[points.length - 1].target;
}

export function ProfilesPage({ status }: Props) {
  const [profile, setProfile] = useState<ProfileDefinition>(DEFAULT_PROFILE);
  const [slots, setSlots] = useState<ProfileSlotSummary[]>([]);
  const [limits, setLimits] = useState({ max_slots: 5, max_steps: 40 });
  const [uploadedSummary, setUploadedSummary] = useState<{ present: boolean; name?: string; step_count?: number }>({ present: false });
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [runSource, setRunSource] = useState<'uploaded' | 'slot'>('uploaded');
  const [runSlotIndex, setRunSlotIndex] = useState(0);
  const [saveMode, setSaveMode] = useState<'editor' | 'uploaded'>('editor');

  const importInputRef = useRef<HTMLInputElement | null>(null);
  const [pvOverlay, setPvOverlay] = useState<PvOverlayPoint[]>([]);
  const prevRunningRef = useRef(false);
  const waitCompressionRef = useRef({
    totalOverflowS: 0,
    activeStepKey: '',
    activeStepType: 'none' as StatusData['profile']['current_step_type'],
    activeWaitOverflowS: 0
  });
  const issues = useMemo(() => validateProfile(profile), [profile]);
  const preview = useMemo(() => buildPreview(profile), [profile]);

  const refreshIndex = async () => {
    setLoading(true);
    try {
      const data = await api.getProfilesIndex();
      setSlots(Array.isArray(data.slots) ? data.slots : []);
      setUploadedSummary(data.uploaded ?? { present: false });
      setLimits(data.limits ?? { max_slots: 5, max_steps: 40 });
      if (data.slots?.length && runSlotIndex >= data.slots.length) {
        setRunSlotIndex(0);
      }
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void refreshIndex();
  }, []);

  useEffect(() => {
    const running = !!status?.profile?.running;
    if (running && !prevRunningRef.current) {
      setPvOverlay([]);
      waitCompressionRef.current = {
        totalOverflowS: 0,
        activeStepKey: '',
        activeStepType: 'none',
        activeWaitOverflowS: 0
      };
    }

    if (running && Number.isFinite(status?.profile?.profile_elapsed_s) && Number.isFinite(status?.controller?.process_value_c)) {
      const rawElapsedS = Math.max(0, status?.profile?.profile_elapsed_s ?? 0);
      const stepElapsedS = Math.max(0, status?.profile?.step_elapsed_s ?? 0);
      const stepNumber = status?.profile?.current_step_number ?? 0;
      const stepType = status?.profile?.current_step_type ?? 'none';
      const stepStartRawS = Math.max(0, rawElapsedS - stepElapsedS);
      const stepKey = `${stepNumber}:${Math.round(stepStartRawS * 1000)}:${stepType}`;

      const compression = waitCompressionRef.current;
      if (stepKey !== compression.activeStepKey) {
        if (compression.activeStepType === 'wait') {
          compression.totalOverflowS += compression.activeWaitOverflowS;
        }
        compression.activeStepKey = stepKey;
        compression.activeStepType = stepType;
        compression.activeWaitOverflowS = 0;
      }

      let activeOverflowS = 0;
      if (stepType === 'wait') {
        const stepDef = profile.steps[stepNumber - 1];
        const expectedWaitS = (stepDef?.type === 'wait') ? Math.max(0, stepDef.wait_time_s ?? 0) : 0;
        activeOverflowS = Math.max(0, stepElapsedS - expectedWaitS);
        compression.activeWaitOverflowS = Math.max(compression.activeWaitOverflowS, activeOverflowS);
      }

      const t = Math.max(0, rawElapsedS - compression.totalOverflowS - activeOverflowS);
      const pv = status?.controller?.process_value_c ?? 0;
      setPvOverlay((prev) => {
        if (prev.length > 0 && Math.abs(prev[prev.length - 1].t - t) < 0.05) {
          return prev;
        }
        return [...prev.slice(-1199), { t, pv }];
      });
    }

    prevRunningRef.current = running;
  }, [profile.steps, status?.controller?.process_value_c, status?.profile]);
  const selectedRunSlot = slots.find((s) => s.slot_index === runSlotIndex);
  const canRun = !busy
    && !status?.profile?.running
    && (runSource === 'uploaded' ? uploadedSummary.present : !!selectedRunSlot?.occupied);

  const activeWindow = useMemo(() => {
    const elapsed = status?.profile?.profile_elapsed_s ?? -1;
    if (!status?.profile?.running || elapsed < 0) return null;

    const byElapsed = preview.stepWindows.find((w) => elapsed >= w.start && elapsed <= w.end);
    if (byElapsed) return byElapsed;

    const stepNumber = status?.profile?.current_step_number ?? 0;
    return preview.stepWindows.find((w) => w.stepNumber === stepNumber) ?? null;
  }, [preview.stepWindows, status?.profile?.current_step_number, status?.profile?.profile_elapsed_s, status?.profile?.running]);

  const chartData = useMemo(() => {
    const bucketScale = 1000; // 1 ms buckets
    const toBucket = (timeS: number) => Math.round(timeS * bucketScale);
    const fromBucket = (bucket: number) => bucket / bucketScale;

    const buckets = new Set<number>();
    const pvByBucket = new Map<number, number>();

    for (const point of preview.points) {
      buckets.add(toBucket(point.t));
    }

    for (const point of pvOverlay) {
      const bucket = toBucket(point.t);
      buckets.add(bucket);
      pvByBucket.set(bucket, point.pv);
    }

    const sortedBuckets = Array.from(buckets).sort((a, b) => a - b);
    return sortedBuckets.map((bucket) => {
      const t = fromBucket(bucket);
      return {
        t,
        target: interpolateTargetAtTime(preview.points, t),
        pv: pvByBucket.get(bucket)
      };
    });
  }, [preview.points, pvOverlay]);

  const updateStep = (index: number, updater: (prev: ProfileStep) => ProfileStep) => {
    setProfile((prev) => ({
      ...prev,
      steps: prev.steps.map((step, i) => (i === index ? updater(step) : step))
    }));
  };

  const setStepType = (index: number, type: ProfileStep['type']) => {
    updateStep(index, () => newStep(type));
  };

  const moveStep = (index: number, direction: -1 | 1) => {
    const target = index + direction;
    setProfile((prev) => {
      if (target < 0 || target >= prev.steps.length) return prev;
      const next = [...prev.steps];
      const [item] = next.splice(index, 1);
      next.splice(target, 0, item);
      return { ...prev, steps: next };
    });
  };

  const removeStep = (index: number) => {
    setProfile((prev) => {
      if (prev.steps.length <= 1) return prev;
      return { ...prev, steps: prev.steps.filter((_, i) => i !== index) };
    });
  };

  const addStep = (type: ProfileStep['type']) => {
    setProfile((prev) => {
      if (prev.steps.length >= limits.max_steps) return prev;
      return { ...prev, steps: [...prev.steps, newStep(type)] };
    });
  };

  const importProfile = async (file: File) => {
    try {
      const text = await file.text();
      const parsed = JSON.parse(text) as ProfileDefinition;
      if (!parsed || typeof parsed !== 'object' || !Array.isArray(parsed.steps)) {
        throw new Error('Invalid profile JSON format');
      }
      setProfile({
        schema_version: Number(parsed.schema_version ?? 1),
        name: String(parsed.name ?? ''),
        description: String(parsed.description ?? ''),
        steps: parsed.steps
      });
      setMessage(`Imported profile: ${parsed.name ?? 'Unnamed'}`);
      setError(null);
    } catch (e) {
      setError(`Import failed: ${(e as Error).message}`);
    }
  };

  const exportProfile = () => {
    const json = JSON.stringify(profile, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${profile.name?.trim() || 'profile'}.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const uploadEditorToBoard = async () => {
    if (issues.length > 0) {
      setError('Fix validation errors before uploading.');
      return;
    }

    setBusy(true);
    try {
      await api.setUploadedProfile(profile);
      setMessage('Uploaded profile to board memory.');
      setError(null);
      await refreshIndex();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const loadUploadedToEditor = async () => {
    setBusy(true);
    try {
      const uploaded = await api.getUploadedProfile();
      setProfile(uploaded);
      setMessage('Loaded uploaded profile into editor.');
      setError(null);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const clearUploaded = async () => {
    setBusy(true);
    try {
      await api.clearUploadedProfile();
      setMessage('Cleared uploaded profile from board RAM.');
      setError(null);
      await refreshIndex();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const loadSlot = async (slotIndex: number) => {
    setBusy(true);
    try {
      const slotProfile = await api.getSlotProfile(slotIndex);
      setProfile(slotProfile);
      setMessage(`Loaded slot ${slotIndex + 1} into editor.`);
      setError(null);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const saveToSlot = async (slotIndex: number) => {
    setBusy(true);
    try {
      let profileToSave = profile;
      if (saveMode === 'uploaded') {
        profileToSave = await api.getUploadedProfile();
      } else if (issues.length > 0) {
        setError('Fix validation errors before saving editor profile.');
        setBusy(false);
        return;
      }

      await api.saveSlotProfile(slotIndex, profileToSave);
      setMessage(`Saved ${saveMode} profile to slot ${slotIndex + 1}.`);
      setError(null);
      await refreshIndex();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const deleteSlot = async (slotIndex: number) => {
    setBusy(true);
    try {
      await api.deleteSlotProfile(slotIndex);
      setMessage(`Deleted slot ${slotIndex + 1}.`);
      setError(null);
      await refreshIndex();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const runProfile = async () => {
    setBusy(true);
    try {
      if (runSource === 'uploaded') {
        await api.runProfile({ source: 'uploaded' });
      } else {
        await api.runProfile({ source: 'slot', slot_index: runSlotIndex });
      }
      setMessage('Profile run started.');
      setError(null);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="grid" style={{ gap: '1rem' }}>
      <div className="toolbar">
        <h2 className="section-title" style={{ margin: 0 }}>Profiles</h2>
        <div className="row" style={{ flexWrap: 'wrap' }}>
          <button onClick={() => importInputRef.current?.click()}>Import JSON</button>
          <button onClick={exportProfile}>Export JSON</button>
          <button onClick={uploadEditorToBoard} disabled={busy}>Upload to Board (RAM)</button>
        </div>
      </div>

      <input
        ref={importInputRef}
        type="file"
        accept="application/json,.json"
        style={{ display: 'none' }}
        onChange={(e) => {
          const file = e.target.files?.[0];
          if (file) void importProfile(file);
          e.currentTarget.value = '';
        }}
      />

      {message && <div className="card" style={{ borderColor: '#bbf7d0', color: '#166534' }}>{message}</div>}
      {error && <div className="warning">{error}</div>}
      {loading && <div className="muted">Loading profile metadata...</div>}

      <div className="grid two">
        <section className="card">
          <h3 className="section-title">Profile Editor</h3>
          <label className="label">Name</label>
          <input
            className="input"
            value={profile.name}
            onChange={(e) => setProfile((prev) => ({ ...prev, name: e.target.value }))}
          />

          <label className="label" style={{ marginTop: '0.75rem' }}>Description</label>
          <input
            className="input"
            value={profile.description}
            onChange={(e) => setProfile((prev) => ({ ...prev, description: e.target.value }))}
          />

          <div className="row" style={{ marginTop: '0.75rem', flexWrap: 'wrap' }}>
            <span className="muted">Add step:</span>
            {STEP_TYPES.map((type) => (
              <button key={type} className="secondary" onClick={() => addStep(type)} disabled={profile.steps.length >= limits.max_steps}>
                {type}
              </button>
            ))}
          </div>

          <div className="grid" style={{ marginTop: '1rem' }}>
            {profile.steps.map((step, idx) => (
              <article key={idx} className="card" style={{ border: '1px solid #fca5a5' }}>
                <div className="toolbar" style={{ marginBottom: '0.5rem' }}>
                  <strong>Step {idx + 1}</strong>
                  <div className="row">
                    <button className="secondary" onClick={() => moveStep(idx, -1)} disabled={idx === 0}>↑</button>
                    <button className="secondary" onClick={() => moveStep(idx, 1)} disabled={idx === profile.steps.length - 1}>↓</button>
                    <button className="secondary" onClick={() => removeStep(idx)} disabled={profile.steps.length <= 1}>Delete</button>
                  </div>
                </div>

                <label className="label">Type</label>
                <select className="select" value={step.type} onChange={(e) => setStepType(idx, e.target.value as ProfileStep['type'])}>
                  {STEP_TYPES.map((type) => (
                    <option key={type} value={type}>{type}</option>
                  ))}
                </select>

                {step.type === 'direct' && (
                  <>
                    <label className="label" style={{ marginTop: '0.5rem' }}>Setpoint (C)</label>
                    <input
                      className="input"
                      type="number"
                      value={step.setpoint_c}
                      onChange={(e) => updateStep(idx, () => ({ type: 'direct', setpoint_c: Number(e.target.value) }))}
                    />
                  </>
                )}

                {step.type === 'wait' && (
                  <div className="grid two" style={{ marginTop: '0.5rem' }}>
                    <div>
                      <label className="label">Wait Time (s, optional)</label>
                      <input
                        className="input"
                        type="number"
                        value={step.wait_time_s ?? ''}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'wait', wait_time_s: e.target.value === '' ? undefined : Number(e.target.value) }))}
                      />
                    </div>
                    <div>
                      <label className="label">PV Target (C, optional, ±1 C latch)</label>
                      <input
                        className="input"
                        type="number"
                        value={step.pv_target_c ?? ''}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'wait', pv_target_c: e.target.value === '' ? undefined : Number(e.target.value) }))}
                      />
                    </div>
                  </div>
                )}

                {step.type === 'soak' && (
                  <>
                    <div className="grid two" style={{ marginTop: '0.5rem' }}>
                      <div>
                        <label className="label">Setpoint (C)</label>
                        <input
                          className="input"
                          type="number"
                          value={step.setpoint_c}
                          onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'soak', setpoint_c: Number(e.target.value) }))}
                        />
                      </div>
                      <div>
                        <label className="label">Soak Time (s)</label>
                        <input
                          className="input"
                          type="number"
                          value={step.soak_time_s}
                          onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'soak', soak_time_s: Number(e.target.value) }))}
                        />
                      </div>
                    </div>
                    <label className="row" style={{ marginTop: '0.5rem' }}>
                      <input
                        type="checkbox"
                        checked={!!step.guaranteed}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'soak', guaranteed: e.target.checked }))}
                      />
                      Guaranteed soak (timer runs only in deviation band)
                    </label>
                    {step.guaranteed && (
                      <>
                        <label className="label" style={{ marginTop: '0.5rem' }}>Deviation (C)</label>
                        <input
                          className="input"
                          type="number"
                          value={step.deviation_c ?? 1}
                          onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'soak', deviation_c: Number(e.target.value) }))}
                        />
                      </>
                    )}
                  </>
                )}

                {step.type === 'ramp_time' && (
                  <div className="grid two" style={{ marginTop: '0.5rem' }}>
                    <div>
                      <label className="label">Target Setpoint (C)</label>
                      <input
                        className="input"
                        type="number"
                        value={step.setpoint_c}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'ramp_time', setpoint_c: Number(e.target.value) }))}
                      />
                    </div>
                    <div>
                      <label className="label">Ramp Time (s)</label>
                      <input
                        className="input"
                        type="number"
                        value={step.ramp_time_s}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'ramp_time', ramp_time_s: Number(e.target.value) }))}
                      />
                    </div>
                  </div>
                )}

                {step.type === 'ramp_rate' && (
                  <div className="grid two" style={{ marginTop: '0.5rem' }}>
                    <div>
                      <label className="label">Target Setpoint (C)</label>
                      <input
                        className="input"
                        type="number"
                        value={step.setpoint_c}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'ramp_rate', setpoint_c: Number(e.target.value) }))}
                      />
                    </div>
                    <div>
                      <label className="label">Ramp Rate (C/s)</label>
                      <input
                        className="input"
                        type="number"
                        value={step.ramp_rate_c_per_s}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'ramp_rate', ramp_rate_c_per_s: Number(e.target.value) }))}
                      />
                    </div>
                  </div>
                )}

                {step.type === 'jump' && (
                  <div className="grid two" style={{ marginTop: '0.5rem' }}>
                    <div>
                      <label className="label">Target Step Number (1-based)</label>
                      <input
                        className="input"
                        type="number"
                        value={step.target_step_number}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'jump', target_step_number: Number(e.target.value) }))}
                      />
                    </div>
                    <div>
                      <label className="label">Repeat Count</label>
                      <input
                        className="input"
                        type="number"
                        value={step.repeat_count}
                        onChange={(e) => updateStep(idx, (prev) => ({ ...prev, type: 'jump', repeat_count: Number(e.target.value) }))}
                      />
                    </div>
                  </div>
                )}
              </article>
            ))}
          </div>
        </section>

        <section className="card">
          <h3 className="section-title">Board Profiles + Runtime</h3>
          <div className="grid" style={{ gap: '0.75rem' }}>
            <article className="card" style={{ border: '1px solid #d1d5db' }}>
              <strong>Uploaded Profile (RAM)</strong>
              <div className="muted">{uploadedSummary.present ? `${uploadedSummary.name} (${uploadedSummary.step_count} steps)` : 'None loaded in RAM'}</div>
              <div className="row" style={{ marginTop: '0.5rem', flexWrap: 'wrap' }}>
                <button onClick={loadUploadedToEditor} disabled={busy || !uploadedSummary.present}>Load to Editor</button>
                <button className="secondary" onClick={clearUploaded} disabled={busy || !uploadedSummary.present}>Clear Uploaded</button>
              </div>
            </article>

            <div className="row" style={{ flexWrap: 'wrap' }}>
              <label className="label" style={{ margin: 0 }}>Save source</label>
              <select className="select" value={saveMode} onChange={(e) => setSaveMode(e.target.value as 'editor' | 'uploaded')} style={{ maxWidth: 220 }}>
                <option value="editor">Editor profile</option>
                <option value="uploaded">Uploaded profile</option>
              </select>
            </div>

            {slots.map((slot) => (
              <article key={slot.slot_index} className="card" style={{ border: `1px solid ${slot.occupied ? '#fca5a5' : '#86efac'}` }}>
                <strong>Slot {slot.slot_index + 1}</strong>
                <div className="muted">{slot.occupied ? `${slot.name} (${slot.step_count} steps)` : 'Empty'}</div>
                <div className="row" style={{ marginTop: '0.5rem', flexWrap: 'wrap' }}>
                  <button onClick={() => loadSlot(slot.slot_index)} disabled={busy || !slot.occupied}>Load</button>
                  <button onClick={() => saveToSlot(slot.slot_index)} disabled={busy}>Save Here</button>
                  <button className="secondary" onClick={() => deleteSlot(slot.slot_index)} disabled={busy || !slot.occupied}>Delete</button>
                </div>
              </article>
            ))}

            <article className="card" style={{ border: '1px solid #fecaca' }}>
              <strong>Run Profile</strong>
              <div className="grid two" style={{ marginTop: '0.5rem' }}>
                <div>
                  <label className="label">Source</label>
                  <select className="select" value={runSource} onChange={(e) => setRunSource(e.target.value as 'uploaded' | 'slot')}>
                    <option value="uploaded">Uploaded (RAM)</option>
                    <option value="slot">Saved Slot</option>
                  </select>
                </div>
                <div>
                  <label className="label">Slot</label>
                  <select className="select" value={runSlotIndex} onChange={(e) => setRunSlotIndex(Number(e.target.value))} disabled={runSource !== 'slot'}>
                    {slots.map((slot) => (
                      <option key={slot.slot_index} value={slot.slot_index}>Slot {slot.slot_index + 1}</option>
                    ))}
                  </select>
                </div>
              </div>
              <button className="primary" style={{ marginTop: '0.6rem' }} onClick={runProfile} disabled={!canRun}>
                {status?.profile?.running ? 'Profile Running' : 'Start Profile'}
              </button>
              {!canRun && !status?.profile?.running && (
                <div className="muted" style={{ marginTop: '0.5rem' }}>
                  {runSource === 'uploaded' ? 'Upload a profile to RAM before running.' : 'Select an occupied slot to run.'}
                </div>
              )}

              <div className="grid two" style={{ marginTop: '0.75rem' }}>
                <div>
                  <div className="muted">Active Step</div>
                  <strong>{status?.profile?.running ? `${status?.profile?.current_step_number} (${status?.profile?.current_step_type})` : '--'}</strong>
                </div>
                <div>
                  <div className="muted">Elapsed</div>
                  <strong>{formatSeconds(status?.profile?.profile_elapsed_s ?? 0)}</strong>
                </div>
                <div>
                  <div className="muted">Step Elapsed</div>
                  <strong>{formatSeconds(status?.profile?.step_elapsed_s ?? 0)}</strong>
                </div>
                <div>
                  <div className="muted">Last End Reason</div>
                  <strong>{status?.profile?.last_end_reason ?? 'none'}</strong>
                </div>
              </div>
            </article>
          </div>
        </section>
      </div>

      <section className="card">
        <h3 className="section-title">Preview + Live PV Overlay</h3>
        <div style={{ height: 360 }}>
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={chartData}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
              <XAxis dataKey="t" name="Time (s)" type="number" domain={['dataMin', 'dataMax']} tickFormatter={(v) => `${Number(v).toFixed(0)}s`} />
              <YAxis />
              <Tooltip formatter={(value: number | string) => Number(value).toFixed(2)} labelFormatter={(label) => `${Number(label).toFixed(2)} s`} />
              <Legend />
              {activeWindow && (
                <ReferenceArea x1={activeWindow.start} x2={activeWindow.end} fill="#fef3c7" fillOpacity={0.45} />
              )}
              <Line
                type="monotone"
                dataKey="target"
                stroke="#dc2626"
                strokeWidth={4}
                strokeOpacity={0.65}
                strokeDasharray="8 6"
                dot={false}
                name="Profile Target"
                isAnimationActive={false}
                connectNulls
              />
              <Line
                type="monotone"
                dataKey="pv"
                stroke="#2563eb"
                strokeWidth={2.5}
                dot={false}
                name="Process Value"
                isAnimationActive={false}
                connectNulls
              />
            </LineChart>
          </ResponsiveContainer>
        </div>
      </section>

      {!!issues.length && (
        <section className="card" style={{ borderColor: '#fca5a5' }}>
          <h3 className="section-title">Validation Issues</h3>
          <div className="warning">
            {issues.map((issue, i) => (
              <div key={i}>{issue}</div>
            ))}
          </div>
        </section>
      )}
    </div>
  );
}
