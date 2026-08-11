import { useState, useEffect } from 'react';
import { open } from '@tauri-apps/plugin-dialog';
import { importWallpaper, listWallpapers, applyWallpaper, setEffect, setSetting } from '../ipc';
import { getWallpaperPairing } from '../store';

export default function WallpaperManager() {
  const [importedImages, setImportedImages] = useState<string[]>([]);

  useEffect(() => {
    listWallpapers().then(setImportedImages).catch(console.error);
  }, []);

  async function handleImport() {
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
        setImportedImages(prev => [...prev, newPath]);
      } catch (err) {
        console.error("Failed to import wallpaper:", err);
      }
    }
  }

  const handleThumbnailClick = async (path: string) => {
    const pairing = await getWallpaperPairing(path);
    if (pairing) {
      if (confirm(`Restore previous effect settings (${pairing.effect})?`)) {
        try {
          if (pairing.effect === 'depth_parallax') {
             const depthImage = path.replace(/\.[^/.]+$/, "") + "_depth.png";
             await applyWallpaper(path, depthImage);
          } else if (pairing.effect === 'gravity_lens' || pairing.effect === 'cursor_reveal') {
             await applyWallpaper(path, "");
          }
          await setEffect(pairing.effect);
          for (const [key, val] of Object.entries(pairing.settings)) {
             await setSetting(key, val);
          }
        } catch (err) {
          console.error("Failed to restore pairing:", err);
        }
      }
    } else {
      alert("No previous effect settings found for this wallpaper. Please assign it in the Effects tab first to create a pairing.");
    }
  };

  return (
    <div>
      <h2 className="page-title">Wallpaper Gallery</h2>
      <div className="card">
        <p style={{marginBottom: "1rem"}}>Imported images are stored securely. You can assign them within the specific Effects tabs.</p>
        <button className="primary" onClick={handleImport}>Import to Gallery...</button>
      </div>

      <h3>Imported Wallpapers</h3>
      {importedImages.length === 0 ? (
        <p style={{color: 'var(--text-secondary)'}}>No wallpapers imported yet.</p>
      ) : (
        <div className="thumbnail-grid">
          {importedImages.map((path, i) => (
            <div 
              key={i} 
              className="thumbnail-card"
              onClick={() => handleThumbnailClick(path)}
              style={{ cursor: 'pointer' }}
            >
              <div style={{padding: '1rem', wordBreak: 'break-all', fontSize: '0.8rem', textAlign: 'center'}}>
                {path.split('\\').pop()}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
