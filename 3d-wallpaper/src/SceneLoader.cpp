#include "SceneLoader.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

// ---------------------------------------------------------------
// Helper: resolve a relative path against a base directory
// ---------------------------------------------------------------
static std::string ResolvePath(const std::string& baseDir, const std::string& relative) {
    // Replace forward slashes with backslashes for Windows
    std::string rel = relative;
    std::replace(rel.begin(), rel.end(), '/', '\\');

    // If already absolute, return as-is
    if (rel.size() >= 2 && rel[1] == ':') return rel;

    std::string result = baseDir;
    if (!result.empty() && result.back() != '\\' && result.back() != '/')
        result += '\\';
    result += rel;
    return result;
}

// ---------------------------------------------------------------
// Parse "#RRGGBB" hex string to normalized RGBA floats
// ---------------------------------------------------------------
void ParseHexColor(const std::string& hex, float outRGBA[4]) {
    outRGBA[0] = 0.0f; outRGBA[1] = 0.0f; outRGBA[2] = 0.0f; outRGBA[3] = 1.0f;

    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);

    if (h.size() == 6) {
        unsigned int r = 0, g = 0, b = 0;
        std::istringstream(h.substr(0, 2)) >> std::hex >> r;
        std::istringstream(h.substr(2, 2)) >> std::hex >> g;
        std::istringstream(h.substr(4, 2)) >> std::hex >> b;
        outRGBA[0] = std::pow(r / 255.0f, 2.2f);
        outRGBA[1] = std::pow(g / 255.0f, 2.2f);
        outRGBA[2] = std::pow(b / 255.0f, 2.2f);
    }
}

// ---------------------------------------------------------------
// Safe JSON accessors
// ---------------------------------------------------------------
template<typename T>
static T GetOr(const json& j, const std::string& key, T defaultVal) {
    if (j.contains(key) && !j[key].is_null()) {
        try { return j[key].get<T>(); } catch (...) {}
    }
    return defaultVal;
}

static void ReadFloat3(const json& j, const std::string& key, float out[3]) {
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
        for (int i = 0; i < 3; i++) {
            try { out[i] = j[key][i].get<float>(); } catch (...) {}
        }
    }
}

// ---------------------------------------------------------------
// LoadScene
// ---------------------------------------------------------------
bool LoadScene(const std::string& jsonPath, SceneData& outScene) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cout << "[SceneLoader] Failed to open: " << jsonPath << "\n";
        return false;
    }

    // Clear any existing data if we are hot-reloading into an existing struct
    outScene.objects.clear();
    outScene.lighting.directionalLights.clear();

    json root;
    try {
        root = json::parse(file);
    } catch (const json::parse_error& e) {
        std::cout << "[SceneLoader] JSON parse error: " << e.what() << "\n";
        return false;
    }

    // Determine the base directory of the scene file
    std::string baseDir;
    {
        size_t lastSlash = jsonPath.find_last_of("\\/");
        if (lastSlash != std::string::npos)
            baseDir = jsonPath.substr(0, lastSlash);
        else
            baseDir = ".";
    }

    // Top-level fields
    outScene.version    = GetOr<int>(root, "version", 1);
    outScene.name       = GetOr<std::string>(root, "name", "Untitled Scene");
    outScene.createdAt  = GetOr<std::string>(root, "createdAt", "");
    outScene.modifiedAt = GetOr<std::string>(root, "modifiedAt", "");

    // Background
    if (root.contains("background")) {
        auto& bg = root["background"];
        outScene.background.type  = GetOr<std::string>(bg, "type", "color");
        outScene.background.value = GetOr<std::string>(bg, "value", "#101418");
        outScene.background.fit   = GetOr<std::string>(bg, "fit", "cover");

        // If the background is an image, resolve the path
        if (outScene.background.type == "image") {
            outScene.background.value = ResolvePath(baseDir, outScene.background.value);
        }
    }

    // Camera
    if (root.contains("camera")) {
        auto& cam = root["camera"];
        outScene.camera.fov               = GetOr<float>(cam, "fov", 45.0f);
        outScene.camera.sensitivity       = GetOr<float>(cam, "sensitivity", 0.3f);
        outScene.camera.maxRotationOffset  = GetOr<float>(cam, "maxRotationOffset", 30.0f);
    }

    // Rendering options
    if (root.contains("rendering")) {
        auto& ren = root["rendering"];
        outScene.rendering.lightingEnabled = GetOr<bool>(ren, "lightingEnabled", false);
    }

    // Lighting
    if (root.contains("lighting")) {
        auto& lit = root["lighting"];
        if (lit.contains("ambientColor")) {
            float c[4];
            ParseHexColor(lit["ambientColor"], c);
            outScene.lighting.ambientColor[0] = c[0];
            outScene.lighting.ambientColor[1] = c[1];
            outScene.lighting.ambientColor[2] = c[2];
        }
        outScene.lighting.exposure = GetOr<float>(lit, "exposure", 1.0f);
        if (lit.contains("directionalLights") && lit["directionalLights"].is_array()) {
            for (const auto& dl : lit["directionalLights"]) {
                SceneDirectionalLight light;
                if (dl.contains("color")) {
                    float c[4];
                    ParseHexColor(dl["color"], c);
                    light.color[0] = c[0];
                    light.color[1] = c[1];
                    light.color[2] = c[2];
                }
                if (dl.contains("direction") && dl["direction"].is_array() && dl["direction"].size() >= 3) {
                    light.direction[0] = dl["direction"][0];
                    light.direction[1] = dl["direction"][1];
                    light.direction[2] = dl["direction"][2];
                }
                if (dl.contains("intensity")) light.intensity = dl["intensity"];
                outScene.lighting.directionalLights.push_back(light);
            }
        }
    }

    // Objects
    if (root.contains("objects") && root["objects"].is_array()) {
        for (auto& objJson : root["objects"]) {
            SceneObject obj;
            obj.id = GetOr<std::string>(objJson, "id", "");

            std::string relModelPath = GetOr<std::string>(objJson, "modelPath", "");
            obj.modelPath = ResolvePath(baseDir, relModelPath);

            ReadFloat3(objJson, "position", obj.position);
            ReadFloat3(objJson, "rotation", obj.rotation);
            ReadFloat3(objJson, "scale",    obj.scale);

            obj.followMouse        = GetOr<bool>(objJson, "followMouse", true);
            obj.rotationMultiplier = GetOr<float>(objJson, "rotationMultiplier", 1.0f);

            outScene.objects.push_back(std::move(obj));
        }
    }

    std::cout << "[SceneLoader] Loaded scene: \"" << outScene.name << "\" with "
              << outScene.objects.size() << " object(s).\n";
    return true;
}
