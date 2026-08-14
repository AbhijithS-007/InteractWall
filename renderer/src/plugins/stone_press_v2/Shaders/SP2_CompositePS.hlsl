Texture2D BaseTex : register(t0);
SamplerState Sampler : register(s0);

cbuffer CompositeCB : register(b0) {
    float parallaxStrength;
    float depthDarkening;
    float directionalShading;
    int enableFX;
    
    float2 cursorUV;
    float pressRadius;
    float pressDepth;
    
    float aspectRatio;
    float pad[3];
}

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    if (enableFX == 0) {
        return BaseTex.SampleLevel(Sampler, input.uv, 0);
    }
    
    float2 cx = cursorUV;
    float2 delta = input.uv - cx;
    
    // Calculate distance in screen-space
    float2 screenDelta = float2(delta.x * aspectRatio, delta.y);
    float dist = length(screenDelta);
    
    float2 displacedUV = input.uv;
    
    // Default flat surface normal
    float3 normal = float3(0.0, 0.0, 1.0);
    float AO = 1.0;
    
    if (dist < pressRadius) {
        // Prevent singularity / extreme gradients right at the exact core
        // Treat any distance closer than 1.5% of the radius as exactly 1.5%
        float safeDist = max(dist, pressRadius * 0.015);
        float normDist = safeDist / pressRadius;
        float currentDepth = parallaxStrength * pressDepth;
        
        // 1. UV Displacement (Massive Funnel Stretch)
        // Cap the maximum gradient magnitude via 'S' to prevent excessive out-of-bounds smearing 
        // while maintaining the smooth mathematical curve (no hard min() clamps that cause concentric rings).
        float S = clamp(currentDepth * 8.0, 0.0, 6.0); 
        float stretch = S * normDist * (1.0 - normDist) * exp(-4.0 * normDist);
        
        float2 dir = screenDelta / safeDist; // Use safeDist to avoid division by zero or jitter
        displacedUV.x += (dir.x / aspectRatio) * stretch * pressRadius;
        displacedUV.y += dir.y * stretch * pressRadius;
        
        // 2. 3D Surface Normals (for Lambertian Shading)
        // Heightmap Z(x) = -Depth * exp(-4.0 * x) * (1.0 - x)
        // Analytical derivative dZ/dx = Depth * exp(-4.0 * x) * (5.0 - 4.0 * x)
        float dZ_dx_norm = currentDepth * exp(-4.0 * normDist) * (5.0 - 4.0 * normDist);
        float dZ_dr = dZ_dx_norm / pressRadius;
        
        float dZ_dx = dZ_dr * (screenDelta.x / safeDist);
        float dZ_dy = dZ_dr * (screenDelta.y / safeDist);
        
        // Exaggerate normals for dramatic lighting
        normal = normalize(float3(-dZ_dx * 2.5, -dZ_dy * 2.5, 1.0));
        
        // 3. Extreme Core Darkening (Event Horizon)
        // Exponential falloff so only the very deepest part gets pitched black.
        float coreShadow = exp(-8.0 * normDist);
        AO = 1.0 - saturate(coreShadow * depthDarkening);
    }
    
    // Explicit inset clamp to guarantee no wrap-around or sampler edge bleeding
    displacedUV = clamp(displacedUV, 0.001, 0.999);
    float4 color = BaseTex.SampleLevel(Sampler, displacedUV, 0);
    
    // 4. Pure Lambertian Shading (No specular plastic gloss)
    if (dist < pressRadius && directionalShading > 0.0) {
        // Virtual light from Top-Left, slightly elevated
        float3 lightDir = normalize(float3(-0.7, -0.7, 0.8)); 
        
        float NdotL = max(0.0, dot(normal, lightDir));
        
        // Map NdotL (0 to 1) to (ambient to 1.0)
        float ambient = 0.2; 
        float diffuse = ambient + NdotL * (1.0 - ambient);
        
        // Multiply texture color by the diffuse light and ambient occlusion
        float3 shadedColor = color.rgb * diffuse * AO;
        
        // Blend in the directional shading based on the slider
        color.rgb = lerp(color.rgb * AO, shadedColor, directionalShading);
    } else {
        // If directional shading is off, just apply the core darkening
        color.rgb *= AO;
    }
    
    return color;
}
