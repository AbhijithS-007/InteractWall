import { useEffect, useState } from 'react';
import { getStatus } from '../ipc';

export default function Performance() {
  const [status, setStatus] = useState<any>(null);

  useEffect(() => {
    const interval = setInterval(async () => {
      try {
        const s = await getStatus();
        setStatus(s);
      } catch (err) {
        console.error(err);
      }
    }, 1000);
    return () => clearInterval(interval);
  }, []);

  return (
    <div>
      <h2 className="page-title">Performance Metrics</h2>
      <div className="card">
        <h2>Engine Telemetry</h2>
        <pre style={{background: 'rgba(0,0,0,0.5)', padding: '1rem', borderRadius: '6px', overflowX: 'auto'}}>
          {status ? JSON.stringify(status, null, 2) : "Awaiting data from IPC pipe..."}
        </pre>
      </div>
    </div>
  );
}
