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
