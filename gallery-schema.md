# Gallery Project Schema

The Gallery editor saves its state to `gallery-project.json`. This document defines the schema for that JSON file.

## Root Object

| Property     | Type     | Description                                                                 |
| ------------ | -------- | --------------------------------------------------------------------------- |
| `version`    | String   | Schema version (e.g., "1.0")                                                |
| `background` | Object   | Defines the canvas background (see Background Object below)                 |
| `shapes`     | Array    | List of shape objects drawn on the canvas, rendered from back (0) to front. |

## Background Object

| Property | Type   | Description                                                                                       |
| -------- | ------ | ------------------------------------------------------------------------------------------------- |
| `type`   | String | `"color"` or `"image"`                                                                            |
| `value`  | String | Hex color string (if type is `"color"`) or file path (if type is `"image"`)                       |
| `fit`    | String | Fit mode if type is `"image"`: `"cover"`, `"contain"`, `"stretch"`, or `"tile"`. Defaults to `"cover"`. |

## Shape Object

| Property    | Type   | Description                                                                                 |
| ----------- | ------ | ------------------------------------------------------------------------------------------- |
| `id`        | String | Unique identifier for the shape (e.g., UUID).                                               |
| `vertices`  | Array  | Array of coordinate pairs `[x, y]` in normalized 0-1 canvas space. Minimum 3 vertices.      |
| `imagePath` | String | Relative or absolute path to the imported image for this shape. null if no image loaded.    |
| `transform` | Object | The transform settings for the clipped image (see Transform Object below).                  |

## Transform Object

Controls the pan, zoom, and rotation of the image **within** the clipped polygon shape. 
The origin for rotation and scaling is the bounding box center of the shape.

| Property | Type   | Description                                                       |
| -------- | ------ | ----------------------------------------------------------------- |
| `panX`   | Number | X translation offset (in normalized coordinates, usually).        |
| `panY`   | Number | Y translation offset.                                             |
| `zoom`   | Number | Scale factor (1.0 is default original size relative to shape).    |
| `rotate` | Number | Rotation in radians (or degrees, but consistency is key).         |

## Example `gallery-project.json`

```json
{
  "version": "1.0",
  "background": {
    "type": "color",
    "value": "#1a1a1a",
    "fit": "cover"
  },
  "shapes": [
    {
      "id": "shape-12345",
      "vertices": [
        [0.1, 0.1],
        [0.4, 0.1],
        [0.4, 0.5],
        [0.1, 0.5]
      ],
      "imagePath": "C:/path/to/imported/image.jpg",
      "transform": {
        "panX": 0.0,
        "panY": 0.0,
        "zoom": 1.0,
        "rotate": 0.0
      }
    }
  ]
}
```
