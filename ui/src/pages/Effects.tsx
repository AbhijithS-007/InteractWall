import { useState, useRef, useEffect } from 'react';
import { setEffect, removeEffect, setSetting, setQualityTier, applyWallpaper, importWallpaper, generateDepthMap, getStatus } from '../ipc';
import { saveWallpaperPairing, loadEffectSettings, saveEffectSettings, saveActiveSession } from '../store';
import { open } from '@tauri-apps/plugin-dialog';
export default function Effects() {
  const [quality, setQuality] = useState('balanced');
  
  // Cursor Reveal Settings
  const [brushSize, setBrushSize] = useState(160);
  const [crBrushHardness, setCRBrushHardness] = useState(0.2);
  const [crTrailLength, setCRTrailLength] = useState(1.0);
  const [crFadeSpeed, setCRFadeSpeed] = useState(0.035);
  const [crFadeWhenResting, setCRFadeWhenResting] = useState(true);
  
  const debounceTimer = useRef<number | null>(null);

  // Depth Parallax State
  const [isGeneratingDepth, setIsGeneratingDepth] = useState(false);
  const [testWallpaper, setTestWallpaper] = useState<string | null>(null);
  const [depthError, setDepthError] = useState<string | null>(null);
  const [parallaxStrength, setParallaxStrength] = useState(0.05);
  
  // Unified effect tracker
  const [activeEffect, setActiveEffect] = useState<string | null>(null); // Actual running effect
  const [selectedEffect, setSelectedEffect] = useState<string | null>(null); // Effect being configured in Hero

  useEffect(() => {
    // Load last-used global effect settings when the component mounts,
    // then immediately push them to the backend renderer so it uses the saved values.
    const loadGlobals = async () => {
      try {
        const crSettings = await loadEffectSettings('cursor_reveal');
        if (crSettings) {
          const bs = crSettings.brushSize ?? 160;
          const bh = crSettings.brushHardness ?? 0.2;
          const tl = crSettings.trailLength ?? 1.0;
          const fs = crSettings.fadeSpeed ?? 0.035;
          const fw = crSettings.fadeWhenResting ?? 1;
          setBrushSize(bs);
          setCRBrushHardness(bh);
          setCRTrailLength(tl);
          setCRFadeSpeed(fs);
          setCRFadeWhenResting(fw === 1);
          // Push to backend so renderer uses saved values from the start
          setSetting('brushSize', bs);
          setSetting('brushHardness', bh);
          setSetting('trailLength', tl);
          setSetting('fadeSpeed', fs);
          setSetting('fadeWhenResting', fw);
        }

        const dpSettings = await loadEffectSettings('depth_parallax');
        if (dpSettings) {
          const ps = dpSettings.parallaxStrength ?? 0.05;
          setParallaxStrength(ps);
          setSetting('parallaxStrength', ps);
        }

        const glSettings = await loadEffectSettings('gravity_lens');
        if (glSettings) {
          const ls = glSettings.lensStrength ?? 5.0;
          const lr = glSettings.lensRadius ?? 0.3;
          const st = glSettings.stiffness ?? 50.0;
          const dm = glSettings.damping ?? 0.90;
          const di = glSettings.dispersion ?? 0.02;
          const cd = glSettings.coreDarkening ?? 0.5;
          const gt = glSettings.trailLength ?? 1.0;
          const fd = glSettings.fadeDecay ?? 0.92;
          setGLStrength(ls);
          setGLRadius(lr);
          setGLStiffness(st);
          setGLDamping(dm);
          setGLDispersion(di);
          setGLDarkening(cd);
          setGLTrailLength(gt);
          setGLFadeDecay(fd);
          // NOTE: Do NOT push settings to backend here — setSetting goes to
          // whichever plugin is currently active, NOT necessarily gravity_lens.
          // Settings are pushed in the activation function instead.
        }

        const spSettings = await loadEffectSettings('stone_press');
        if (spSettings) {
          // Clamp loaded values to safe ranges to prevent stale high values
          const depth = Math.min(spSettings.pressDepth ?? 0.03, 0.15);
          const rad = Math.min(spSettings.pressRadius ?? 0.08, 0.3);
          const st = spSettings.stiffness ?? 50.0;
          const dm = spSettings.damping ?? 0.90;
          const di = spSettings.dispersion ?? 0.02;
          const cd = spSettings.coreDarkening ?? 0.15;
          const gt = spSettings.trailLength ?? 1.0;
          const fd = spSettings.fadeDecay ?? 0.92;
          setSPDepth(depth);
          setSPRadius(rad);
          setSPStiffness(st);
          setSPDamping(dm);
          setSPDispersion(di);
          setSPDarkening(cd);
          setSPTrailLength(gt);
          setSPFadeDecay(fd);
          // NOTE: Do NOT push settings to backend here — setSetting goes to
          // whichever plugin is currently active, NOT necessarily stone_press.
          // Settings are pushed in the activation function instead.
        }
      } catch (err) {
        console.error("Failed to load global effect settings:", err);
      }
    };
    loadGlobals();

    // Flush debounced changes on page unload
    const handleBeforeUnload = () => {
      if (debounceTimer.current) {
        window.clearTimeout(debounceTimer.current);
        debounceTimer.current = null;
      }
      
      const crSettings = {
        brushSize,
        brushHardness: crBrushHardness,
        trailLength: crTrailLength,
        fadeSpeed: crFadeSpeed,
        fadeWhenResting: crFadeWhenResting ? 1 : 0,
      };
      saveEffectSettings('cursor_reveal', crSettings);
      if (layerA) saveWallpaperPairing(layerA, 'cursor_reveal', crSettings);

      saveEffectSettings('depth_parallax', { parallaxStrength });
      if (testWallpaper) saveWallpaperPairing(testWallpaper, 'depth_parallax', { parallaxStrength });

      const glSettings = {
        lensStrength: glStrength,
        lensRadius: glRadius,
        stiffness: glStiffness,
        damping: glDamping,
        dispersion: glDispersion,
        coreDarkening: glDarkening,
        trailLength: glTrailLength,
        fadeDecay: glFadeDecay,
      };
      saveEffectSettings('gravity_lens', glSettings);
      if (glBaseImage) saveWallpaperPairing(glBaseImage, 'gravity_lens', glSettings);

      const spSettings = {
        pressDepth: spDepth,
        pressRadius: spRadius,
        stiffness: spStiffness,
        damping: spDamping,
        dispersion: spDispersion,
        coreDarkening: spDarkening,
        trailLength: spTrailLength,
        fadeDecay: spFadeDecay,
      };
      saveEffectSettings('stone_press', spSettings);
      if (spBaseImage) saveWallpaperPairing(spBaseImage, 'stone_press', spSettings);
    };

    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => {
      window.removeEventListener('beforeunload', handleBeforeUnload);
    };
  }, []);

  useEffect(() => {
    const fetchStatus = async () => {
      try {
        const status = await getStatus();
        if (status && status.activePlugin) {
          let uiName = status.activePlugin;
          if (uiName.toLowerCase().includes("gravity")) uiName = "gravity_lens";
          else if (uiName.toLowerCase().includes("stone")) uiName = "stone_press";
          else if (uiName.toLowerCase().includes("depth")) uiName = "depth_parallax";
          else if (uiName.toLowerCase().includes("cursor")) uiName = "cursor_reveal";
          else if (uiName.toLowerCase().includes("none")) uiName = "none";
          
          const newActive = uiName === 'none' ? null : uiName;
          setActiveEffect(newActive);
          setSelectedEffect(prev => prev === null ? newActive : prev);
        }
      } catch (err) {
        console.error("Failed to fetch status:", err);
      }
    };
    fetchStatus();
    const interval = setInterval(fetchStatus, 1000);
    return () => clearInterval(interval);
  }, []);

  const handleRemoveEffect = async () => {
    try {
      await removeEffect();
      setActiveEffect(null);
      await saveActiveSession(null);
    } catch (err) {
      console.error("Failed to remove effect", err);
    }
  };

  const activateDepthParallax = async () => {
    if (!testWallpaper) return;
    try {
      // The depth map is saved with "_depth.png" suffix, replacing the original extension
      const depthImage = testWallpaper.replace(/\.[^/.]+$/, "") + "_depth.png";
      await applyWallpaper(testWallpaper, depthImage);
      await setEffect('depth_parallax');
      setActiveEffect('depth_parallax');
      setSelectedEffect('depth_parallax');
      await saveWallpaperPairing(testWallpaper, 'depth_parallax', { parallaxStrength });
      await saveActiveSession({ layerA: testWallpaper, layerB: depthImage, effect: 'depth_parallax' });
      await setSetting('parallaxStrength', parallaxStrength);
    } catch (err) {
      console.error("Failed to activate depth parallax", err);
    }
  };

  const handleDepthImport = async () => {
    const selected = await open({
      multiple: false,
      filters: [{ name: 'Image', extensions: ['png', 'jpeg', 'jpg', 'webp', 'bmp'] }]
    });

    if (selected && typeof selected === 'string') {
      try {
        setDepthError(null);
        const newPath = await importWallpaper(selected);
        setTestWallpaper(newPath);
        if (newPath) {
          setIsGeneratingDepth(true);
          try {
            await generateDepthMap(newPath);
          } catch (err: any) {
            console.error("Depth generation failed:", err);
            setDepthError("Depth generation failed: " + err.toString());
          } finally {
            setIsGeneratingDepth(false);
          }
        }
      } catch (err) {
        console.error("Failed to import wallpaper:", err);
      }
    }
  };

  // Cursor Reveal State
  const [layerA, setLayerA] = useState<string | null>(null);
  const [layerB, setLayerB] = useState<string | null>(null);


  const handleQualityChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    setQuality(e.target.value);
    setQualityTier(e.target.value);
  };

  const handleCRSettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => {
        setSetting(key, val);
        const newCRSettings = {
          brushSize: key === 'brushSize' ? val : brushSize,
          brushHardness: key === 'brushHardness' ? val : crBrushHardness,
          trailLength: key === 'trailLength' ? val : crTrailLength,
          fadeSpeed: key === 'fadeSpeed' ? val : crFadeSpeed,
          fadeWhenResting: key === 'fadeWhenResting' ? val : (crFadeWhenResting ? 1 : 0),
        };
        saveEffectSettings('cursor_reveal', newCRSettings);
        if (layerA) {
          saveWallpaperPairing(layerA, 'cursor_reveal', newCRSettings);
        }
    }, 50);
  };

  const handleImport = async (setLayer: React.Dispatch<React.SetStateAction<string | null>>) => {
    const selected = await open({
      multiple: false,
      filters: [{
        name: 'Image',
        extensions: ['png', 'jpeg', 'jpg', 'webp', 'bmp']
      }]
    });

    if (selected && typeof selected === 'string') {
      try {
        const newPath = await importWallpaper(selected);
        setLayer(newPath);
      } catch (err) {
        console.error("Failed to import wallpaper:", err);
      }
    }
  };

  const activateEffect = async () => {
    if (!layerA || !layerB) return;
    try {
      await applyWallpaper(layerA, layerB);
      await setEffect('cursor_reveal');
      setActiveEffect('cursor_reveal');
      setSelectedEffect('cursor_reveal');
      const crSettings = {
        brushSize, brushHardness: crBrushHardness, trailLength: crTrailLength, fadeSpeed: crFadeSpeed, fadeWhenResting: crFadeWhenResting ? 1 : 0
      };
      await saveEffectSettings('cursor_reveal', crSettings);
      await saveWallpaperPairing(layerA, 'cursor_reveal', crSettings);
      await saveActiveSession({ layerA, layerB, effect: 'cursor_reveal' });
      for (const [k, v] of Object.entries(crSettings)) {
        await setSetting(k, v);
      }
    } catch (err) {
      console.error("Failed to activate effect", err);
    }
  };

  // Gravity Lens State
  const [glBaseImage, setGLBaseImage] = useState<string | null>(null);
  const [glStrength, setGLStrength] = useState(5.0);
  const [glRadius, setGLRadius] = useState(0.3);
  const [glStiffness, setGLStiffness] = useState(50.0);
  const [glDamping, setGLDamping] = useState(0.90);
  const [glDispersion, setGLDispersion] = useState(0.02);
  const [glDarkening, setGLDarkening] = useState(0.5);
  const [glTrailLength, setGLTrailLength] = useState(1.0);
  const [glFadeDecay, setGLFadeDecay] = useState(0.92);

  const activateGravityLens = async () => {
    if (!glBaseImage) return;
    try {
      await applyWallpaper(glBaseImage, ""); // only one layer
      await setEffect('gravity_lens');
      setActiveEffect('gravity_lens');
      setSelectedEffect('gravity_lens');
      const glSettings = {
        lensStrength: glStrength, lensRadius: glRadius, stiffness: glStiffness,
        damping: glDamping, dispersion: glDispersion, coreDarkening: glDarkening,
        trailLength: glTrailLength, fadeDecay: glFadeDecay
      };
      await saveEffectSettings('gravity_lens', glSettings);
      await saveWallpaperPairing(glBaseImage, 'gravity_lens', glSettings);
      await saveActiveSession({ layerA: glBaseImage, layerB: "", effect: 'gravity_lens' });
      for (const [k, v] of Object.entries(glSettings)) {
        await setSetting(k, v);
      }
    } catch (err) {
      console.error("Failed to activate gravity lens", err);
    }
  };

  const handleGLSettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => {
        setSetting(key, val);
        const newGLSettings = {
          lensStrength: key === 'lensStrength' ? val : glStrength,
          lensRadius: key === 'lensRadius' ? val : glRadius,
          stiffness: key === 'stiffness' ? val : glStiffness,
          damping: key === 'damping' ? val : glDamping,
          dispersion: key === 'dispersion' ? val : glDispersion,
          coreDarkening: key === 'coreDarkening' ? val : glDarkening,
          trailLength: key === 'trailLength' ? val : glTrailLength,
          fadeDecay: key === 'fadeDecay' ? val : glFadeDecay,
        };
        saveEffectSettings('gravity_lens', newGLSettings);
        if (glBaseImage) {
          saveWallpaperPairing(glBaseImage, 'gravity_lens', newGLSettings);
        }
    }, 50);
  };

  // Stone Press State
  const [spBaseImage, setSPBaseImage] = useState<string | null>(null);
  const [spDepth, setSPDepth] = useState(0.03);
  const [spRadius, setSPRadius] = useState(0.08);
  const [spStiffness, setSPStiffness] = useState(50.0);
  const [spDamping, setSPDamping] = useState(0.90);
  const [spDispersion, setSPDispersion] = useState(0.02);
  const [spDarkening, setSPDarkening] = useState(0.15);
  const [spTrailLength, setSPTrailLength] = useState(1.0);
  const [spFadeDecay, setSPFadeDecay] = useState(0.92);

  const activateStonePress = async () => {
    if (!spBaseImage) return;
    try {
      await applyWallpaper(spBaseImage, ""); // only one layer
      await setEffect('stone_press');
      setActiveEffect('stone_press');
      setSelectedEffect('stone_press');
      const spSettings = {
        pressDepth: spDepth, pressRadius: spRadius, stiffness: spStiffness,
        damping: spDamping, dispersion: spDispersion, coreDarkening: spDarkening,
        trailLength: spTrailLength, fadeDecay: spFadeDecay
      };
      await saveEffectSettings('stone_press', spSettings);
      await saveWallpaperPairing(spBaseImage, 'stone_press', spSettings);
      await saveActiveSession({ layerA: spBaseImage, layerB: "", effect: 'stone_press' });
      for (const [k, v] of Object.entries(spSettings)) {
        await setSetting(k, v);
      }
    } catch (err) {
      console.error("Failed to activate stone press", err);
    }
  };

  const handleSPSettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => {
        setSetting(key, val);
        const newSPSettings = {
          pressDepth: key === 'pressDepth' ? val : spDepth,
          pressRadius: key === 'pressRadius' ? val : spRadius,
          stiffness: key === 'stiffness' ? val : spStiffness,
          damping: key === 'damping' ? val : spDamping,
          dispersion: key === 'dispersion' ? val : spDispersion,
          coreDarkening: key === 'coreDarkening' ? val : spDarkening,
          trailLength: key === 'trailLength' ? val : spTrailLength,
          fadeDecay: key === 'fadeDecay' ? val : spFadeDecay,
        };
        saveEffectSettings('stone_press', newSPSettings);
        if (spBaseImage) {
          saveWallpaperPairing(spBaseImage, 'stone_press', newSPSettings);
        }
    }, 50);
  };

  // Component specific renderers for active settings
  const renderQualityTier = () => (
    <div className="card" style={{marginBottom: '2rem'}}>
      <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center'}}>
        <div>
          <h2 style={{margin: 0}}>Render Quality</h2>
          <p style={{color: 'var(--text-secondary)', margin: '0.25rem 0 0 0', fontSize: '0.9rem'}}>
            Select the base render resolution.
          </p>
        </div>
        <select value={quality} onChange={handleQualityChange} style={{width: '200px'}}>
          <option value="low">Low (Half Res)</option>
          <option value="balanced">Balanced (Auto)</option>
          <option value="high">High (Native)</option>
        </select>
      </div>
    </div>
  );

  const renderActiveEffectControls = () => {
    if (selectedEffect === 'cursor_reveal') {
      return (
        <div style={{animation: 'fadeIn 0.3s ease'}}>
          <div style={{display: 'flex', gap: '1rem', marginBottom: '1.5rem'}}>
            <div style={{flex: 1, padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)'}}>
              <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Layer A (Background)</h4>
              <button className="secondary" onClick={() => handleImport(setLayerA)} style={{width: '100%', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap'}}>
                {layerA ? layerA.split('\\').pop() : "Import Image..."}
              </button>
            </div>
            <div style={{flex: 1, padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)'}}>
              <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Layer B (Reveal)</h4>
              <button className="secondary" onClick={() => handleImport(setLayerB)} style={{width: '100%', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap'}}>
                {layerB ? layerB.split('\\').pop() : "Import Image..."}
              </button>
            </div>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group">
              <label>Brush Size ({brushSize}px)</label>
              <input type="range" min="50" max="300" step="1" value={brushSize} onChange={(e) => handleCRSettingChange('brushSize', parseFloat(e.target.value), setBrushSize)} />
            </div>
            <div className="control-group">
              <label>Brush Hardness ({crBrushHardness.toFixed(2)})</label>
              <input type="range" min="0.0" max="1.0" step="0.05" value={crBrushHardness} onChange={(e) => handleCRSettingChange('brushHardness', parseFloat(e.target.value), setCRBrushHardness)} />
            </div>
            <div className="control-group">
              <label>Trail Length ({crTrailLength.toFixed(1)}s)</label>
              <input type="range" min="0.0" max="5.0" step="0.1" value={crTrailLength} onChange={(e) => handleCRSettingChange('trailLength', parseFloat(e.target.value), setCRTrailLength)} />
            </div>
            <div className="control-group">
              <label>Fade Out Speed ({crFadeSpeed.toFixed(3)})</label>
              <input type="range" min="0.005" max="0.1" step="0.005" value={crFadeSpeed} onChange={(e) => handleCRSettingChange('fadeSpeed', parseFloat(e.target.value), setCRFadeSpeed)} />
            </div>
          </div>
          <div className="control-group" style={{display: 'flex', alignItems: 'center', gap: '0.75rem', marginTop: '0.5rem'}}>
            <input type="checkbox" id="crFadeResting" checked={crFadeWhenResting} onChange={(e) => {
              setCRFadeWhenResting(e.target.checked);
              handleCRSettingChange('fadeWhenResting', e.target.checked ? 1 : 0, () => {});
            }} style={{width: '18px', height: '18px', accentColor: 'var(--accent)'}} />
            <label htmlFor="crFadeResting" style={{margin: 0, cursor: 'pointer'}}>Disappear when cursor is resting</label>
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateEffect} disabled={!layerA || !layerB}>
              {activeEffect === 'cursor_reveal' ? 'Re-Apply Changes' : 'Activate Effect'}
            </button>
          </div>
        </div>
      );
    }
    if (selectedEffect === 'gravity_lens') {
      return (
        <div style={{animation: 'fadeIn 0.3s ease'}}>
          <div style={{padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', marginBottom: '1.5rem'}}>
            <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Base Wallpaper</h4>
            <button className="secondary" onClick={() => handleImport(setGLBaseImage)}>
              {glBaseImage ? glBaseImage.split('\\').pop() : "Import Image..."}
            </button>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group"><label>Lens Strength ({glStrength.toFixed(1)})</label><input type="range" min="0" max="20" step="0.5" value={glStrength} onChange={(e) => handleGLSettingChange('lensStrength', parseFloat(e.target.value), setGLStrength)} /></div>
            <div className="control-group"><label>Lens Radius ({glRadius.toFixed(2)})</label><input type="range" min="0.1" max="1.0" step="0.05" value={glRadius} onChange={(e) => handleGLSettingChange('lensRadius', parseFloat(e.target.value), setGLRadius)} /></div>
            <div className="control-group"><label>Spring Stiffness ({glStiffness.toFixed(0)})</label><input type="range" min="10" max="200" step="1" value={glStiffness} onChange={(e) => handleGLSettingChange('stiffness', parseFloat(e.target.value), setGLStiffness)} /></div>
            <div className="control-group"><label>Spring Damping ({glDamping.toFixed(2)})</label><input type="range" min="0.70" max="0.99" step="0.01" value={glDamping} onChange={(e) => handleGLSettingChange('damping', parseFloat(e.target.value), setGLDamping)} /></div>
            <div className="control-group"><label>Chromatic Dispersion ({glDispersion.toFixed(3)})</label><input type="range" min="0" max="0.1" step="0.005" value={glDispersion} onChange={(e) => handleGLSettingChange('dispersion', parseFloat(e.target.value), setGLDispersion)} /></div>
            <div className="control-group"><label>Trail Length ({glTrailLength.toFixed(1)}s)</label><input type="range" min="0" max="5" step="0.1" value={glTrailLength} onChange={(e) => handleGLSettingChange('trailLength', parseFloat(e.target.value), setGLTrailLength)} /></div>
            <div className="control-group"><label>Fade Speed ({glFadeDecay >= 0.99 ? 'Never' : glFadeDecay.toFixed(2)})</label><input type="range" min="0.80" max="1.0" step="0.01" value={glFadeDecay} onChange={(e) => handleGLSettingChange('fadeDecay', parseFloat(e.target.value), setGLFadeDecay)} /></div>
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateGravityLens} disabled={!glBaseImage}>
              {activeEffect === 'gravity_lens' ? 'Re-Apply Changes' : 'Activate Effect'}
            </button>
          </div>
        </div>
      );
    }
    if (selectedEffect === 'stone_press') {
      return (
        <div style={{animation: 'fadeIn 0.3s ease'}}>
          <div style={{padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', marginBottom: '1.5rem'}}>
            <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Base Wallpaper</h4>
            <button className="secondary" onClick={() => handleImport(setSPBaseImage)}>
              {spBaseImage ? spBaseImage.split('\\').pop() : "Import Image..."}
            </button>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group"><label>Press Depth ({spDepth.toFixed(3)})</label><input type="range" min="0" max="0.15" step="0.005" value={spDepth} onChange={(e) => handleSPSettingChange('pressDepth', parseFloat(e.target.value), setSPDepth)} /></div>
            <div className="control-group"><label>Press Radius ({spRadius.toFixed(2)})</label><input type="range" min="0.02" max="0.25" step="0.01" value={spRadius} onChange={(e) => handleSPSettingChange('pressRadius', parseFloat(e.target.value), setSPRadius)} /></div>
            <div className="control-group"><label>Spring Stiffness ({spStiffness.toFixed(0)})</label><input type="range" min="10" max="200" step="1" value={spStiffness} onChange={(e) => handleSPSettingChange('stiffness', parseFloat(e.target.value), setSPStiffness)} /></div>
            <div className="control-group"><label>Spring Damping ({spDamping.toFixed(2)})</label><input type="range" min="0.70" max="0.99" step="0.01" value={spDamping} onChange={(e) => handleSPSettingChange('damping', parseFloat(e.target.value), setSPDamping)} /></div>
            <div className="control-group"><label>Chromatic Dispersion ({spDispersion.toFixed(3)})</label><input type="range" min="0" max="0.1" step="0.005" value={spDispersion} onChange={(e) => handleSPSettingChange('dispersion', parseFloat(e.target.value), setSPDispersion)} /></div>
            <div className="control-group"><label>Trail Length ({spTrailLength.toFixed(1)}s)</label><input type="range" min="0" max="5" step="0.1" value={spTrailLength} onChange={(e) => handleSPSettingChange('trailLength', parseFloat(e.target.value), setSPTrailLength)} /></div>
            <div className="control-group"><label>Fade Speed ({spFadeDecay >= 0.99 ? 'Never' : spFadeDecay.toFixed(2)})</label><input type="range" min="0.80" max="1.0" step="0.01" value={spFadeDecay} onChange={(e) => handleSPSettingChange('fadeDecay', parseFloat(e.target.value), setSPFadeDecay)} /></div>
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateStonePress} disabled={!spBaseImage}>
              {activeEffect === 'stone_press' ? 'Re-Apply Changes' : 'Activate Effect'}
            </button>
          </div>
        </div>
      );
    }
    if (selectedEffect === 'depth_parallax') {
      return (
        <div style={{animation: 'fadeIn 0.3s ease'}}>
          <div style={{padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', marginBottom: '1.5rem'}}>
            <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Test Wallpaper</h4>
            <button className="secondary" onClick={handleDepthImport} disabled={isGeneratingDepth}>
              {testWallpaper ? testWallpaper.split('\\').pop() : "Import Image..."}
            </button>
            {isGeneratingDepth && <p style={{color: 'var(--accent)', marginTop: '0.75rem', fontSize: '0.85rem'}}>Generating ML Depth Map...</p>}
            {depthError && <p style={{color: 'var(--danger)', marginTop: '0.75rem', fontSize: '0.85rem'}}>{depthError}</p>}
          </div>
          <div className="control-group">
            <label>Parallax Strength ({parallaxStrength.toFixed(3)})</label>
            <input type="range" min="0.01" max="0.2" step="0.01" value={parallaxStrength} onChange={(e) => {
              const val = parseFloat(e.target.value);
              setParallaxStrength(val);
              if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
              debounceTimer.current = window.setTimeout(() => {
                setSetting('parallaxStrength', val);
                saveEffectSettings('depth_parallax', { parallaxStrength: val });
                if (testWallpaper) saveWallpaperPairing(testWallpaper, 'depth_parallax', { parallaxStrength: val });
              }, 50);
            }} />
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateDepthParallax} disabled={!testWallpaper}>
              {activeEffect === 'depth_parallax' ? 'Re-Apply Changes' : 'Activate Effect'}
            </button>
          </div>
        </div>
      );
    }
    return (
      <div style={{padding: '2rem', textAlign: 'center', color: 'var(--text-secondary)'}}>
        <div style={{fontSize: '3rem', opacity: 0.2, marginBottom: '1rem'}}>✧</div>
        <p>No effect currently selected.</p>
        <p style={{fontSize: '0.9rem'}}>Select an effect from the gallery below to configure it.</p>
      </div>
    );
  };

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '2rem' }}>
        <h2 className="page-title" style={{margin: 0}}>Effects Studio</h2>
        <button className="danger" onClick={handleRemoveEffect} disabled={!activeEffect}>
          Stop Wallpaper
        </button>
      </div>
      
      {renderQualityTier()}

      {/* Hero Section: Currently Configured Effect */}
      <div className="card" style={{border: selectedEffect ? '1px solid var(--accent)' : '1px solid var(--border-color)', boxShadow: selectedEffect ? '0 8px 32px var(--accent-glow)' : ''}}>
        <div style={{display: 'flex', alignItems: 'center', gap: '0.75rem', marginBottom: '1.5rem'}}>
          <div style={{width: '8px', height: '24px', backgroundColor: selectedEffect ? 'var(--accent)' : 'var(--text-secondary)', borderRadius: '4px'}}></div>
          <h2 style={{margin: 0}}>{selectedEffect ? selectedEffect.replace('_', ' ').replace(/\b\w/g, l => l.toUpperCase()) : 'Selected Effect'}</h2>
          
          {selectedEffect && activeEffect === selectedEffect && (
            <span style={{marginLeft: 'auto', padding: '0.25rem 0.75rem', background: 'rgba(0, 240, 255, 0.1)', color: 'var(--accent)', borderRadius: '20px', fontSize: '0.85rem', fontWeight: 600, border: '1px solid rgba(0,240,255,0.2)'}}>
              ● ACTIVE
            </span>
          )}
        </div>
        {renderActiveEffectControls()}
      </div>

      {/* Browse Effects Grid */}
      <h3 style={{marginTop: '3rem', marginBottom: '1.5rem', fontWeight: 600}}>Browse Effects</h3>
      <div className="effect-grid">
        <div className={`effect-card ${selectedEffect === 'cursor_reveal' ? 'active' : ''}`} onClick={() => setSelectedEffect('cursor_reveal')}>
          <div className="effect-card-thumb">
            <img src="https://images.unsplash.com/photo-1550684848-fac1c5b4e853?q=80&w=600&auto=format&fit=crop" alt="Cursor Reveal" />
            {activeEffect === 'cursor_reveal' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
          </div>
          <div className="effect-card-content">
            <h3>Cursor Reveal</h3>
            <p>Reveals a hidden wallpaper layer beneath your cursor with a glowing brush and trailing path.</p>
          </div>
        </div>

        <div className={`effect-card ${selectedEffect === 'gravity_lens' ? 'active' : ''}`} onClick={() => setSelectedEffect('gravity_lens')}>
          <div className="effect-card-thumb">
            <img src="https://images.unsplash.com/photo-1462331940025-496dfbfc7564?q=80&w=600&auto=format&fit=crop" alt="Gravity Lens" />
            {activeEffect === 'gravity_lens' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
          </div>
          <div className="effect-card-content">
            <h3>Gravity Lens</h3>
            <p>The cursor acts as a localized gravitational lens, warping nearby pixels like a black hole or liquid ripple.</p>
          </div>
        </div>

        <div className={`effect-card ${selectedEffect === 'stone_press' ? 'active' : ''}`} onClick={() => setSelectedEffect('stone_press')}>
          <div className="effect-card-thumb">
            <img src="https://images.unsplash.com/photo-1518640467707-6811f4a6ab73?q=80&w=600&auto=format&fit=crop" alt="Stone Press" />
            {activeEffect === 'stone_press' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
          </div>
          <div className="effect-card-content">
            <h3>Stone Press</h3>
            <p>The cursor presses inward like a heavy stone on fabric, creating a concave dimple.</p>
          </div>
        </div>

        <div className={`effect-card ${selectedEffect === 'depth_parallax' ? 'active' : ''}`} onClick={() => setSelectedEffect('depth_parallax')}>
          <div className="effect-card-thumb">
            <img src="https://images.unsplash.com/photo-1478760329108-5c3ed9d495a0?q=80&w=600&auto=format&fit=crop" alt="Depth Parallax" />
            {activeEffect === 'depth_parallax' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
          </div>
          <div className="effect-card-content">
            <h3>Depth Parallax</h3>
            <p>Uses Machine Learning to automatically generate a 3D depth map from any 2D image for mouse parallax.</p>
          </div>
        </div>
      </div>
    </div>
  );
}
