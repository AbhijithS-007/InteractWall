#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>

// ---------------------------------------------------------------
// Scene data structures — mirrors docs/scene-schema.md (v1)
// ---------------------------------------------------------------

struct SceneBackground {
    std::string type  = "color";   // "color" or "image"
    std::string value = "#101418"; // hex color or relative image path
    std::string fit   = "cover";   // "cover"|"contain"|"stretch"|"tile"
};

struct SceneCamera {
    float fov                = 45.0f;
    float sensitivity        = 0.3f;
    float maxRotationOffset  = 30.0f; // degrees
};

struct SceneObject {
    std::string id;
    std::string modelPath;         // relative path to .glb

    float position[3]  = { 0, 0, 0 };
    float rotation[3]  = { 0, 0, 0 };   // Euler XYZ in degrees (base/resting)
    float scale[3]     = { 1, 1, 1 };

    bool  followMouse        = true;
    float rotationMultiplier = 1.0f;
};

struct SceneDirectionalLight {
    float color[3] = {1.0f, 1.0f, 1.0f};
    float direction[3] = {0.0f, -1.0f, 0.0f};
    float intensity = 1.0f;
};

struct SceneLighting {
    float ambientColor[3] = {0.2f, 0.2f, 0.2f};
    std::vector<SceneDirectionalLight> directionalLights;
    float exposure = 1.0f;
};

struct SceneRendering {
    bool lightingEnabled = false;
    std::string qualityTier = "high"; // "low", "balanced", "high"
};

struct SceneData {
    int         version = 1;
    std::string name;
    std::string createdAt;
    std::string modifiedAt;

    SceneBackground background;
    SceneCamera     camera;
    SceneRendering  rendering;
    SceneLighting   lighting;
    std::vector<SceneObject> objects;
};

// Parse a scene.json file. Returns true on success.
// All relative model paths in SceneObject::modelPath are resolved
// to absolute paths relative to the directory containing jsonPath.
bool LoadScene(const std::string& jsonPath, SceneData& outScene);

// Parse a "#RRGGBB" hex string into normalized float RGBA.
void ParseHexColor(const std::string& hex, float outRGBA[4]);
