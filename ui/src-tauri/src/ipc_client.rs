use serde_json::json;
use std::fs::{self, OpenOptions};
use std::io::{Read, Write};
use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use tauri::command;

const PIPE_NAME: &str = r"\\.\pipe\Graffiti";
const CREATE_NO_WINDOW: u32 = 0x08000000;

fn get_renderer_path() -> PathBuf {
    // Try relative to exe first (for packaged builds), then fall back to dev path
    if let Ok(exe) = std::env::current_exe() {
        let relative = exe
            .parent()
            .unwrap_or(Path::new("."))
            .join("GraffitiRenderer.exe");
        if relative.exists() {
            return relative;
        }
    }
    // Dev fallback: hardcoded project path
    PathBuf::from(r"C:\My_Proj\InteractWall\renderer\build\Release\GraffitiRenderer.exe")
}

fn send_ipc_message(msg: &serde_json::Value) -> Result<String, String> {
    // Check if pipe exists, if not try spawning the renderer
    let mut file = match OpenOptions::new().read(true).write(true).open(PIPE_NAME) {
        Ok(f) => f,
        Err(_) => {
            println!("[IPC] Pipe not found, attempting to spawn renderer...");
            let renderer_path = get_renderer_path();
            Command::new(&renderer_path)
                .creation_flags(CREATE_NO_WINDOW)
                .spawn()
                .map_err(|e| format!("Failed to spawn renderer at {:?}: {}", renderer_path, e))?;

            // Wait for it to start
            std::thread::sleep(std::time::Duration::from_millis(500));

            // Try again
            OpenOptions::new()
                .read(true)
                .write(true)
                .open(PIPE_NAME)
                .map_err(|e| format!("Renderer started, but pipe still failed: {}", e))?
        }
    };

    let mut payload = msg.to_string();
    payload.push('\n');

    file.write_all(payload.as_bytes())
        .map_err(|e| e.to_string())?;

    // If it's a command that returns data, read response
    let cmd_str = msg.get("cmd").and_then(|v| v.as_str());
    if cmd_str == Some("get_status") || cmd_str == Some("get_adapters") || cmd_str == Some("apply_wallpaper") {
        let mut buffer = [0; 4096];
        let bytes_read = file.read(&mut buffer).map_err(|e| e.to_string())?;
        let response = String::from_utf8_lossy(&buffer[..bytes_read]).to_string();
        if cmd_str == Some("apply_wallpaper") && response.contains("\"error\"") {
            return Err(format!("Renderer failed to apply native wallpaper. Response: {}", response));
        }
        
        Ok(response.trim().to_string())
    } else {
        Ok("".to_string())
    }
}

#[command]
pub fn apply(layer_a: String, layer_b: String) -> Result<(), String> {
    let msg = json!({
        "cmd": "apply_wallpaper",
        "layerA": layer_a.clone(),
        "layerB": layer_b
    });
    println!("[ipc_client.rs] Sending apply_wallpaper. EXACT path: {}", layer_a);
    send_ipc_message(&msg)?;
    Ok(())
}

#[command]
pub fn set_effect(plugin: String) -> Result<(), String> {
    let msg = json!({
        "cmd": "set_effect",
        "plugin": plugin
    });
    send_ipc_message(&msg)?;
    Ok(())
}

#[command]
pub fn remove_effect() -> Result<(), String> {
    let msg = json!({
        "cmd": "remove_effect"
    });
    send_ipc_message(&msg)?;
    Ok(())
}

#[command]
pub fn set_setting(key: String, value: f32) -> Result<(), String> {
    let msg = json!({
        "cmd": "set_setting",
        "key": key,
        "value": value
    });
    send_ipc_message(&msg)?;
    Ok(())
}

#[command]
pub fn set_setting_str(key: String, value: String) -> Result<(), String> {
    let msg = json!({
        "cmd": "set_setting",
        "key": key,
        "valueStr": value
    });
    send_ipc_message(&msg)?;
    Ok(())
}

