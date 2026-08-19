import { useState, useRef, useEffect } from 'react';
import { setEffect, removeEffect, quitRenderer, setSetting, setQualityTier, applyWallpaper, importWallpaper, generateDepthMap, getStatus, listWallpapers } from '../ipc';
import { saveWallpaperPairing, loadEffectSettings, saveEffectSettings, saveActiveSession, getActiveSession } from '../store';
import { open } from '@tauri-apps/plugin-dialog';
import { convertFileSrc } from '@tauri-apps/api/core';

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
  const [isGalleryCollage, setIsGalleryCollage] = useState(false);

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

  // Gravity Lens Transparent State
  const [gltBaseImage, setgltBaseImage] = useState<string | null>(null);
  const [gltDepth, setgltDepth] = useState(0.03);
  const [gltRadius, setgltRadius] = useState(0.08);
  const [gltStiffness, setgltStiffness] = useState(50.0);
  const [gltDamping, setgltDamping] = useState(0.90);
  const [gltDispersion, setgltDispersion] = useState(0.02);
  const [gltDarkening, setgltDarkening] = useState(0.15);
  const [gltShading, setgltShading] = useState(0.5);
  const [gltTrailLength, setgltTrailLength] = useState(1.0);
  const [gltFadeDecay, setgltFadeDecay] = useState(0.92);

  // Stone Press V2 (Space Ball) State
  const [sp2BaseImage, setsp2BaseImage] = useState<string | null>(null);
  const [sp2Depth, setsp2Depth] = useState(2.0);
  const [sp2Radius, setsp2Radius] = useState(0.3);
  const [sp2Stiffness, setsp2Stiffness] = useState(50.0);
  const [sp2Damping, setsp2Damping] = useState(0.90);
  const [sp2Darkening, setsp2Darkening] = useState(0.6);
  const [sp2DirectionalShading, setsp2DirectionalShading] = useState(0.4);
  const [sp2ParallaxStrength, setsp2ParallaxStrength] = useState(0.2);

  // Brick Outline State
  const [boBaseImage, setBOBaseImage] = useState<string | null>(null);
  const [boBrickWidth, setBOBrickWidth] = useState(100.0);
  const [boBrickHeight, setBOBrickHeight] = useState(50.0);
  const [boLineThickness, setBOLineThickness] = useState(3.0);
  const [boEffectRadius, setBOEffectRadius] = useState(0.20);
  const [boEdgeSoftness, setBOEdgeSoftness] = useState(0.10);
  const [boGlowIntensity, setBOGlowIntensity] = useState(1.0);
  const [boOutlineColor, setBOOutlineColor] = useState('#ffffff');

  useEffect(() => {
    const loadGlobals = async () => {
      try {
        const crSettings = await loadEffectSettings('cursor_reveal');
        if (crSettings) {
          setBrushSize(crSettings.brushSize ?? 160);
          setCRBrushHardness(crSettings.brushHardness ?? 0.2);
          setCRTrailLength(crSettings.trailLength ?? 1.0);
          setCRFadeSpeed(crSettings.fadeSpeed ?? 0.035);
          setCRFadeWhenResting((crSettings.fadeWhenResting ?? 1) === 1);
          setSetting('brushSize', crSettings.brushSize ?? 160);
          setSetting('brushHardness', crSettings.brushHardness ?? 0.2);
          setSetting('trailLength', crSettings.trailLength ?? 1.0);
          setSetting('fadeSpeed', crSettings.fadeSpeed ?? 0.035);
          setSetting('fadeWhenResting', crSettings.fadeWhenResting ?? 1);
        }

        const dpSettings = await loadEffectSettings('depth_parallax');
        if (dpSettings) {
          setParallaxStrength(dpSettings.parallaxStrength ?? 0.05);
          setSetting('parallaxStrength', dpSettings.parallaxStrength ?? 0.05);
        }

        const glSettings = await loadEffectSettings('gravity_lens');
        if (glSettings) {
          setGLStrength(glSettings.lensStrength ?? 5.0);
          setGLRadius(glSettings.lensRadius ?? 0.3);
          setGLStiffness(glSettings.stiffness ?? 50.0);
          setGLDamping(glSettings.damping ?? 0.90);
          setGLDispersion(glSettings.dispersion ?? 0.02);
          setGLDarkening(glSettings.coreDarkening ?? 0.5);
          setGLTrailLength(glSettings.trailLength ?? 1.0);
          setGLFadeDecay(glSettings.fadeDecay ?? 0.92);
        }

        const gltSettings = await loadEffectSettings('gravity_lens_transparent');
        if (gltSettings) {
          setgltDepth(gltSettings.pressDepth ?? 0.03);
          setgltRadius(gltSettings.pressRadius ?? 0.08);
          setgltStiffness(gltSettings.stiffness ?? 50.0);
          setgltDamping(gltSettings.damping ?? 0.90);
          setgltDispersion(gltSettings.dispersion ?? 0.02);
          setgltDarkening(gltSettings.coreDarkening ?? 0.15);
          setgltShading(gltSettings.shadingStrength ?? 0.5);
          setgltTrailLength(gltSettings.trailLength ?? 1.0);
          setgltFadeDecay(gltSettings.fadeDecay ?? 0.92);
        }

        const sp2Settings = await loadEffectSettings('stone_press_v2');
        if (sp2Settings) {
          setsp2Depth(sp2Settings.pressDepth ?? 2.0);
          setsp2Radius(sp2Settings.pressRadius ?? 0.3);
          setsp2Stiffness(sp2Settings.stiffness ?? 50.0);
          setsp2Damping(sp2Settings.damping ?? 0.90);
          setsp2Darkening(sp2Settings.depthDarkening ?? 0.6);
          setsp2DirectionalShading(sp2Settings.directionalShading ?? 0.4);
          setsp2ParallaxStrength(sp2Settings.parallaxStrength ?? 0.2);
        }

        const boSettings = await loadEffectSettings('brick_outline');
        if (boSettings) {
          setBOBrickWidth(boSettings.brickWidth ?? 100.0);
          setBOBrickHeight(boSettings.brickHeight ?? 50.0);
          setBOLineThickness(boSettings.lineThickness ?? 3.0);
          setBOEffectRadius(boSettings.effectRadius ?? 0.20);
          setBOEdgeSoftness(boSettings.edgeSoftness ?? 0.10);
          setBOGlowIntensity(boSettings.glowIntensity ?? 1.0);
          setBOOutlineColor(boSettings.outlineColor ?? '#ffffff');
        }

        const session = await getActiveSession();
        if (session) {
          setIsGalleryCollage(!!session.isGalleryCollage);
          if (session.isGalleryCollage && session.layerA) {
            setGLBaseImage(session.layerA);
            setgltBaseImage(session.layerA);
            setsp2BaseImage(session.layerA);
            setBOBaseImage(session.layerA);
            if (session.effect !== "cursor_reveal") {
              setLayerA(session.layerA);
            }
          }
          if (session.effect === "cursor_reveal") {
            setLayerA(session.layerA);
            setLayerB(session.layerB);
          }
        }
      } catch (err) {
        console.error("Failed to load global effect settings:", err);
      }
    };
    loadGlobals();

    const handleBeforeUnload = () => {
      if (debounceTimer.current) {
        window.clearTimeout(debounceTimer.current);
        debounceTimer.current = null;
      }
      
      const crSettings = {
        brushSize, brushHardness: crBrushHardness, trailLength: crTrailLength,
        fadeSpeed: crFadeSpeed, fadeWhenResting: crFadeWhenResting ? 1 : 0,
      };
      saveEffectSettings('cursor_reveal', crSettings);
      
      saveEffectSettings('depth_parallax', { parallaxStrength });

      const glSettings = {
        lensStrength: glStrength, lensRadius: glRadius, stiffness: glStiffness,
        damping: glDamping, dispersion: glDispersion, coreDarkening: glDarkening,
        trailLength: glTrailLength, fadeDecay: glFadeDecay,
      };
      saveEffectSettings('gravity_lens', glSettings);

      const gltSettings = {
        pressDepth: gltDepth, pressRadius: gltRadius, stiffness: gltStiffness,
        damping: gltDamping, dispersion: gltDispersion, coreDarkening: gltDarkening,
        shadingStrength: gltShading, trailLength: gltTrailLength, fadeDecay: gltFadeDecay,
      };
      saveEffectSettings('gravity_lens_transparent', gltSettings);

      const sp2Settings = {
        pressDepth: sp2Depth, pressRadius: sp2Radius, stiffness: sp2Stiffness,
        damping: sp2Damping, depthDarkening: sp2Darkening, directionalShading: sp2DirectionalShading,
        parallaxStrength: sp2ParallaxStrength
      };
      saveEffectSettings('stone_press_v2', sp2Settings);

      const boSettings = {
        brickWidth: boBrickWidth, brickHeight: boBrickHeight, lineThickness: boLineThickness,
        effectRadius: boEffectRadius, edgeSoftness: boEdgeSoftness, glowIntensity: boGlowIntensity,
        outlineColor: boOutlineColor
      };
      saveEffectSettings('brick_outline', boSettings);
    };

    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => window.removeEventListener('beforeunload', handleBeforeUnload);
  }, []);

  useEffect(() => {
    const fetchStatus = async () => {
      try {
        const status = await getStatus();
        if (status && status.activePlugin) {
          let uiName = status.activePlugin.replace('.dll', '');
          if (uiName === "stone_press_v2") uiName = "stone_press_v2";
          else if (uiName === "gravity_lens_transparent") uiName = "gravity_lens_transparent";
          else if (uiName === "gravity_lens") uiName = "gravity_lens";
          else if (uiName === "depth_parallax") uiName = "depth_parallax";
          else if (uiName === "cursor_reveal") uiName = "cursor_reveal";
          else if (uiName === "brick_outline") uiName = "brick_outline";
          else uiName = "none";
          
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

  useEffect(() => {
    // Removed strict isGalleryCollage lock on cursor_reveal
  }, [isGalleryCollage, selectedEffect]);

  const handleRemoveEffect = async () => {
    try {
      await removeEffect();
      setActiveEffect(null);
      await saveActiveSession(null);
    } catch (err: any) {
      console.error("Failed to remove effect", err);
      alert(`Failed to remove effect: ${err.toString()}`);
    }
  };

  const handleStopWallpaper = async () => {
    try {
      await quitRenderer();
      setActiveEffect(null);
      await saveActiveSession(null);
    } catch (err: any) {
      console.error("Failed to stop wallpaper", err);
      alert(`Failed to stop wallpaper: ${err.toString()}`);
    }
  };

  const activateDepthParallax = async () => {
    if (!testWallpaper) return;
    try {
      const depthImage = testWallpaper.replace(/\.[^/.]+$/, "") + "_depth.png";
      await applyWallpaper(testWallpaper, depthImage);
      await setEffect('depth_parallax');
      setActiveEffect('depth_parallax');
      setSelectedEffect('depth_parallax');
      await saveWallpaperPairing(testWallpaper, 'depth_parallax', { parallaxStrength });
      await saveActiveSession({ layerA: testWallpaper, layerB: depthImage, effect: 'depth_parallax', isGalleryCollage });
      await setSetting('parallaxStrength', parallaxStrength);
    } catch (err: any) {
      console.error("Failed to activate depth parallax", err);
      alert(`Failed to activate effect: ${err.toString()}`);
    }
  };



  const [layerA, setLayerA] = useState<string | null>(null);
  const [layerB, setLayerB] = useState<string | null>(null);

  const handleQualityChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    setQuality(e.target.value);
    setQualityTier(e.target.value);
  };

  const handleCRSettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => setSetting(key, val), 50);
  };

  const [showImportPickerFor, setShowImportPickerFor] = useState<{setter: React.Dispatch<React.SetStateAction<string | null>>, effectType: string, onComplete?: (path: string) => void} | null>(null);
  const [pickerWallpapers, setPickerWallpapers] = useState<string[]>([]);
  const [pickerSource, setPickerSource] = useState<'options' | 'gallery'>('options');

  const handleImport = async (setLayer: React.Dispatch<React.SetStateAction<string | null>>, effectType: string, onComplete?: (path: string) => void) => {
    setShowImportPickerFor({setter: setLayer, effectType, onComplete});
    setPickerSource('options');
    try {
      const list = await listWallpapers();
      setPickerWallpapers(list);
    } catch (err) {
      setPickerWallpapers([]);
    }
  };

  const executeNativeImport = async (setLayer: React.Dispatch<React.SetStateAction<string | null>>, onComplete?: (path: string) => void) => {
    const selected = await open({
      multiple: false,
      filters: [{ name: 'Image', extensions: ['png', 'jpeg', 'jpg', 'webp', 'bmp'] }]
    });
    if (selected && typeof selected === 'string') {
      try {
        const newPath = await importWallpaper(selected);
        setLayer(newPath);
        onComplete?.(newPath);
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
      await saveActiveSession({ layerA, layerB, effect: 'cursor_reveal', isGalleryCollage });
      await setSetting('brushSize', brushSize);
      await setSetting('brushHardness', crBrushHardness);
      await setSetting('trailLength', crTrailLength);
      await setSetting('fadeSpeed', crFadeSpeed);
      await setSetting('fadeWhenResting', crFadeWhenResting ? 1 : 0);
    } catch (err: any) {
      alert(`Failed to activate effect: ${err.toString()}`);
    }
  };

  const activateGravityLens = async () => {
    if (!glBaseImage) return;
    try {
      await applyWallpaper(glBaseImage, ""); 
      await setEffect('gravity_lens');
      setActiveEffect('gravity_lens');
      setSelectedEffect('gravity_lens');
      await saveActiveSession({ layerA: glBaseImage, layerB: "", effect: 'gravity_lens', isGalleryCollage });
      await setSetting('lensStrength', glStrength);
      await setSetting('lensRadius', glRadius);
      await setSetting('stiffness', glStiffness);
      await setSetting('damping', glDamping);
      await setSetting('dispersion', glDispersion);
      await setSetting('coreDarkening', glDarkening);
      await setSetting('trailLength', glTrailLength);
      await setSetting('fadeDecay', glFadeDecay);
    } catch (err) {}
  };

  const activateGravityLensTransparent = async () => {
    if (!gltBaseImage) return;
    try {
      await applyWallpaper(gltBaseImage, ""); 
      await setEffect('gravity_lens_transparent');
      setActiveEffect('gravity_lens_transparent');
      setSelectedEffect('gravity_lens_transparent');
      await saveActiveSession({ layerA: gltBaseImage, layerB: "", effect: 'gravity_lens_transparent', isGalleryCollage });
      await setSetting('pressDepth', gltDepth);
      await setSetting('pressRadius', gltRadius);
      await setSetting('stiffness', gltStiffness);
      await setSetting('damping', gltDamping);
      await setSetting('dispersion', gltDispersion);
      await setSetting('coreDarkening', gltDarkening);
      await setSetting('shadingStrength', gltShading);
      await setSetting('trailLength', gltTrailLength);
      await setSetting('fadeDecay', gltFadeDecay);
    } catch (err) {}
  };

  const activateStonePressV2 = async () => {
    if (!sp2BaseImage) return;
    try {
      await applyWallpaper(sp2BaseImage, ""); 
      await setEffect('stone_press_v2');
      setActiveEffect('stone_press_v2');
      setSelectedEffect('stone_press_v2');
      await saveActiveSession({ layerA: sp2BaseImage, layerB: "", effect: 'stone_press_v2', isGalleryCollage });
      await setSetting('pressDepth', sp2Depth);
      await setSetting('pressRadius', sp2Radius);
      await setSetting('stiffness', sp2Stiffness);
      await setSetting('damping', sp2Damping);
      await setSetting('depthDarkening', sp2Darkening);
      await setSetting('directionalShading', sp2DirectionalShading);
      await setSetting('parallaxStrength', sp2ParallaxStrength);
    } catch (err) {}
  };

  const activateBrickOutline = async () => {
    if (!boBaseImage) return;
    try {
      await applyWallpaper(boBaseImage, ""); 
      await setEffect('brick_outline');
      setActiveEffect('brick_outline');
      setSelectedEffect('brick_outline');
      await saveActiveSession({ layerA: boBaseImage, layerB: "", effect: 'brick_outline', isGalleryCollage });
      await setSetting('brickWidth', boBrickWidth);
      await setSetting('brickHeight', boBrickHeight);
      await setSetting('lineThickness', boLineThickness);
      await setSetting('effectRadius', boEffectRadius);
      await setSetting('edgeSoftness', boEdgeSoftness);
      await setSetting('glowIntensity', boGlowIntensity);
    } catch (err) {}
  };

  const handleGLSettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => setSetting(key, val), 50);
  };
  const handleGLTSettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => setSetting(key, val), 50);
  };
  const handleSP2SettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => setSetting(key, val), 50);
  };
  const handleBOSettingChange = (key: string, val: number) => {
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => {
      setSetting(key, val);
    }, 50);
  };

  const handleBOColorChange = (hex: string) => {
    setBOOutlineColor(hex);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => {
      const r = parseInt(hex.slice(1, 3), 16) / 255.0;
      const g = parseInt(hex.slice(3, 5), 16) / 255.0;
      const b = parseInt(hex.slice(5, 7), 16) / 255.0;
      setSetting('outlineColorR', r);
      setSetting('outlineColorG', g);
      setSetting('outlineColorB', b);
    }, 50);
  };

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
              <button className="secondary" onClick={() => handleImport(setLayerA, 'cursor_reveal')} style={{width: '100%', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap'}}>
                {layerA ? layerA.split('\\').pop() : "Import Image..."}
              </button>
            </div>
            <div style={{flex: 1, padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)'}}>
              <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Layer B (Reveal)</h4>
              <button className="secondary" onClick={() => handleImport(setLayerB, 'cursor_reveal')} style={{width: '100%', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap'}}>
                {layerB ? layerB.split('\\').pop() : "Import Image..."}
              </button>
            </div>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group"><label>Brush Size ({brushSize}px)</label><input type="range" min="50" max="300" step="0.1" value={brushSize} onChange={(e) => handleCRSettingChange('brushSize', parseFloat(e.target.value), setBrushSize)} /></div>
            <div className="control-group"><label>Brush Hardness ({crBrushHardness.toFixed(2)})</label><input type="range" min="0.0" max="1.0" step="0.001" value={crBrushHardness} onChange={(e) => handleCRSettingChange('brushHardness', parseFloat(e.target.value), setCRBrushHardness)} /></div>
            <div className="control-group"><label>Trail Length ({crTrailLength.toFixed(1)}s)</label><input type="range" min="0.0" max="5.0" step="0.01" value={crTrailLength} onChange={(e) => handleCRSettingChange('trailLength', parseFloat(e.target.value), setCRTrailLength)} /></div>
            <div className="control-group"><label>Fade Out Speed ({crFadeSpeed.toFixed(3)})</label><input type="range" min="0.005" max="0.1" step="0.001" value={crFadeSpeed} onChange={(e) => handleCRSettingChange('fadeSpeed', parseFloat(e.target.value), setCRFadeSpeed)} /></div>
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
            <button className="secondary" onClick={() => handleImport(setGLBaseImage, 'gravity_lens')}>
              {glBaseImage ? glBaseImage.split('\\').pop() : "Import Image..."}
            </button>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group"><label>Lens Strength ({glStrength.toFixed(1)})</label><input type="range" min="0" max="20" step="0.01" value={glStrength} onChange={(e) => handleGLSettingChange('lensStrength', parseFloat(e.target.value), setGLStrength)} /></div>
            <div className="control-group"><label>Lens Radius ({glRadius.toFixed(2)})</label><input type="range" min="0.1" max="1.0" step="0.001" value={glRadius} onChange={(e) => handleGLSettingChange('lensRadius', parseFloat(e.target.value), setGLRadius)} /></div>
            <div className="control-group"><label>Spring Stiffness ({glStiffness.toFixed(0)})</label><input type="range" min="10" max="200" step="0.1" value={glStiffness} onChange={(e) => handleGLSettingChange('stiffness', parseFloat(e.target.value), setGLStiffness)} /></div>
            <div className="control-group"><label>Spring Damping ({glDamping.toFixed(2)})</label><input type="range" min="0.70" max="0.99" step="0.001" value={glDamping} onChange={(e) => handleGLSettingChange('damping', parseFloat(e.target.value), setGLDamping)} /></div>
            <div className="control-group"><label>Chromatic Dispersion ({glDispersion.toFixed(3)})</label><input type="range" min="0" max="0.1" step="0.001" value={glDispersion} onChange={(e) => handleGLSettingChange('dispersion', parseFloat(e.target.value), setGLDispersion)} /></div>
            <div className="control-group"><label>Trail Length ({glTrailLength.toFixed(1)}s)</label><input type="range" min="0" max="5" step="0.01" value={glTrailLength} onChange={(e) => handleGLSettingChange('trailLength', parseFloat(e.target.value), setGLTrailLength)} /></div>
            <div className="control-group"><label>Fade Speed ({glFadeDecay >= 0.99 ? 'Never' : glFadeDecay.toFixed(2)})</label><input type="range" min="0.80" max="1.0" step="0.001" value={glFadeDecay} onChange={(e) => handleGLSettingChange('fadeDecay', parseFloat(e.target.value), setGLFadeDecay)} /></div>
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateGravityLens} disabled={!glBaseImage}>
              {activeEffect === 'gravity_lens' ? 'Re-Apply Changes' : 'Activate Effect'}
            </button>
          </div>
        </div>
      );
    }
    if (selectedEffect === 'gravity_lens_transparent') {
      return (
        <div style={{animation: 'fadeIn 0.3s ease'}}>
          <div style={{padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', marginBottom: '1.5rem'}}>
            <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Base Wallpaper</h4>
            <button className="secondary" onClick={() => handleImport(setgltBaseImage, 'gravity_lens_transparent')}>
              {gltBaseImage ? gltBaseImage.split('\\').pop() : "Import Image..."}
            </button>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group"><label>Press Depth ({gltDepth.toFixed(3)})</label><input type="range" min="0" max="0.15" step="0.001" value={gltDepth} onChange={(e) => handleGLTSettingChange('pressDepth', parseFloat(e.target.value), setgltDepth)} /></div>
            <div className="control-group"><label>Press Radius ({gltRadius.toFixed(2)})</label><input type="range" min="0.02" max="0.25" step="0.001" value={gltRadius} onChange={(e) => handleGLTSettingChange('pressRadius', parseFloat(e.target.value), setgltRadius)} /></div>
            <div className="control-group"><label>Spring Stiffness ({gltStiffness.toFixed(0)})</label><input type="range" min="10" max="200" step="0.1" value={gltStiffness} onChange={(e) => handleGLTSettingChange('stiffness', parseFloat(e.target.value), setgltStiffness)} /></div>
            <div className="control-group"><label>Spring Damping ({gltDamping.toFixed(2)})</label><input type="range" min="0.70" max="0.99" step="0.001" value={gltDamping} onChange={(e) => handleGLTSettingChange('damping', parseFloat(e.target.value), setgltDamping)} /></div>
            <div className="control-group"><label>Chromatic Dispersion ({gltDispersion.toFixed(3)})</label><input type="range" min="0" max="0.1" step="0.001" value={gltDispersion} onChange={(e) => handleGLTSettingChange('dispersion', parseFloat(e.target.value), setgltDispersion)} /></div>
            <div className="control-group"><label>Core Darkening ({gltDarkening.toFixed(2)})</label><input type="range" min="0" max="1.0" step="0.001" value={gltDarkening} onChange={(e) => handleGLTSettingChange('coreDarkening', parseFloat(e.target.value), setgltDarkening)} /></div>
            <div className="control-group"><label>Directional Shading ({gltShading.toFixed(2)})</label><input type="range" min="0" max="1.0" step="0.001" value={gltShading} onChange={(e) => handleGLTSettingChange('shadingStrength', parseFloat(e.target.value), setgltShading)} /></div>
            <div className="control-group"><label>Trail Length ({gltTrailLength.toFixed(1)}s)</label><input type="range" min="0" max="5" step="0.01" value={gltTrailLength} onChange={(e) => handleGLTSettingChange('trailLength', parseFloat(e.target.value), setgltTrailLength)} /></div>
            <div className="control-group"><label>Fade Speed ({gltFadeDecay >= 0.99 ? 'Never' : gltFadeDecay.toFixed(2)})</label><input type="range" min="0.80" max="1.0" step="0.001" value={gltFadeDecay} onChange={(e) => handleGLTSettingChange('fadeDecay', parseFloat(e.target.value), setgltFadeDecay)} /></div>
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateGravityLensTransparent} disabled={!gltBaseImage}>
              {activeEffect === 'gravity_lens_transparent' ? 'Re-Apply Changes' : 'Activate Effect'}
            </button>
          </div>
        </div>
      );
    }
    if (selectedEffect === 'stone_press_v2') {
      return (
        <div style={{animation: 'fadeIn 0.3s ease'}}>
          <div style={{padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', marginBottom: '1.5rem'}}>
            <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Base Wallpaper</h4>
            <button className="secondary" onClick={() => handleImport(setsp2BaseImage, 'stone_press_v2')}>
              {sp2BaseImage ? sp2BaseImage.split('\\').pop() : "Import Image..."}
            </button>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group"><label>Press Depth ({sp2Depth.toFixed(2)})</label><input type="range" min="0" max="10.0" step="0.01" value={sp2Depth} onChange={(e) => handleSP2SettingChange('pressDepth', parseFloat(e.target.value), setsp2Depth)} /></div>
            <div className="control-group"><label>Press Radius ({sp2Radius.toFixed(2)})</label><input type="range" min="0.01" max="1.0" step="0.001" value={sp2Radius} onChange={(e) => handleSP2SettingChange('pressRadius', parseFloat(e.target.value), setsp2Radius)} /></div>
            <div className="control-group"><label>Spring Stiffness ({sp2Stiffness.toFixed(0)})</label><input type="range" min="10" max="300" step="0.1" value={sp2Stiffness} onChange={(e) => handleSP2SettingChange('stiffness', parseFloat(e.target.value), setsp2Stiffness)} /></div>
            <div className="control-group"><label>Spring Damping ({sp2Damping.toFixed(2)})</label><input type="range" min="0.70" max="0.99" step="0.001" value={sp2Damping} onChange={(e) => handleSP2SettingChange('damping', parseFloat(e.target.value), setsp2Damping)} /></div>
            <div className="control-group"><label>Depth Darkening ({sp2Darkening.toFixed(2)})</label><input type="range" min="0" max="1.0" step="0.001" value={sp2Darkening} onChange={(e) => handleSP2SettingChange('depthDarkening', parseFloat(e.target.value), setsp2Darkening)} /></div>
            <div className="control-group"><label>Directional Shading ({sp2DirectionalShading.toFixed(2)})</label><input type="range" min="0" max="1.0" step="0.001" value={sp2DirectionalShading} onChange={(e) => handleSP2SettingChange('directionalShading', parseFloat(e.target.value), setsp2DirectionalShading)} /></div>
            <div className="control-group"><label>Parallax Strength ({sp2ParallaxStrength.toFixed(2)})</label><input type="range" min="0" max="1.0" step="0.001" value={sp2ParallaxStrength} onChange={(e) => handleSP2SettingChange('parallaxStrength', parseFloat(e.target.value), setsp2ParallaxStrength)} /></div>
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateStonePressV2} disabled={!sp2BaseImage}>
              {activeEffect === 'stone_press_v2' ? 'Re-Apply Changes' : 'Activate Effect'}
            </button>
          </div>
        </div>
      );
    }
    if (selectedEffect === 'brick_outline') {
      return (
        <div style={{animation: 'fadeIn 0.3s ease'}}>
          <div style={{padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', marginBottom: '1.5rem'}}>
            <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Base Wallpaper</h4>
            <button className="secondary" onClick={() => handleImport(setBOBaseImage, 'brick_outline')}>
              {boBaseImage ? boBaseImage.split('\\').pop() : "Import Image..."}
            </button>
          </div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
            <div className="control-group"><label>Brick Width ({boBrickWidth.toFixed(1)})</label><input type="range" min="10" max="300" step="0.1" value={boBrickWidth} onChange={(e) => { const v = parseFloat(e.target.value); setBOBrickWidth(v); handleBOSettingChange('brickWidth', v); }} /></div>
            <div className="control-group"><label>Brick Height ({boBrickHeight.toFixed(1)})</label><input type="range" min="10" max="300" step="0.1" value={boBrickHeight} onChange={(e) => { const v = parseFloat(e.target.value); setBOBrickHeight(v); handleBOSettingChange('brickHeight', v); }} /></div>
            <div className="control-group"><label>Line Thickness ({boLineThickness.toFixed(1)})</label><input type="range" min="0.5" max="10" step="0.01" value={boLineThickness} onChange={(e) => { const v = parseFloat(e.target.value); setBOLineThickness(v); handleBOSettingChange('lineThickness', v); }} /></div>
            <div className="control-group"><label>Effect Radius ({boEffectRadius.toFixed(2)})</label><input type="range" min="0.01" max="1.0" step="0.001" value={boEffectRadius} onChange={(e) => { const v = parseFloat(e.target.value); setBOEffectRadius(v); handleBOSettingChange('effectRadius', v); }} /></div>
            <div className="control-group"><label>Edge Softness ({boEdgeSoftness.toFixed(2)})</label><input type="range" min="0.0" max="0.5" step="0.001" value={boEdgeSoftness} onChange={(e) => { const v = parseFloat(e.target.value); setBOEdgeSoftness(v); handleBOSettingChange('edgeSoftness', v); }} /></div>
            <div className="control-group"><label>Glow Intensity ({boGlowIntensity.toFixed(2)})</label><input type="range" min="0.0" max="3.0" step="0.01" value={boGlowIntensity} onChange={(e) => { const v = parseFloat(e.target.value); setBOGlowIntensity(v); handleBOSettingChange('glowIntensity', v); }} /></div>
            <div className="control-group" style={{ gridColumn: '1 / -1' }}><label>Outline Color</label><input type="color" value={boOutlineColor} onChange={(e) => handleBOColorChange(e.target.value)} style={{ width: '100%', height: '40px', padding: 0, border: 'none', background: 'transparent', cursor: 'pointer' }} /></div>
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateBrickOutline} disabled={!boBaseImage}>
              {activeEffect === 'brick_outline' ? 'Re-Apply Changes' : 'Activate Effect'}
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
            <button className="secondary" onClick={() => handleImport(setTestWallpaper, 'depth_parallax', async (newPath) => {
              setIsGeneratingDepth(true);
              setDepthError(null);
              try {
                await generateDepthMap(newPath);
              } catch (err: any) {
                setDepthError("Depth generation failed: " + err.toString());
              } finally {
                setIsGeneratingDepth(false);
              }
            })}>
              {testWallpaper ? testWallpaper.split('\\').pop() : "Import Image..."}
            </button>
            {isGeneratingDepth && <p style={{color: 'var(--accent)', marginTop: '0.75rem', fontSize: '0.85rem'}}>Generating ML Depth Map...</p>}
            {depthError && <p style={{color: 'var(--danger)', marginTop: '0.75rem', fontSize: '0.85rem'}}>{depthError}</p>}
          </div>
          <div className="control-group">
            <label>Parallax Strength ({parallaxStrength.toFixed(3)})</label>
            <input type="range" min="0.01" max="0.2" step="0.001" value={parallaxStrength} onChange={(e) => {
              const val = parseFloat(e.target.value);
              setParallaxStrength(val);
              if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
              debounceTimer.current = window.setTimeout(() => {
                setSetting('parallaxStrength', val);
              }, 50);
            }} />
          </div>
          <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
            <button className="primary" onClick={activateDepthParallax} disabled={!testWallpaper || isGeneratingDepth}>
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
        <div style={{ display: 'flex', gap: '1rem' }}>
          <button className="primary" onClick={handleRemoveEffect} disabled={!activeEffect}>
            Stop Effect
          </button>
          <button className="danger" onClick={handleStopWallpaper}>
            Stop Wallpaper
          </button>
        </div>
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
        <div 
          className={`effect-card ${selectedEffect === 'cursor_reveal' ? 'active' : ''}`} 
          onClick={() => { setSelectedEffect('cursor_reveal'); }}
          style={{ opacity: 1, cursor: 'pointer' }}
        >
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

        <div className={`effect-card ${selectedEffect === 'gravity_lens_transparent' ? 'active' : ''}`} onClick={() => setSelectedEffect('gravity_lens_transparent')}>
          <div className="effect-card-thumb">
            <img src="https://images.unsplash.com/photo-1518640467707-6811f4a6ab73?q=80&w=600&auto=format&fit=crop" alt="Gravity Lens - Transparent" />
            {activeEffect === 'gravity_lens_transparent' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
          </div>
          <div className="effect-card-content">
            <h3>Gravity Lens - Transparent</h3>
            <p>The cursor presses inward like a heavy stone on fabric, creating a concave dimple.</p>
          </div>
        </div>

        <div className={`effect-card ${selectedEffect === 'stone_press_v2' ? 'active' : ''}`} onClick={() => setSelectedEffect('stone_press_v2')}>
          <div className="effect-card-thumb">
            <img src="https://images.unsplash.com/photo-1518640467707-6811f4a6ab73?q=80&w=600&auto=format&fit=crop" alt="Space Ball" />
            {activeEffect === 'stone_press_v2' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
          </div>
          <div className="effect-card-content">
            <h3>Space Ball</h3>
            <p>The cursor presses inward like a heavy stone on fabric, using physical height-field simulation.</p>
          </div>
        </div>

        <div className={`effect-card ${selectedEffect === 'brick_outline' ? 'active' : ''}`} onClick={() => setSelectedEffect('brick_outline')}>
          <div className="effect-card-thumb">
            <img src="https://images.unsplash.com/photo-1518640467707-6811f4a6ab73?q=80&w=600&auto=format&fit=crop" alt="Brick Outline" />
            {activeEffect === 'brick_outline' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
          </div>
          <div className="effect-card-content">
            <h3>Brick Outline</h3>
            <p>Glowing procedural running-bond brick pattern overlaid on the wallpaper.</p>
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

      {showImportPickerFor && (
        <div 
          className="modal-overlay" 
          onClick={() => setShowImportPickerFor(null)}
          style={{
            position: 'fixed',
            top: 0, left: 0, right: 0, bottom: 0,
            backgroundColor: 'rgba(0,0,0,0.85)',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            zIndex: 9999,
            padding: '2rem'
          }}
        >
          <div 
            className="modal-content picker-modal" 
            onClick={e => e.stopPropagation()} 
            style={{
              maxWidth: '600px', 
              width: '100%',
              backgroundColor: 'var(--bg)',
              borderRadius: '12px',
              border: '1px solid rgba(255,255,255,0.1)',
              padding: '2rem',
              boxShadow: '0 20px 40px rgba(0,0,0,0.5)'
            }}
          >
            <h2 style={{marginTop: 0}}>Select Wallpaper Source</h2>
            
            <div style={{display: 'flex', gap: '1rem', marginBottom: '1.5rem'}}>
              <button 
                className={pickerSource === 'options' ? 'primary' : 'secondary'} 
                onClick={() => setPickerSource('options')}
                style={{flex: 1}}
              >
                File Explorer
              </button>
              <button 
                className={pickerSource === 'gallery' ? 'primary' : 'secondary'} 
                onClick={() => setPickerSource('gallery')}
                style={{flex: 1}}
              >
                My Baked Wallpapers
              </button>
            </div>

            {pickerSource === 'options' && (
              <div style={{textAlign: 'center', padding: '2rem'}}>
                <button className="primary" onClick={async () => {
                  await executeNativeImport(showImportPickerFor.setter, showImportPickerFor.onComplete);
                  setShowImportPickerFor(null);
                }}>
                  Browse Local Files...
                </button>
              </div>
            )}

            {pickerSource === 'gallery' && (
              <div className="wallpapers-grid" style={{display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(120px, 1fr))', gap: '1rem', maxHeight: '400px', overflowY: 'auto'}}>
                {pickerWallpapers.length === 0 ? (
                  <p style={{gridColumn: '1/-1', textAlign: 'center', color: 'rgba(255,255,255,0.5)'}}>No baked wallpapers found.</p>
                ) : (
                  pickerWallpapers.map((wp, idx) => (
                    <div key={idx} style={{position: 'relative', cursor: 'pointer', borderRadius: '8px', overflow: 'hidden', border: '2px solid transparent'}}
                         onClick={() => {
                           showImportPickerFor.setter(wp);
                           showImportPickerFor.onComplete?.(wp);
                           setShowImportPickerFor(null);
                         }}
                         onMouseEnter={e => e.currentTarget.style.borderColor = 'var(--accent)'}
                         onMouseLeave={e => e.currentTarget.style.borderColor = 'transparent'}
                    >
                      <img src={convertFileSrc(wp)} style={{width: '100%', aspectRatio: '16/9', objectFit: 'cover', display: 'block'}} />
                    </div>
                  ))
                )}
              </div>
            )}
            
            <div style={{marginTop: '1.5rem', textAlign: 'right'}}>
              <button className="secondary" onClick={() => setShowImportPickerFor(null)}>Cancel</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
