use image::{imageops::FilterType, Luma};
use imageproc::filter::gaussian_blur_f32;
use ort::init;
use ort::session::builder::GraphOptimizationLevel;
use ort::session::Session;
use std::path::{Path, PathBuf};
use std::sync::OnceLock;

static ONNX_INITIALIZED: OnceLock<bool> = OnceLock::new();

fn init_ort() -> Result<(), String> {
    if ONNX_INITIALIZED.get().is_none() {
        let _ = ort::init().with_name("Graffiti").commit();
        let _ = ONNX_INITIALIZED.set(true);
    }
    Ok(())
}

#[tauri::command]
pub async fn generate_depth_map(source_path: String) -> Result<String, String> {
    init_ort()?;

    let source = Path::new(&source_path);
    if !source.exists() {
        return Err("Source file does not exist".to_string());
    }

    // Resolve model path
    let exe_dir = std::env::current_exe()
        .map_err(|e| e.to_string())?
        .parent()
        .unwrap()
        .to_path_buf();

    let mut model_path = PathBuf::from(r"C:\My_Proj\InteractWall\depth_model.onnx");
    if !model_path.exists() {
        model_path = exe_dir.join("depth_model.onnx");
    }
    if !model_path.exists() {
        return Err("ONNX model not found".to_string());
    }

    let mut session = Session::builder()
        .map_err(|e| format!("Session build error: {}", e))?
        .with_optimization_level(GraphOptimizationLevel::Level3)
        .map_err(|e| format!("Optimization error: {}", e))?
        .commit_from_file(&model_path)
        .map_err(|e| format!("Failed to load ONNX model: {}", e))?;

    // Load and preprocess image
    let img = image::open(&source).map_err(|e| format!("Failed to open image: {}", e))?;
    let orig_width = img.width();
    let orig_height = img.height();

    // Resize to 518x518 (Depth Anything V2 default)
    let resized = img.resize_exact(518, 518, FilterType::Triangle).into_rgb8();

    // Create tensor
    let mut input_vec = vec![0.0f32; 1 * 3 * 518 * 518];
    let mean = [0.485, 0.456, 0.406];
    let std = [0.229, 0.224, 0.225];

    for (x, y, pixel) in resized.enumerate_pixels() {
        let x = x as usize;
        let y = y as usize;
        for c in 0..3 {
            let val = (pixel[c] as f32 / 255.0 - mean[c]) / std[c];
            input_vec[c * (518 * 518) + y * 518 + x] = val;
        }
    }

    let input_tensor = ort::value::Tensor::from_array((vec![1, 3, 518, 518], input_vec))
        .map_err(|e| format!("Tensor err: {}", e))?;

    // Run inference
    let outputs = session
        .run(ort::inputs![input_tensor])
        .map_err(|e| format!("Inference failed: {}", e))?;

    // Depth anything v2 output is named "predicted_depth" or "depth"
    let output_val = outputs.values().next().unwrap();
    let (_, data) = output_val
        .try_extract_tensor::<f32>()
        .map_err(|e| format!("Extract tensor failed: {}", e))?;

    // Find min and max for normalization
    let mut min_val = f32::MAX;
    let mut max_val = f32::MIN;
    for &val in data.iter() {
        if val.is_nan() || val.is_infinite() {
            continue;
        }
        if val < min_val {
            min_val = val;
        }
        if val > max_val {
            max_val = val;
        }
    }

    let mut depth_img = image::ImageBuffer::new(518, 518);
    let range = max_val - min_val;

    for y in 0..518 {
        for x in 0..518 {
            let mut val = data[y as usize * 518 + x as usize];
            if val.is_nan() || val.is_infinite() {
                val = min_val; // Default bad pixels to far background
            }

            let normalized = if range > 0.0 {
                (val - min_val) / range
            } else {
                0.0
            };

            let luma = (normalized * 255.0).clamp(0.0, 255.0) as u8;
            depth_img.put_pixel(x, y, Luma([luma]));
        }
    }

    // Blur to reduce noise (Gaussian blur with sigma=3.0)
    let blurred = gaussian_blur_f32(&depth_img, 3.0);

    // Resize back to orig resolution, but capped at 1920x1080
    let cap_w = orig_width.min(1920);
    let cap_h = orig_height.min(1080);
    let final_depth =
        image::DynamicImage::ImageLuma8(blurred).resize_exact(cap_w, cap_h, FilterType::Triangle);

    // Save as _depth.png
    let file_stem = source.file_stem().unwrap().to_string_lossy();
    let file_name = format!("{}_depth.png", file_stem);

    let target_dir = source.parent().ok_or("Invalid source path")?;
    let target_path = target_dir.join(file_name);
    
    final_depth
        .save(&target_path)
        .map_err(|e| format!("Failed to save depth map: {}", e))?;

    Ok(target_path.to_string_lossy().to_string())
}
