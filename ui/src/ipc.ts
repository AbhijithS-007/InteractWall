import { invoke } from "@tauri-apps/api/core";

export async function applyWallpaper(layerA: string, layerB: string) {
    return invoke("apply", { layerA, layerB });
}

export async function setEffect(plugin: string) {
    return invoke("set_effect", { plugin });
}

export async function removeEffect() {
    return invoke("remove_effect");
}

export async function setSetting(key: string, value: number) {
    return invoke("set_setting", { key, value });
}

export async function setSettingStr(key: string, value: string) {
    return invoke("set_setting_str", { key, value });
}

export async function setQualityTier(tier: string) {
    return invoke("set_quality_tier", { tier });
}

export async function getStatus(): Promise<any> {
    return invoke("get_status");
}

export async function importWallpaper(filePath: string): Promise<string> {
    return invoke("import_wallpaper", { filePath });
}

export async function listWallpapers(): Promise<string[]> {
    return invoke("list_wallpapers");
}

export async function generateDepthMap(sourcePath: string): Promise<string> {
    return invoke("generate_depth_map", { sourcePath });
}

export async function saveBakedWallpaper(filename: string, bytes: Uint8Array): Promise<string> {
    return invoke("save_baked_wallpaper", { filename, bytes });
}

export async function deleteBakedWallpaper(path: string): Promise<void> {
    return invoke("delete_baked_wallpaper", { path });
}

export async function isAutostart(): Promise<boolean> {
    return invoke("is_autostart");
}

export async function fileExists(path: string): Promise<boolean> {
    return invoke("file_exists", { path });
}

export async function clearWallpaper(): Promise<void> {
    return invoke('clear_wallpaper');
}



export async function startWebWallpaper(model: string, bgType: string, bgColor?: string, bgImage?: string): Promise<void> {
    return invoke("start_web_wallpaper", { model, bgType, bgColor, bgImage });
}

export async function stopWebWallpaper(): Promise<void> {
    return invoke("stop_web_wallpaper");
}

export async function importWebAsset(filePath: string): Promise<string> {
    return invoke("import_web_asset", { filePath });
}