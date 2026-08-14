const fs = require('fs');
let code = fs.readFileSync('ui/src/pages/Effects.tsx', 'utf8');

// 1. Add beforeunload flush
const beforeUnloadPoint = "        if (gltBaseImage) saveWallpaperPairing(gltBaseImage, 'gravity_lens_transparent', spSettings);";
if (!code.includes("saveEffectSettings('stone_press_v2', sp2Settings);")) {
    const sp2Unload = 
        const sp2Settings = {
          pressDepth: sp2Depth, pressRadius: sp2Radius, stiffness: sp2Stiffness,
          damping: sp2Damping, depthDarkening: sp2Darkening, directionalShading: sp2DirectionalShading, parallaxStrength: sp2ParallaxStrength
        };
        saveEffectSettings('stone_press_v2', sp2Settings);
        if (sp2BaseImage) saveWallpaperPairing(sp2BaseImage, 'stone_press_v2', sp2Settings);;
    code = code.replace(beforeUnloadPoint, beforeUnloadPoint + sp2Unload);
}

// 2. Add State block after Gravity Lens - Transparent State block
const stateBlockPoint = "saveWallpaperPairing(gltBaseImage, 'gravity_lens_transparent', newGLTSettings);\n          }\n      }, 50);\n    };";
if (!code.includes("// Stone Press V2 State")) {
    const sp2State = 

  // Stone Press V2 State
  const [sp2BaseImage, setsp2BaseImage] = useState<string | null>(null);
  const [sp2Depth, setsp2Depth] = useState(1.5);
  const [sp2Radius, setsp2Radius] = useState(0.08);
  const [sp2Stiffness, setsp2Stiffness] = useState(100.0);
  const [sp2Damping, setsp2Damping] = useState(0.90);
  const [sp2Darkening, setsp2Darkening] = useState(0.8);
  const [sp2DirectionalShading, setsp2DirectionalShading] = useState(0.7);
  const [sp2ParallaxStrength, setsp2ParallaxStrength] = useState(20.0);

  const activateStonePressV2 = async () => {
    if (!sp2BaseImage) return;
    try {
      await applyWallpaper(sp2BaseImage, "");
      await setEffect('stone_press_v2');
      setActiveEffect('stone_press_v2');
      setSelectedEffect('stone_press_v2');
      const sp2Settings = {
        pressDepth: sp2Depth, pressRadius: sp2Radius, stiffness: sp2Stiffness,
        damping: sp2Damping, depthDarkening: sp2Darkening, directionalShading: sp2DirectionalShading, parallaxStrength: sp2ParallaxStrength
      };
      await saveEffectSettings('stone_press_v2', sp2Settings);
      await saveWallpaperPairing(sp2BaseImage, 'stone_press_v2', sp2Settings);
      await saveActiveSession({ layerA: sp2BaseImage, layerB: "", effect: 'stone_press_v2' });
      for (const [k, v] of Object.entries(sp2Settings)) {
        await setSetting(k, v);
      }
    } catch (err) {
      console.error("Failed to activate Stone Press V2", err);
    }
  };

  const handleSP2SettingChange = (key: string, val: number, setter: React.Dispatch<React.SetStateAction<number>>) => {
    setter(val);
    if (debounceTimer.current) window.clearTimeout(debounceTimer.current);
    debounceTimer.current = window.setTimeout(() => {
        setSetting(key, val);
        const newSP2Settings = {
          pressDepth: key === 'pressDepth' ? val : sp2Depth,
          pressRadius: key === 'pressRadius' ? val : sp2Radius,
          stiffness: key === 'stiffness' ? val : sp2Stiffness,
          damping: key === 'damping' ? val : sp2Damping,
          depthDarkening: key === 'depthDarkening' ? val : sp2Darkening,
          directionalShading: key === 'directionalShading' ? val : sp2DirectionalShading,
          parallaxStrength: key === 'parallaxStrength' ? val : sp2ParallaxStrength,
        };
        saveEffectSettings('stone_press_v2', newSP2Settings);
        if (sp2BaseImage) {
          saveWallpaperPairing(sp2BaseImage, 'stone_press_v2', newSP2Settings);
        }
    }, 50);
  };;
    code = code.replace(stateBlockPoint, stateBlockPoint + sp2State);
}

// 3. Add Control UI block
const uiControlPoint =             <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
              <button className="primary" onClick={activateGravityLensTransparent} disabled={!gltBaseImage}>
                {activeEffect === 'gravity_lens_transparent' ? 'Re-Apply Changes' : 'Activate Effect'}
              </button>
            </div>
          </div>
        );
      };