#[command]
pub fn set_quality_tier(tier: String) -> Result<(), String> {
    let msg = json!({
        "cmd": "set_quality_tier",
        "tier": tier
    });
    send_ipc_message(&msg)?;
    Ok(())
}

#[command]
pub fn get_status() -> Result<serde_json::Value, String> {
    let msg = json!({"cmd": "get_status"});
    let resp = send_ipc_message(&msg)?;
    if resp.is_empty() {
        return Err("Empty response".to_string());
    }
    serde_json::from_str(&resp).map_err(|e| format!("Failed to parse status JSON: {}", e))
}

#[command]
pub fn import_wallpaper(file_path: String) -> Result<String, String> {
    let source = Path::new(&file_path);
    if !source.exists() {
        return Err("Source file does not exist".to_string());
    }

    let file_name = source.file_name().ok_or("Invalid file name")?;

    // Resolve %APPDATA%\Graffiti\wallpapers
    let app_data = std::env::var("APPDATA").map_err(|_| "Could not find APPDATA".to_string())?;
    let mut target_dir = PathBuf::from(app_data);
    target_dir.push("Graffiti");
    target_dir.push("wallpapers");

    if !target_dir.exists() {
        fs::create_dir_all(&target_dir).map_err(|e| e.to_string())?;
    }

    let target_path = target_dir.join(file_name);
    fs::copy(source, &target_path).map_err(|e| e.to_string())?;

    println!("[IPC Stub] Imported wallpaper to {:?}", target_path);

    Ok(target_path.to_string_lossy().to_string())
}

#[tauri::command]
pub async fn list_wallpapers() -> Result<Vec<String>, String> {
    let app_data = std::env::var("APPDATA").map_err(|_| "Could not find APPDATA".to_string())?;
    let mut target_dir = PathBuf::from(app_data);
    target_dir.push("Graffiti");
    target_dir.push("baked_wallpapers");

    let mut wallpapers = Vec::new();
    if target_dir.exists() {
        if let Ok(entries) = fs::read_dir(&target_dir) {
            for entry in entries.flatten() {
                if let Ok(file_type) = entry.file_type() {
                    if file_type.is_file() {
                        let file_name = entry.file_name().to_string_lossy().to_string();
                        if file_name != "active-collage.png" && !file_name.ends_with("_depth.png") {
                            wallpapers.push(entry.path().to_string_lossy().to_string());
                        }
                    }
                }
            }
        }
    }
    
    Ok(wallpapers)
}

#[command]
pub fn save_baked_wallpaper(filename: String, bytes: Vec<u8>) -> Result<String, String> {
    let app_data = std::env::var("APPDATA").map_err(|_| "Could not find APPDATA".to_string())?;
    let mut target_dir = PathBuf::from(app_data);
    target_dir.push("Graffiti");
    target_dir.push("baked_wallpapers");

    if !target_dir.exists() {
        fs::create_dir_all(&target_dir).map_err(|e| e.to_string())?;
    }

    let target_path = target_dir.join(&filename);
    
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(&target_path)
        .map_err(|e| e.to_string())?;
        
    file.write_all(&bytes).map_err(|e| e.to_string())?;

    println!("[IPC Stub] Saved baked wallpaper to {:?}", target_path);

    Ok(target_path.to_string_lossy().to_string())
}

#[command]
pub fn delete_baked_wallpaper(path: String) -> Result<(), String> {
    std::fs::remove_file(&path).map_err(|e| format!("Failed to delete file {}: {}", path, e))?;
    Ok(())
}

pub fn send_quit_command() {
    if let Ok(mut file) = OpenOptions::new().read(true).write(true).open(PIPE_NAME) {
        let msg = json!({"cmd": "quit"}).to_string() + "\n";
        let _ = file.write_all(msg.as_bytes());
        let _ = file.flush();
    }
}

#[command]
pub fn clear_wallpaper() -> Result<(), String> {
    let msg = json!({"cmd": "clear_wallpaper"});
    send_ipc_message(&msg)?;
    Ok(())
}