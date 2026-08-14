import os

filepath = r'C:\My_Proj\InteractWall\ui\src\pages\Effects.tsx'
with open(filepath, 'r', encoding='utf-8') as f:
    code = f.read()

search_code = """        const spSettings = await loadEffectSettings('gravity_lens_transparent');
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
          const ss = Math.min(spSettings.shadingStrength ?? 0.7, 1.0);
          setgltDepth(depth);
          setgltRadius(rad);
          setgltStiffness(st);
          setgltDamping(dm);
          setgltDispersion(di);
          setgltDarkening(cd);
          setgltTrailLength(gt);
          setgltFadeDecay(fd);
          setgltShading(ss);
          // NOTE: Do NOT push settings to backend here – setSetting goes to
          // whichever plugin is currently active, NOT necessarily gravity_lens_transparent.
          // Settings are pushed in the activation function instead.
        }
      } catch (err) {"""

replace_code = """        const spSettings = await loadEffectSettings('gravity_lens_transparent');
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
          const ss = Math.min(spSettings.shadingStrength ?? 0.7, 1.0);
          setgltDepth(depth);
          setgltRadius(rad);
          setgltStiffness(st);
          setgltDamping(dm);
          setgltDispersion(di);
          setgltDarkening(cd);
          setgltTrailLength(gt);
          setgltFadeDecay(fd);
          setgltShading(ss);
        }

        const sp2Settings = await loadEffectSettings('stone_press_v2');
        if (sp2Settings) {
          setsp2Depth(sp2Settings.pressDepth ?? 1.5);
          setsp2Radius(sp2Settings.pressRadius ?? 0.08);
          setsp2Stiffness(sp2Settings.stiffness ?? 100.0);
          setsp2Damping(sp2Settings.damping ?? 0.90);
          setsp2Darkening(sp2Settings.depthDarkening ?? 0.8);
          setsp2DirectionalShading(sp2Settings.directionalShading ?? 0.7);
          setsp2ParallaxStrength(sp2Settings.parallaxStrength ?? 0.05);
        }
      } catch (err) {"""

code = code.replace(search_code, replace_code)

with open(filepath, 'w', encoding='utf-8') as f:
    f.write(code)

