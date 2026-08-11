import { load, Store } from '@tauri-apps/plugin-store';
import { setSetting, setSettingStr } from './ipc';

let storeInstance: Store | null = null;

async function getStore(): Promise<Store> {
    if (!storeInstance) {
        storeInstance = await load('settings.json', { autoSave: false });
    }
    return storeInstance;
}

export interface EffectSettings {
    cursor_reveal: {
        brushSize: number;
        brushHardness: number;
        trailLength: number;
        fadeSpeed: number;
        fadeWhenResting: number;
    };
    depth_parallax: {
        parallaxStrength: number;
    };
    gravity_lens: {
        lensStrength: number;
        lensRadius: number;
        stiffness: number;
        damping: number;
        dispersion: number;
        coreDarkening: number;
        trailLength: number;
        fadeDecay: number;
    };
    stone_press: {
        pressDepth: number;
        pressRadius: number;
        stiffness: number;
        damping: number;
        dispersion: number;
        coreDarkening: number;
        trailLength: number;
        fadeDecay: number;
    };
}

export interface AppSettings {
    idleTimeout: number; // 0 = disabled
    pauseHidden: boolean;
    pauseFullscreen: boolean;
    pauseBattery: boolean;
    pauseSessionLocked: boolean;
    preferredGPU: string;
}

export const defaultSettings: AppSettings = {
    idleTimeout: 60, // 60 seconds
    pauseHidden: true,
    pauseFullscreen: true,
    pauseBattery: false,
    pauseSessionLocked: true,
    preferredGPU: 'Automatic',
};

export async function loadSettings(): Promise<AppSettings> {
    const store = await getStore();
    const idleTimeout = await store.get<number>('idleTimeout');
    const pauseHidden = await store.get<boolean>('pauseHidden');
    const pauseFullscreen = await store.get<boolean>('pauseFullscreen');
    const pauseBattery = await store.get<boolean>('pauseBattery');
    const pauseSessionLocked = await store.get<boolean>('pauseSessionLocked');
    const preferredGPU = await store.get<string>('preferredGPU');

    return {
        idleTimeout: idleTimeout ?? defaultSettings.idleTimeout,
        pauseHidden: pauseHidden ?? defaultSettings.pauseHidden,
        pauseFullscreen: pauseFullscreen ?? defaultSettings.pauseFullscreen,
        pauseBattery: pauseBattery ?? defaultSettings.pauseBattery,
        pauseSessionLocked: pauseSessionLocked ?? defaultSettings.pauseSessionLocked,
        preferredGPU: preferredGPU ?? defaultSettings.preferredGPU,
    };
}

export async function saveSettings(settings: AppSettings) {
    const store = await getStore();
    await store.set('idleTimeout', settings.idleTimeout);
    await store.set('pauseHidden', settings.pauseHidden);
    await store.set('pauseFullscreen', settings.pauseFullscreen);
    await store.set('pauseBattery', settings.pauseBattery);
    await store.set('pauseSessionLocked', settings.pauseSessionLocked);
    
    // Check if GPU changed, and only apply if it did, since it triggers restart
    const oldGPU = await store.get<string>('preferredGPU');
    await store.set('preferredGPU', settings.preferredGPU);
    
    await store.save();
    
    await applySettingsToBackend(settings, oldGPU !== settings.preferredGPU);
}

export async function applySettingsToBackend(settings: AppSettings, gpuChanged = false) {
    await setSetting('engine.idleTimeout', settings.idleTimeout);
    await setSetting('engine.pauseHidden', settings.pauseHidden ? 1 : 0);
    await setSetting('engine.pauseFullscreen', settings.pauseFullscreen ? 1 : 0);
    await setSetting('engine.pauseBattery', settings.pauseBattery ? 1 : 0);
    await setSetting('engine.pauseSessionLocked', settings.pauseSessionLocked ? 1 : 0);
    
    if (gpuChanged) {
        // This will restart the renderer!
        await setSettingStr('engine.preferredGPU', settings.preferredGPU);
    }
}

export async function resetToDefaults() {
    const store = await getStore();
    await store.clear();
    await store.save();
    await applySettingsToBackend(defaultSettings, true); // true to force restart and apply default GPU
}

export interface WallpaperPairing {
    effect: string;
    settings: Record<string, number>;
}

export async function getWallpaperPairing(wallpaperPath: string): Promise<WallpaperPairing | null> {
    const store = await getStore();
    const pairings = await store.get<Record<string, WallpaperPairing>>('wallpaperPairings') || {};
    return pairings[wallpaperPath] || null;
}

export async function saveWallpaperPairing(wallpaperPath: string, effect: string, settings: Record<string, number>) {
    const store = await getStore();
    const pairings = await store.get<Record<string, WallpaperPairing>>('wallpaperPairings') || {};
    pairings[wallpaperPath] = { effect, settings };
    await store.set('wallpaperPairings', pairings);
    await store.save();
}

export async function loadEffectSettings(effect: string): Promise<Record<string, number> | null> {
    const store = await getStore();
    const effectSettings = await store.get<Record<string, Record<string, number>>>('effectSettings') || {};
    return effectSettings[effect] || null;
}

export async function saveEffectSettings(effect: string, settings: Record<string, number>) {
    const store = await getStore();
    const effectSettings = await store.get<Record<string, Record<string, number>>>('effectSettings') || {};
    // Merge new settings with existing ones for this effect
    effectSettings[effect] = { ...(effectSettings[effect] || {}), ...settings };
    await store.set('effectSettings', effectSettings);
    await store.save();
}

export interface ActiveSession {
    layerA: string;
    layerB: string;
    effect: string;
}

export async function getActiveSession(): Promise<ActiveSession | null> {
    const store = await getStore();
    return await store.get<ActiveSession>('activeSession') || null;
}

export async function saveActiveSession(session: ActiveSession | null) {
    const store = await getStore();
    await store.set('activeSession', session);
    await store.save();
}
