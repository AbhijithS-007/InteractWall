cbuffer CBQuad : register(b0) {
    float screenWidth;
    float screenHeight;
    float imageWidth;
    float imageHeight;
    int fitMode; // 0=cover, 1=contain, 2=stretch, 3=tile
    float padding[3];
};

Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.texcoord;
    float screenAspect = screenWidth / screenHeight;
    float imageAspect = imageWidth / imageHeight;

    if (fitMode == 0) {
        // Cover
        if (screenAspect > imageAspect) {
            // Screen is wider than image. Scale image height to match width.
            float scale = screenWidth / imageWidth;
            float newImageHeight = imageHeight * scale;
            float offset = (newImageHeight - screenHeight) / 2.0 / newImageHeight;
            uv.y = uv.y * (screenHeight / newImageHeight) + offset;
        } else {
            // Screen is taller than image. Scale image width to match height.
            float scale = screenHeight / imageHeight;
            float newImageWidth = imageWidth * scale;
            float offset = (newImageWidth - screenWidth) / 2.0 / newImageWidth;
            uv.x = uv.x * (screenWidth / newImageWidth) + offset;
        }
    } else if (fitMode == 1) {
        // Contain
        if (screenAspect > imageAspect) {
            // Screen is wider. Fit to height, center width.
            float scale = screenHeight / imageHeight;
            float newImageWidth = imageWidth * scale;
            float offset = (screenWidth - newImageWidth) / 2.0 / screenWidth;
            uv.x = (uv.x - offset) / (newImageWidth / screenWidth);
            if (uv.x < 0.0 || uv.x > 1.0) discard;
        } else {
            // Screen is taller. Fit to width, center height.
            float scale = screenWidth / imageWidth;
            float newImageHeight = imageHeight * scale;
            float offset = (screenHeight - newImageHeight) / 2.0 / screenHeight;
            uv.y = (uv.y - offset) / (newImageHeight / screenHeight);
            if (uv.y < 0.0 || uv.y > 1.0) discard;
        }
    } else if (fitMode == 2) {
        // Stretch (default UVs 0..1 map exactly to 0..1)
        // Nothing to do.
    } else if (fitMode == 3) {
        // Tile (repeat native resolution)
        uv.x = uv.x * (screenWidth / imageWidth);
        uv.y = uv.y * (screenHeight / imageHeight);
    }

    return tex.Sample(samp, uv);
}
