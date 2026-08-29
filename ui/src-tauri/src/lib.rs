// Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
mod depth;
mod ipc_client;

use tauri::Manager;
use std::path::Path;

#[tauri::command]
fn is_autostart() -> bool {
    std::env::args().any(|arg| arg == "--autostart")
}

#[tauri::command]
fn file_exists(path: String) -> bool {
    Path::new(&path).exists()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // 1-TIME MIGRATION: Rename %APPDATA%\InteractWall to %APPDATA%\Graffiti
    if let Some(appdata) = std::env::var_os("APPDATA") {
        let old_path = std::path::Path::new(&appdata).join("InteractWall");
        let new_path = std::path::Path::new(&appdata).join("Graffiti");
        
        if old_path.exists() && !new_path.exists() {
            println!("[Migration] Renaming legacy 'InteractWall' data folder to 'Graffiti'...");
            if let Err(e) = std::fs::rename(&old_path, &new_path) {
                eprintln!("[Migration] FAILED to rename data folder: {}", e);
            } else {
                println!("[Migration] Successfully migrated data folder.");
            }
        } else if new_path.exists() {
            println!("[Migration] Skipped (already migrated or fresh install).");
        } else {
            println!("[Migration] Skipped (fresh install, no old data found).");
        }
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_store::Builder::new().build())
        .plugin(tauri_plugin_autostart::Builder::new().args(vec!["--autostart"]).build())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            let quit_i = tauri::menu::MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let show_i =
                tauri::menu::MenuItem::with_id(app, "show", "Show Settings", true, None::<&str>)?;
            let menu = tauri::menu::Menu::with_items(app, &[&show_i, &quit_i])?;

            let _tray = tauri::tray::TrayIconBuilder::new()
                .icon(app.default_window_icon().unwrap().clone())
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "quit" => {
                        ipc_client::send_quit_command();
                        app.exit(0);
                    }
                    "show" => {
                        if let Some(window) = app.get_webview_window("main") {
                            let _ = window.show();
                            let _ = window.set_focus();
                        }
                    }
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if let tauri::tray::TrayIconEvent::DoubleClick { .. } = event {
                        if let Some(window) = tray.app_handle().get_webview_window("main") {
                            let _ = window.show();
                            let _ = window.set_focus();
                        }
                    }
                })
                .build(app)?;

            // If it's NOT an autostart, we want to show the UI.
            // If it IS an autostart, it starts hidden (because of tauri.conf.json visible: false).
            if !std::env::args().any(|arg| arg == "--autostart") {
                if let Some(window) = app.get_webview_window("main") {
                    let _ = window.show();
                    let _ = window.set_focus();
                }
            }

            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                let _ = window.hide();
                api.prevent_close();
            }
        })
        .invoke_handler(tauri::generate_handler![
            ipc_client::apply,
            ipc_client::set_effect,
            ipc_client::remove_effect,
            ipc_client::set_setting,
            ipc_client::set_setting_str,
            ipc_client::set_quality_tier,
            ipc_client::get_status,
            ipc_client::import_wallpaper,
            ipc_client::list_wallpapers,
            ipc_client::save_baked_wallpaper,
            ipc_client::delete_baked_wallpaper,
            ipc_client::clear_wallpaper,
            ipc_client::start_web_wallpaper,
            ipc_client::stop_web_wallpaper,
            ipc_client::import_web_asset,
            depth::generate_depth_map,
            is_autostart,
            file_exists,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