if (!code.includes("selectedEffect === 'stone_press_v2'")) {
    const sp2Control = 
      if (selectedEffect === 'stone_press_v2') {
        return (
          <div style={{animation: 'fadeIn 0.3s ease'}}>
            <div style={{padding: '1.25rem', background: 'rgba(0,0,0,0.4)', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', marginBottom: '1.5rem'}}>
              <h4 style={{marginTop: 0, marginBottom: '0.75rem', fontWeight: 500}}>Base Wallpaper</h4>
              <button className="secondary" onClick={() => handleImport(setsp2BaseImage)}>
                {sp2BaseImage ? sp2BaseImage.split('\\\\').pop() : "Import Image..."}
              </button>
            </div>
            <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0 2rem'}}>
              <div className="control-group"><label>Press Depth ({sp2Depth.toFixed(2)})</label><input type="range" min="0" max="3.0" step="0.05" value={sp2Depth} onChange={(e) => handleSP2SettingChange('pressDepth', parseFloat(e.target.value), setsp2Depth)} /></div>
              <div className="control-group"><label>Press Radius ({sp2Radius.toFixed(2)})</label><input type="range" min="0.01" max="0.25" step="0.01" value={sp2Radius} onChange={(e) => handleSP2SettingChange('pressRadius', parseFloat(e.target.value), setsp2Radius)} /></div>
              <div className="control-group"><label>Spring Stiffness ({sp2Stiffness.toFixed(0)})</label><input type="range" min="10" max="300" step="1" value={sp2Stiffness} onChange={(e) => handleSP2SettingChange('stiffness', parseFloat(e.target.value), setsp2Stiffness)} /></div>
              <div className="control-group"><label>Spring Damping ({sp2Damping.toFixed(2)})</label><input type="range" min="0.70" max="0.99" step="0.01" value={sp2Damping} onChange={(e) => handleSP2SettingChange('damping', parseFloat(e.target.value), setsp2Damping)} /></div>
              <div className="control-group"><label>Depth Darkening ({sp2Darkening.toFixed(2)})</label><input type="range" min="0" max="2.0" step="0.05" value={sp2Darkening} onChange={(e) => handleSP2SettingChange('depthDarkening', parseFloat(e.target.value), setsp2Darkening)} /></div>
              <div className="control-group"><label>Directional Shading ({sp2DirectionalShading.toFixed(2)})</label><input type="range" min="0" max="2.0" step="0.05" value={sp2DirectionalShading} onChange={(e) => handleSP2SettingChange('directionalShading', parseFloat(e.target.value), setsp2DirectionalShading)} /></div>
              <div className="control-group"><label>Parallax Strength ({sp2ParallaxStrength.toFixed(1)})</label><input type="range" min="0" max="50" step="0.5" value={sp2ParallaxStrength} onChange={(e) => handleSP2SettingChange('parallaxStrength', parseFloat(e.target.value), setsp2ParallaxStrength)} /></div>
            </div>
            <div style={{display: 'flex', justifyContent: 'flex-end', marginTop: '1rem'}}>
              <button className="primary" onClick={activateStonePressV2} disabled={!sp2BaseImage}>
                {activeEffect === 'stone_press_v2' ? 'Re-Apply Changes' : 'Activate Effect'}
              </button>
            </div>
          </div>
        );
      };
    code = code.replace(uiControlPoint, uiControlPoint + sp2Control);
}

// 4. Add Effect Card
const cardPoint =             <div className="effect-card-content">
              <h3>Gravity Lens - Transparent</h3>
              <p>The cursor presses inward like a heavy stone on fabric, creating a concave dimple.</p>
            </div>
          </div>;
if (!code.includes("setSelectedEffect('stone_press_v2')")) {
    const sp2Card = 

          <div className={\effect-card \\} onClick={() => setSelectedEffect('stone_press_v2')}>
            <div className="effect-card-thumb">
              <img src="https://images.unsplash.com/photo-1518640467707-6811f4a6ab73?q=80&w=600&auto=format&fit=crop" alt="Stone Press" />
              {activeEffect === 'stone_press_v2' && <div style={{position: 'absolute', top: '10px', right: '10px', background: 'var(--accent)', color: '#000', padding: '0.15rem 0.5rem', borderRadius: '4px', fontSize: '0.75rem', fontWeight: 700}}>RUNNING</div>}
            </div>
            <div className="effect-card-content">
              <h3>Stone Press V2</h3>
              <p>The cursor presses inward like a heavy stone on fabric, using height field simulation.</p>
            </div>
          </div>;
    code = code.replace(cardPoint, cardPoint + sp2Card);
}

fs.writeFileSync('ui/src/pages/Effects.tsx', code);
console.log("Updated Effects.tsx");
