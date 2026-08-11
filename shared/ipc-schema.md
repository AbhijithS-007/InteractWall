# InteractWall IPC Schema

The InteractWall renderer runs a named pipe server at `\\.\pipe\InteractWall`.
It accepts newline-delimited JSON commands from clients.

## Commands

### `apply_wallpaper`
Changes the active wallpaper layers.
```json
{
  "cmd": "apply_wallpaper",
  "layerA": "C:\\path\\to\\wallpaperA.jpg",
  "layerB": "C:\\path\\to\\wallpaperB.png"
}
```

### `set_effect`
Changes the active effect plugin.
```json
{
  "cmd": "set_effect",
  "plugin": "cursor_reveal"
}
```

### `set_setting`
Updates a setting on the active effect plugin.
```json
{
  "cmd": "set_setting",
  "key": "brushSize",
  "value": 80
}
```

### `set_quality_tier`
Overrides the current quality tier.
```json
{
  "cmd": "set_quality_tier",
  "tier": "low" // Or "balanced", "high"
}
```

### `remove_effect`
Removes the active effect plugin and resets to the base wallpaper.
```json
{
  "cmd": "remove_effect"
}
```

### `get_status`
Requests the current engine status. The renderer will respond on the same named pipe with a JSON object.
**Request:**
```json
{
  "cmd": "get_status"
}
```

**Response:**
```json
{
  "fps": 60.0,
  "cpu": 2.5,
  "gpuMemMB": 128.5,
  "state": "VISIBLE_ACTIVE",
  "tier": "HIGH",
  "activePlugin": "cursor_reveal"
}
```
