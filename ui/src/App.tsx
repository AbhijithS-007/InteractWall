import { BrowserRouter as Router, Routes, Route, NavLink, Navigate } from 'react-router-dom';
import { Wand2, Settings as SettingsIcon, Info, Layers } from 'lucide-react';
import './index.css';

// Pages
import Effects from './pages/Effects';
import Settings from './pages/Settings';
import { useEffect } from 'react';
import About from './pages/About';
import Gallery from './pages/Gallery';
import { loadSettings, applySettingsToBackend, getActiveSession, loadEffectSettings } from './store';
import { applyWallpaper, setEffect, setSetting, isAutostart } from './ipc';

function App() {
  useEffect(() => {
    loadSettings().then(applySettingsToBackend).catch(console.error);
    
    // Auto-restore last active wallpaper and its settings ONLY if launched via autostart (system boot)
    isAutostart().then((isAuto) => {
      if (!isAuto) return; // User manually opened the UI; don't force auto-apply
      
      getActiveSession().then(async (session) => {
          if (session) {
              await applyWallpaper(session.layerA, session.layerB);
              await setEffect(session.effect);
              const settings = await loadEffectSettings(session.effect);
              if (settings) {
                // Short delay to ensure plugin is initialized before accepting settings
                setTimeout(async () => {
                    for (const [k, v] of Object.entries(settings)) {
                        await setSetting(k, v);
                    }
                }, 100);
            }
        }
    }).catch(console.error);
    });
  }, []);
  return (
    <Router>
      <div className="top-bar">
        <h1 className="brand">
          <Layers className="brand-icon" size={24} />
          <span className="brand-text">Interact<span className="brand-accent">Wall</span></span>
        </h1>
        <div className="nav-capsule">
          <NavLink to="/gallery" className={({isActive}) => `nav-link ${isActive ? 'active' : ''}`}>
            <Layers size={18} /> Gallery
          </NavLink>
          <NavLink to="/effects" className={({isActive}) => `nav-link ${isActive ? 'active' : ''}`}>
            <Wand2 size={18} /> Effects
          </NavLink>
          <NavLink to="/settings" className={({isActive}) => `nav-link ${isActive ? 'active' : ''}`}>
            <SettingsIcon size={18} /> Settings
          </NavLink>
          <NavLink to="/about" className={({isActive}) => `nav-link ${isActive ? 'active' : ''}`}>
            <Info size={18} /> About
          </NavLink>
        </div>
        <div style={{width: '150px'}}></div> {/* Spacer for centering capsule */}
      </div>
      
      <div className="content">
        <Routes>
          <Route path="/" element={<Navigate to="/effects" replace />} />
          <Route path="/gallery" element={<Gallery />} />
          <Route path="/effects" element={<Effects />} />
          <Route path="/settings" element={<Settings />} />
          <Route path="/about" element={<About />} />
        </Routes>
      </div>
    </Router>
  );
}

export default App;
