import { getActiveSession } from './store';
import { stopWebWallpaper as ipcStopWebWallpaper, applyWallpaper as ipcApplyWallpaper, setEffect as ipcSetEffect, removeEffect as ipcRemoveEffect } from './ipc';

export async function stopWebWallpaperIfNeeded() {
    try {
        const session = await getActiveSession();
        if (session?.effect === 'web-wallpaper') {
            console.log("[Manager] Switching away from Web Wallpaper. Stopping it first...");
            await ipcStopWebWallpaper();
        }
    } catch (e) {
        console.error("Failed to check or stop web wallpaper:", e);
    }
}

export async function applyWallpaper(layerA: string, layerB: string) {
    await stopWebWallpaperIfNeeded();
    return ipcApplyWallpaper(layerA, layerB);
}

export async function setEffect(plugin: string) {
    await stopWebWallpaperIfNeeded();
    return ipcSetEffect(plugin);
}

export async function removeEffect() {
    await stopWebWallpaperIfNeeded();
    return ipcRemoveEffect();
}
