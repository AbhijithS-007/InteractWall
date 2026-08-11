import { useState, useEffect } from 'react';
import { enable, isEnabled, disable } from '@tauri-apps/plugin-autostart';
import { Power, MonitorPlay, EyeOff, Battery, Lock, Monitor, Cpu } from 'lucide-react';

import { loadSettings, saveSettings, AppSettings, defaultSettings, resetToDefaults } from '../store';

export default function Settings() {
  const [autostart, setAutostart] = useState(false);
  const [settings, setSettings] = useState<AppSettings>(defaultSettings);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    async function init() {
      console.log("Settings init: starting");
      try {
        const auto = await isEnabled();
        setAutostart(auto);
        console.log("Settings init: autostart fetched", auto);
      } catch (err) {
        console.warn("Autostart not supported/configured yet", err);
      }

      console.log("Settings init: calling loadSettings");
      try {
        const s = await loadSettings();
        setSettings(s);
        console.log("Settings init: loadSettings finished");
      } catch (err) {
        console.error("Settings init: loadSettings failed", err);
      }

      setLoading(false);
      console.log("Settings init: complete");
    }
    init();
  }, []);

  const toggleAutostart = async () => {
    try {
      if (autostart) {
        await disable();
      } else {
        await enable();
      }
      setAutostart(!autostart);
    } catch (err) {
      console.error(err);
    }
  };

  const updateSetting = async (key: keyof AppSettings, value: any) => {
    const newSettings = { ...settings, [key]: value };
    setSettings(newSettings);
    await saveSettings(newSettings);
  };

  const handleReset = async () => {
    if (confirm('Are you sure you want to reset all settings to defaults? This will restart the renderer.')) {
      await resetToDefaults();
      setSettings(defaultSettings);
    }
  };

  if (loading) {
    return <div style={{padding: '2rem'}}>Loading settings...</div>;
  }

  return (
    <div style={{maxWidth: '800px'}}>
      <h2 className="page-title" style={{marginBottom: '2rem'}}>Engine Settings</h2>
      
      <div className="card" style={{marginBottom: '1.5rem'}}>
        <div style={{display: 'flex', alignItems: 'center', gap: '0.75rem', marginBottom: '1.5rem'}}>
          <Cpu className="brand-accent" size={24} />
          <h3 style={{margin: 0, fontSize: '1.2rem', fontWeight: 600}}>System Integration</h3>
        </div>
        
        <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: '8px'}}>
          <div style={{display: 'flex', gap: '1rem', alignItems: 'flex-start'}}>
            <Power size={20} style={{marginTop: '0.2rem', color: 'var(--text-secondary)'}} />
            <div>
              <strong style={{fontSize: '1rem'}}>Launch at Startup</strong>
              <p style={{fontSize: '0.85rem', color: 'var(--text-secondary)', margin: '0.25rem 0 0 0', lineHeight: 1.4}}>
                Start InteractWall automatically silently in the background when you sign into Windows.
              </p>
            </div>
          </div>
          <button className={autostart ? 'primary' : 'secondary'} onClick={toggleAutostart}>
            {autostart ? 'Enabled' : 'Disabled'}
          </button>
        </div>
      </div>

      <div className="card">
        <div style={{display: 'flex', alignItems: 'center', gap: '0.75rem', marginBottom: '1.5rem'}}>
          <Battery className="brand-accent" size={24} />
          <h3 style={{margin: 0, fontSize: '1.2rem', fontWeight: 600}}>Power & Performance</h3>
        </div>
        
        <div style={{display: 'flex', flexDirection: 'column', gap: '0.75rem'}}>
          
          <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: '8px'}}>
            <div style={{display: 'flex', gap: '1rem', alignItems: 'flex-start'}}>
              <Monitor size={20} style={{marginTop: '0.2rem', color: 'var(--text-secondary)'}} />
              <div>
                <strong style={{fontSize: '0.95rem'}}>Idle Timeout (seconds)</strong>
                <p style={{fontSize: '0.85rem', color: 'var(--text-secondary)', margin: '0.25rem 0 0 0'}}>
                  Pause the engine (0% GPU) if the mouse hasn't moved for this long. Set to 0 to disable.
                </p>
              </div>
            </div>
            <input type="number" min="0" step="10" value={settings.idleTimeout} onChange={(e) => updateSetting('idleTimeout', parseFloat(e.target.value) || 0)} style={{width: '80px', textAlign: 'center'}} />
          </div>

          <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: '8px'}}>
            <div style={{display: 'flex', gap: '1rem', alignItems: 'flex-start'}}>
              <EyeOff size={20} style={{marginTop: '0.2rem', color: 'var(--text-secondary)'}} />
              <div>
                <strong style={{fontSize: '0.95rem'}}>Pause When Hidden</strong>
                <p style={{fontSize: '0.85rem', color: 'var(--text-secondary)', margin: '0.25rem 0 0 0'}}>
                  Pause rendering entirely when the desktop is completely covered by an opaque window.
                </p>
              </div>
            </div>
            <button className={settings.pauseHidden ? 'primary' : 'secondary'} onClick={() => updateSetting('pauseHidden', !settings.pauseHidden)}>
              {settings.pauseHidden ? 'Enabled' : 'Disabled'}
            </button>
          </div>

          <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: '8px'}}>
            <div style={{display: 'flex', gap: '1rem', alignItems: 'flex-start'}}>
              <MonitorPlay size={20} style={{marginTop: '0.2rem', color: 'var(--text-secondary)'}} />
              <div>
                <strong style={{fontSize: '0.95rem'}}>Pause During Fullscreen</strong>
                <p style={{fontSize: '0.85rem', color: 'var(--text-secondary)', margin: '0.25rem 0 0 0'}}>
                  Pause rendering when you're playing a game or watching a video in fullscreen.
                </p>
              </div>
            </div>
            <button className={settings.pauseFullscreen ? 'primary' : 'secondary'} onClick={() => updateSetting('pauseFullscreen', !settings.pauseFullscreen)}>
              {settings.pauseFullscreen ? 'Enabled' : 'Disabled'}
            </button>
          </div>

          <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: '8px'}}>
            <div style={{display: 'flex', gap: '1rem', alignItems: 'flex-start'}}>
              <Battery size={20} style={{marginTop: '0.2rem', color: 'var(--text-secondary)'}} />
              <div>
                <strong style={{fontSize: '0.95rem'}}>Pause on Battery Saver</strong>
                <p style={{fontSize: '0.85rem', color: 'var(--text-secondary)', margin: '0.25rem 0 0 0'}}>
                  Pause rendering when unplugged and running low on battery or when Battery Saver is active.
                </p>
              </div>
            </div>
            <button className={settings.pauseBattery ? 'primary' : 'secondary'} onClick={() => updateSetting('pauseBattery', !settings.pauseBattery)}>
              {settings.pauseBattery ? 'Enabled' : 'Disabled'}
            </button>
          </div>

          <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: '8px'}}>
            <div style={{display: 'flex', gap: '1rem', alignItems: 'flex-start'}}>
              <Lock size={20} style={{marginTop: '0.2rem', color: 'var(--text-secondary)'}} />
              <div>
                <strong style={{fontSize: '0.95rem'}}>Pause on Lock Screen</strong>
                <p style={{fontSize: '0.85rem', color: 'var(--text-secondary)', margin: '0.25rem 0 0 0'}}>
                  Pause rendering when the workstation is locked or accessed via Remote Desktop.
                </p>
              </div>
            </div>
            <button className={settings.pauseSessionLocked ? 'primary' : 'secondary'} onClick={() => updateSetting('pauseSessionLocked', !settings.pauseSessionLocked)}>
              {settings.pauseSessionLocked ? 'Enabled' : 'Disabled'}
            </button>
          </div>

        </div>
      </div>

      <div className="card" style={{marginTop: '2.5rem', background: 'rgba(239, 68, 68, 0.05)', borderColor: 'rgba(239, 68, 68, 0.2)'}}>
        <h3 style={{color: 'var(--danger)', marginTop: 0}}>Danger Zone</h3>
        <p style={{fontSize: '0.85rem', color: 'var(--text-secondary)', margin: '0.25rem 0 1.5rem 0'}}>
          Resetting will clear all settings and wallpaper pairings, and restart the renderer engine immediately.
        </p>
        <button className="danger" onClick={handleReset}>
          Reset Engine to Defaults
        </button>
      </div>
    </div>
  );
}
